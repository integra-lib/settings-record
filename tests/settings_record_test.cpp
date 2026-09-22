#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <integra/settings_record.hpp>
#include <map>
#include <span>
#include <string_view>
#include <vector>

namespace
{

using integra::ReadSettingsRecord;
using integra::SettingsRecord;
using integra::WriteSettingsRecord;

// Stand-in for the device's flash-backed store.
class FakeStorage
{
public:
    [[nodiscard]] bool Write(std::uint16_t id, std::span<const std::uint8_t> data)
    {
        if (m_failWrites)
        {
            return false;
        }
        m_blobs[id].assign(data.begin(), data.end());
        return true;
    }

    [[nodiscard]] bool Read(std::uint16_t id, std::span<std::uint8_t> data)
    {
        const auto found = m_blobs.find(id);
        if (found == m_blobs.end() || found->second.size() != data.size())
        {
            return false;
        }
        std::ranges::copy(found->second, data.begin());
        return true;
    }

    void FailWrites()
    {
        m_failWrites = true;
    }

    [[nodiscard]] std::vector<std::uint8_t>& Blob(std::uint16_t id)
    {
        return m_blobs.at(id);
    }

    [[nodiscard]] bool Has(std::uint16_t id) const
    {
        return m_blobs.contains(id);
    }

private:
    std::map<std::uint16_t, std::vector<std::uint8_t>> m_blobs;
    bool m_failWrites{false};
};

struct Calibration
{
    std::uint32_t offset{};
    float scale{};
    std::uint8_t channel{};

    [[nodiscard]] bool operator==(const Calibration&) const = default;
};

constexpr std::uint16_t RECORD_ID = 7U;

// The checksum a174-hardware's settings-storage wrote, bit for bit: CRC-32/ISO-HDLC
// spelled out by hand. Kept here so a change to Integra::crc that moved this
// component to a different checksum could not pass unnoticed — devices in the field
// already hold records written with this one.
[[nodiscard]] std::uint32_t LegacyCalcCrc(std::span<const std::uint8_t> bytes)
{
    constexpr std::uint32_t INIT = 0xFFFFFFFFU;
    constexpr std::uint32_t POLY = 0xEDB88320U;

    std::uint32_t crc = INIT;
    for (const auto byte : bytes)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ (POLY * (crc & 1U));
        }
    }
    return crc ^ INIT;
}

TEST(SettingsRecordTest, ReadsBackWhatItWrote)
{
    FakeStorage storage;
    const Calibration written{1234U, 2.5F, 3U};

    ASSERT_TRUE(WriteSettingsRecord(storage, RECORD_ID, written));

    Calibration read{};
    ASSERT_TRUE(ReadSettingsRecord(storage, RECORD_ID, read));
    EXPECT_EQ(read, written);
}

TEST(SettingsRecordTest, ReportsAMissingRecord)
{
    FakeStorage storage;

    Calibration read{9U, 9.0F, 9U};
    EXPECT_FALSE(ReadSettingsRecord(storage, RECORD_ID, read));
    // Untouched, so the caller's default survives a first boot.
    EXPECT_EQ(read, (Calibration{9U, 9.0F, 9U}));
}

TEST(SettingsRecordTest, RejectsACorruptedPayload)
{
    FakeStorage storage;
    ASSERT_TRUE(WriteSettingsRecord(storage, RECORD_ID, Calibration{1234U, 2.5F, 3U}));

    // One flipped bit past the checksum.
    storage.Blob(RECORD_ID).at(sizeof(std::uint32_t)) ^= 0x01U;

    Calibration read{};
    EXPECT_FALSE(ReadSettingsRecord(storage, RECORD_ID, read));
}

TEST(SettingsRecordTest, RejectsACorruptedChecksum)
{
    FakeStorage storage;
    ASSERT_TRUE(WriteSettingsRecord(storage, RECORD_ID, Calibration{1234U, 2.5F, 3U}));

    storage.Blob(RECORD_ID).at(0U) ^= 0xFFU;

    Calibration read{};
    EXPECT_FALSE(ReadSettingsRecord(storage, RECORD_ID, read));
}

TEST(SettingsRecordTest, PassesOnAStorageFailure)
{
    FakeStorage storage;
    storage.FailWrites();

    EXPECT_FALSE(WriteSettingsRecord(storage, RECORD_ID, Calibration{1U, 1.0F, 1U}));
    EXPECT_FALSE(storage.Has(RECORD_ID));
}

TEST(SettingsRecordTest, KeepsTheOnFlashLayout)
{
    FakeStorage storage;
    const Calibration written{0xDEADBEEFU, 1.25F, 0x5AU};
    ASSERT_TRUE(WriteSettingsRecord(storage, RECORD_ID, written));

    const auto& blob = storage.Blob(RECORD_ID);
    // Checksum first, then the payload — the layout a174-hardware already wrote.
    ASSERT_EQ(blob.size(), sizeof(SettingsRecord<Calibration>));

    const std::span<const std::uint8_t> payloadBytes{blob.data() + sizeof(std::uint32_t), sizeof(Calibration)};
    const std::uint32_t storedCrc =
        static_cast<std::uint32_t>(blob.at(0U)) | static_cast<std::uint32_t>(blob.at(1U)) << 8U |
        static_cast<std::uint32_t>(blob.at(2U)) << 16U | static_cast<std::uint32_t>(blob.at(3U)) << 24U;

    EXPECT_EQ(storedCrc, LegacyCalcCrc(payloadBytes));
}

TEST(SettingsRecordTest, ComputesTheCheckAgainstTheCatalogueVector)
{
    // CRC-32/ISO-HDLC of "123456789", the catalogue's check value — the same number
    // LegacyCalcCrc produces, which is what ties the two together.
    constexpr std::string_view CHECK_INPUT = "123456789";
    const std::span<const std::uint8_t> bytes{
        reinterpret_cast<const std::uint8_t*>(CHECK_INPUT.data()), CHECK_INPUT.size()};

    EXPECT_EQ(LegacyCalcCrc(bytes), 0xCBF43926U);
    EXPECT_EQ(integra::Crc32IsoHdlc(bytes), 0xCBF43926U);
}

TEST(SettingsRecordTest, TellsTwoPayloadsApartThroughTheSameId)
{
    FakeStorage storage;
    ASSERT_TRUE(WriteSettingsRecord(storage, RECORD_ID, Calibration{1U, 1.0F, 1U}));
    ASSERT_TRUE(WriteSettingsRecord(storage, RECORD_ID, Calibration{2U, 2.0F, 2U}));

    Calibration read{};
    ASSERT_TRUE(ReadSettingsRecord(storage, RECORD_ID, read));
    EXPECT_EQ(read, (Calibration{2U, 2.0F, 2U}));
}

TEST(SettingsRecordTest, ChecksumCoversThePayloadOnly)
{
    // Two payloads that differ in one field must not share a checksum — the loop
    // has to run over every byte, not just the first.
    FakeStorage first;
    FakeStorage second;
    ASSERT_TRUE(WriteSettingsRecord(first, RECORD_ID, Calibration{1U, 1.0F, 1U}));
    ASSERT_TRUE(WriteSettingsRecord(second, RECORD_ID, Calibration{1U, 1.0F, 2U}));

    EXPECT_NE(first.Blob(RECORD_ID).at(0U), second.Blob(RECORD_ID).at(0U));
}

} // namespace
