#pragma once

namespace brookesia {

// Path where the on-flash FAT storage partition is mounted.
inline constexpr char kStoragePath[] = "/data";

// Mounts the wear-levelled FAT "storage" partition once (idempotent).
// Returns true if the filesystem is mounted and usable.
bool ensureStorageMounted();

} // namespace brookesia
