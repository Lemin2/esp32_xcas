#include "brookesia/core/ui_fonts.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "dirent.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "brookesia/apps/fs_util.hpp"

#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
#include "src/libs/tiny_ttf/lv_tiny_ttf.h"
#endif

LV_FONT_DECLARE(lv_font_noto_math_14)
LV_FONT_DECLARE(lv_font_noto_math_18)
LV_FONT_DECLARE(lv_font_noto_math_22)
LV_FONT_DECLARE(lv_font_symbols_14)
LV_FONT_DECLARE(lv_font_symbols_18)
LV_FONT_DECLARE(lv_font_symbols_22)
LV_FONT_DECLARE(lv_font_montserrat_22)

namespace brookesia::ui_fonts
{
namespace
{

constexpr char kTag[] = "ui_fonts";
constexpr int kMaxTtfFonts = 4;
constexpr size_t kTtfCacheGlyphs = 96;

struct TtfSource {
    std::string lv_path;
};

struct SizedFont {
    int size = 0;
    lv_font_t *font = nullptr;
};

std::vector<TtfSource> s_sources;
std::vector<SizedFont> s_sized_fonts;
int s_default_ttf_size = 22;
lv_font_t s_font_symbols14;
lv_font_t s_font_symbols18;
lv_font_t s_font_symbols22;
lv_font_t s_builtin_text14;
lv_font_t s_builtin_text16;
lv_font_t s_builtin_text22;
lv_font_t s_text14;
lv_font_t s_text16;
lv_font_t s_text22;
bool s_initialized = false;

bool hasFontExtension(const char *name)
{
    if (name == nullptr) {
        return false;
    }
    const char *dot = std::strrchr(name, '.');
    if (dot == nullptr) {
        return false;
    }
    char ext[5] = {};
    for (int i = 0; i < 4 && dot[i] != '\0'; ++i) {
        ext[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(dot[i])));
    }
    return std::strcmp(ext, ".ttf") == 0 || std::strcmp(ext, ".otf") == 0;
}

bool directoryExists(const char *path)
{
    struct stat st = {};
    return path != nullptr && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

std::string trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

void addFontSource(const std::string &native_path)
{
    if (native_path.empty() || static_cast<int>(s_sources.size()) >= kMaxTtfFonts) {
        return;
    }
    struct stat st = {};
    if (stat(native_path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        ESP_LOGW(kTag, "font not found: %s", native_path.c_str());
        return;
    }
    s_sources.push_back({std::string("A:") + native_path});
    ESP_LOGI(kTag, "registered font %s", native_path.c_str());
}

bool loadFontConfig(const char *native_dir)
{
    if (native_dir == nullptr || !directoryExists(native_dir)) {
        return false;
    }

    const std::string cfg_path = std::string(native_dir) + "/fonts.cfg";
    FILE *file = std::fopen(cfg_path.c_str(), "r");
    if (file == nullptr) {
        return false;
    }

    bool loaded_any = false;
    char line[256];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        std::string entry = trim(line);
        if (entry.empty() || entry[0] == '#') {
            continue;
        }

        constexpr char kSizePrefix[] = "size=";
        constexpr char kFontPrefix[] = "font=";
        if (entry.rfind(kSizePrefix, 0) == 0) {
            const int size = std::atoi(entry.c_str() + sizeof(kSizePrefix) - 1);
            if (size >= 10 && size <= 48) {
                s_default_ttf_size = size;
                ESP_LOGI(kTag, "default TTF size %d", s_default_ttf_size);
            }
            continue;
        }
        if (entry.rfind(kFontPrefix, 0) == 0) {
            std::string value = trim(entry.substr(sizeof(kFontPrefix) - 1));
            if (!value.empty() && value.front() != '/') {
                value = std::string(native_dir) + "/" + value;
            }
            addFontSource(value);
            loaded_any = true;
        }
    }

    std::fclose(file);
    return loaded_any;
}

void scanFontDir(const char *native_dir)
{
    if (native_dir == nullptr || !directoryExists(native_dir) || static_cast<int>(s_sources.size()) >= kMaxTtfFonts) {
        return;
    }

    DIR *dir = opendir(native_dir);
    if (dir == nullptr) {
        return;
    }

    std::vector<std::string> names;
    while (dirent *entry = readdir(dir)) {
        if (hasFontExtension(entry->d_name)) {
            names.emplace_back(entry->d_name);
        }
    }
    closedir(dir);
    std::sort(names.begin(), names.end());

    for (const std::string &name : names) {
        if (static_cast<int>(s_sources.size()) >= kMaxTtfFonts) {
            break;
        }
        addFontSource(std::string(native_dir) + "/" + name);
    }
}

lv_font_t *createTtfChain(int size)
{
#if LV_USE_TINY_TTF && LV_TINY_TTF_FILE_SUPPORT
    lv_font_t *head = nullptr;
    lv_font_t *tail = nullptr;
    for (const TtfSource &source : s_sources) {
        lv_font_t *font = lv_tiny_ttf_create_file_ex(source.lv_path.c_str(), size, LV_FONT_KERNING_NORMAL, kTtfCacheGlyphs);
        if (font == nullptr) {
            ESP_LOGW(kTag, "failed to load %s", source.lv_path.c_str());
            continue;
        }
        if (head == nullptr) {
            head = font;
        } else {
            tail->fallback = font;
        }
        tail = font;
    }
    if (tail != nullptr) {
        tail->fallback = (size <= 14) ? &s_builtin_text14 : (size <= 18) ? &s_builtin_text16 : &s_builtin_text22;
    }
    return head;
#else
    (void)size;
    return nullptr;
#endif
}

void initBuiltins()
{
    s_font_symbols14 = lv_font_symbols_14;
    s_font_symbols14.fallback = &lv_font_noto_math_14;

    s_font_symbols18 = lv_font_symbols_18;
    s_font_symbols18.fallback = &lv_font_noto_math_18;

    s_font_symbols22 = lv_font_symbols_22;
    s_font_symbols22.fallback = &lv_font_noto_math_22;

    s_builtin_text14 = lv_font_source_han_sans_sc_14_cjk;
    s_builtin_text14.fallback = &s_font_symbols14;

    s_builtin_text16 = lv_font_source_han_sans_sc_16_cjk;
    s_builtin_text16.fallback = &s_font_symbols18;

    s_builtin_text22 = lv_font_montserrat_22;
    s_builtin_text22.fallback = &s_font_symbols22;
}

} // namespace

void init()
{
    if (s_initialized) {
        return;
    }
    s_initialized = true;

    initBuiltins();
    (void)ensureStorageMounted();
    bool configured = loadFontConfig("/data/fonts");
    configured = loadFontConfig("/sdcard/fonts") || configured;
    if (!configured) {
        scanFontDir("/data/fonts");
        scanFontDir("/sdcard/fonts");
    }

    lv_font_t *ttf14 = createTtfChain(14);
    lv_font_t *ttf16 = createTtfChain(16);
    lv_font_t *ttf22 = createTtfChain(s_default_ttf_size);
    s_text14 = (ttf14 != nullptr) ? *ttf14 : s_builtin_text14;
    s_text16 = (ttf16 != nullptr) ? *ttf16 : s_builtin_text16;
    s_text22 = (ttf22 != nullptr) ? *ttf22 : s_builtin_text22;
}

const lv_font_t *textFont14()
{
    init();
#if defined(CONFIG_XCAS_BOARD_TAB5) && CONFIG_XCAS_BOARD_TAB5
    return textFont(s_default_ttf_size);
#else
    return &s_text14;
#endif
}

const lv_font_t *textFont16()
{
    init();
#if defined(CONFIG_XCAS_BOARD_TAB5) && CONFIG_XCAS_BOARD_TAB5
    return textFont(s_default_ttf_size);
#endif
    return &s_text16;
}

const lv_font_t *textFont(int size)
{
    init();
    if (size <= 14) {
        return textFont14();
    }
    if (size <= 16) {
        return textFont16();
    }
    if (size <= 22 && s_sources.empty()) {
        return &s_text22;
    }

    for (const SizedFont &entry : s_sized_fonts) {
        if (entry.size == size && entry.font != nullptr) {
            return entry.font;
        }
    }

    lv_font_t *font = createTtfChain(size);
    if (font == nullptr) {
        return textFont16();
    }
    s_sized_fonts.push_back({size, font});
    return font;
}

} // namespace brookesia::ui_fonts
