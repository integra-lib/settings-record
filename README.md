# settings-record

A checksummed settings record: write a payload to a key-value store, read it back only if it verifies.

Part of [integra-lib](https://github.com/integra-lib) — architecture-independent C++20
components shared between firmware projects. Header-only,
no exceptions, no RTTI.

## Use it

```bash
git submodule add git@github.com:integra-lib/settings-record.git external/integra/settings-record
```

```cmake
add_subdirectory(external/integra/settings-record)
target_link_libraries(app PRIVATE Integra::settings_record)
```

```cpp
#include <integra/settings_record.hpp>
```

Each component carries its own include directory, so this header stays unreachable
until the component is linked: a forgotten dependency is a compile error rather than
a build that happens to work.

## Dependencies

One other component, added **next to** this one rather than inside it, so that a
consumer never ends up with two copies of the same component:

* `Integra::crc` >= 0.1.0, < 0.2.0

```bash
git submodule add git@github.com:integra-lib/crc.git external/integra/crc
```

```cmake
add_subdirectory(external/integra/crc)
add_subdirectory(external/integra/settings-record)
```

A missing or out-of-range dependency stops the CMake configure with a message naming
the component and the version found.

## The storage is yours

The component owns the record format, not the medium. A storage satisfies
`integra::RecordStorageLike`:

```cpp
bool Write(std::uint16_t id, std::span<const std::uint8_t> data);
bool Read(std::uint16_t id, std::span<std::uint8_t> data);
```

On a device that is a flash-backed store — Zephyr's NVS, an EEPROM driver — and the
adapter stays in the project, because a partition name and a flash device are the
project's, not the library's. In a test it is a map.

```cpp
struct Calibration
{
    std::uint32_t offset{};
    float scale{};
};

constexpr std::uint16_t CALIBRATION_ID = 7U;

if (!integra::WriteSettingsRecord(storage, CALIBRATION_ID, calibration))
{
    LOG_ERR("calibration not saved");
}

Calibration calibration{DEFAULT_CALIBRATION};
// Left untouched when the record is missing or does not verify, so the default
// survives a first boot and a corrupted cell alike.
std::ignore = integra::ReadSettingsRecord(storage, CALIBRATION_ID, calibration);
```

## What the checksum is for

Storage is not a link. A half-written record after a power cut, or a flash cell that
has degraded, reads back as a plausible value of the right size — nothing about the
read fails. The CRC-32 stored in front of the payload is what tells that apart from a
setting the device actually saved, and a failed check is reported as `false` rather
than as an exception, because falling back to a default is ordinary behaviour, not an
error.

The payload is stored as raw bytes, so it must be trivially copyable; that is a
`static_assert`, not a comment.

## Versioning

Every component is released on its own, tagged `vX.Y.Z`. Pre-1.0, a minor release may
break the API, which is why dependants accept a single minor.

```bash
git -C external/integra/settings-record fetch --tags
git -C external/integra/settings-record checkout v0.2.0
git add external/integra/settings-record && git commit -m "build: bump settings-record to v0.2.0"
```

## Coming from a174-hardware's settings-storage

The record format is unchanged — a `std::uint32_t` checksum, then the payload, the
checksum covering the payload only — so records already on a device still read back.
Two things did change:

* `CalcCrc` is gone; the checksum comes from `Integra::crc`. It is the same
  CRC-32/ISO-HDLC, and a test pins it against both the catalogue's check value and
  the original's hand-written loop.
* the storage contract takes `std::span` instead of `const void*` plus a length, so
  an existing `NvsStorage` needs its two signatures widened:

```cpp
// before
bool Write(std::uint16_t id, const void* data, std::size_t len);
bool Read(std::uint16_t id, void* data, std::size_t len);
// after
bool Write(std::uint16_t id, std::span<const std::uint8_t> data);
bool Read(std::uint16_t id, std::span<std::uint8_t> data);
```

The bodies keep calling `nvs_write`/`nvs_read` with `data.data()` and `data.size()`.

## In a consumer's CI

The component is an ordinary submodule, so the build needs it checked out. On GitLab
that means `GIT_SUBMODULE_STRATEGY: normal` (or `recursive`) on every job that builds —
not only on the ones that run unit tests.

## Develop it

```bash
git submodule update --init          # ci-shared, needed by pre-commit
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

A standalone build fetches `crc` itself, at the version this component was verified
against; point `-DINTEGRA_REMOTE=` elsewhere to build against a different remote.
Tests are built only when this repository is the top-level project, so a consumer
never builds them and never fetches GoogleTest.

The style configs are symlinks into the `ci-shared` submodule, and the pipeline comes
from the same place. On GitHub the workflow does not run at all: a workflow token
cannot read the private `crc` repository, so a standalone build there cannot be made
green honestly. The shared setup is what GitLab will use.
