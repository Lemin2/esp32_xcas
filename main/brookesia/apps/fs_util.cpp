#include "brookesia/apps/fs_util.hpp"

#include "esp_log.h"
#include "esp_vfs_fat.h"

namespace brookesia {
namespace {

constexpr char kTag[] = "fs_util";
bool s_mounted = false;
wl_handle_t s_wl_handle = WL_INVALID_HANDLE;

} // namespace

bool ensureStorageMounted()
{
    if (s_mounted) {
        return true;
    }

    esp_vfs_fat_mount_config_t mount_config = {};
    mount_config.max_files = 4;
    mount_config.format_if_mount_failed = true;
    mount_config.allocation_unit_size = 0;

    const esp_err_t err =
        esp_vfs_fat_spiflash_mount_rw_wl(kStoragePath, "storage", &mount_config, &s_wl_handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "mount failed: %s", esp_err_to_name(err));
        return false;
    }

    s_mounted = true;
    return true;
}

} // namespace brookesia
