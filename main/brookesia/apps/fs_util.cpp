#include "brookesia/apps/fs_util.hpp"

#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_err.h"

namespace brookesia {
namespace {

constexpr char kTag[] = "fs_util";
bool s_mounted = false;

} // namespace

bool ensureStorageMounted()
{
    if (s_mounted) {
        return true;
    }

    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = kStoragePath;
    conf.partition_label = "storage";
    conf.partition = nullptr;
    conf.format_if_mount_failed = true;
    conf.dont_mount = false;
    conf.grow_on_mount = false;

    const esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "mount failed: %s", esp_err_to_name(err));
        return false;
    }

    s_mounted = true;
    return true;
}

} // namespace brookesia
