#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <hwlib/algorithms/crc.hpp>
#include <span>
#include <type_traits>

namespace hwlib::persistence
{

/// The key-value storage a record is kept in: an identifier addresses one blob of
/// a fixed size. On a device that is a flash-backed store (Zephyr's NVS, an EEPROM
/// driver); in a test it is a map. Both calls report failure rather than throwing,
/// because a missing key is an ordinary outcome on first boot.
template<typename Storage>
concept RecordStorageLike =
    requires(Storage& storage, std::uint16_t id, std::span<const std::uint8_t> in, std::span<std::uint8_t> out) {
        {
            storage.Write(id, in)
        } -> std::same_as<bool>;
        {
            storage.Read(id, out)
        } -> std::same_as<bool>;
    };

/// What actually lands in storage: the payload and a CRC-32 over it. The checksum
/// comes first and covers the payload only, so the record can be verified without
/// knowing how the payload is laid out.
///
/// Storage is not the same as a link: a half-written record after a power cut, or a
/// flash cell that has degraded, reads back as a plausible value of the right size.
/// The checksum is what tells that apart from a setting the device actually saved.
template<typename Payload>
struct SettingsRecord
{
    std::uint32_t crc{};
    Payload payload{};
};

namespace detail
{

/// A byte view of an object, without reinterpret_cast: the copy is what makes it
/// defined, and the compiler elides it.
template<typename T>
[[nodiscard]] constexpr std::array<std::uint8_t, sizeof(T)> ObjectBytes(const T& object) noexcept
{
    return std::bit_cast<std::array<std::uint8_t, sizeof(T)>>(object);
}

} // namespace detail

/// Writes the payload with its checksum. Returns whatever the storage reports.
template<RecordStorageLike Storage, typename Payload>
[[nodiscard]] bool WriteSettingsRecord(Storage& storage, std::uint16_t id, const Payload& payload)
{
    // The record reaches storage as raw bytes, padding included, so a payload that
    // is not trivially copyable would persist something that means nothing when it
    // is read back — a pointer, or the state of a std::function. The original of
    // this component let that compile.
    static_assert(std::is_trivially_copyable_v<Payload>,
                  "a settings payload is stored as raw bytes and must be trivially copyable");

    SettingsRecord<Payload> record{};
    record.payload = payload;
    // Value-initialised above, so the payload's own padding is zero rather than
    // whatever the stack held — otherwise the same setting could be written twice
    // with two different checksums.
    record.crc = hwlib::algorithms::Crc32IsoHdlc(detail::ObjectBytes(record.payload));

    const auto bytes = detail::ObjectBytes(record);
    return storage.Write(id, bytes);
}

/// Reads the payload back and verifies it. Returns false — leaving `payload`
/// untouched — if the storage has no such record or the checksum does not match,
/// which is the caller's cue to fall back to a default.
template<RecordStorageLike Storage, typename Payload>
[[nodiscard]] bool ReadSettingsRecord(Storage& storage, std::uint16_t id, Payload& payload)
{
    static_assert(std::is_trivially_copyable_v<Payload>,
                  "a settings payload is stored as raw bytes and must be trivially copyable");

    std::array<std::uint8_t, sizeof(SettingsRecord<Payload>)> bytes{};
    if (!storage.Read(id, bytes))
    {
        return false;
    }

    const auto record = std::bit_cast<SettingsRecord<Payload>>(bytes);
    if (record.crc != hwlib::algorithms::Crc32IsoHdlc(detail::ObjectBytes(record.payload)))
    {
        return false;
    }

    payload = record.payload;
    return true;
}

} // namespace hwlib::persistence
