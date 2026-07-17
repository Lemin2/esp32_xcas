#pragma once

namespace brookesia {

// Path where the on-flash LittleFS storage partition is mounted.
inline constexpr char kStoragePath[] = "/data";

// Mounts the LittleFS "storage" partition once (idempotent).
// Returns true if the filesystem is mounted and usable.
bool ensureStorageMounted();

} // namespace brookesia
