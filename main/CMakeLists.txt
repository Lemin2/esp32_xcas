set(XCAS_GUI_SRCS
    "app_main.cpp"
    "xcas_service.cpp"
    "cardputer_bsp.cpp"
    "xcas_ui.cpp"
    "mathlayout/font/font_metrics_lvgl.cpp"
    "mathlayout/layout/layout_engine.cpp"
    "mathlayout/layout/layout_debug_dump.cpp"
    "mathlayout/paint/math_painter.cpp"
    "mathlayout/render/text_renderer.cpp"
    "fonts/lv_font_noto_math_14.c"
    "fonts/lv_font_symbols_14.c"
    "brookesia/core/kernel.cpp"
    "brookesia/core/app_settings.cpp"
    "brookesia/core/ui_theme.cpp"
    "brookesia/apps/calc_app.cpp"
    "brookesia/apps/graph_app.cpp"
    "brookesia/apps/files_app.cpp"
    "brookesia/apps/project_app.cpp"
    "brookesia/apps/settings_app.cpp"
    "brookesia/apps/fs_util.cpp"
    "brookesia/apps/placeholder_app.cpp")

set(XCAS_CONSOLE_SRCS
    "console_example_main.c"
    "console_settings.c"
    "cmd_xcas.cpp")

option(XCAS_ENABLE_CONSOLE_MAIN "Build console REPL entry for debugging" OFF)

set(XCAS_SRCS ${XCAS_GUI_SRCS})
set(XCAS_PRIV_REQUIRES
    giac
    lvgl
    esp_lcd
    esp_driver_spi
    esp_driver_uart
    esp_driver_gpio
    esp_driver_sdmmc
    esp_timer
    fatfs
    littlefs
    esp_event
    esp_netif
    esp_wifi
    bt)

if(XCAS_ENABLE_CONSOLE_MAIN)
    list(APPEND XCAS_SRCS ${XCAS_CONSOLE_SRCS})
    list(APPEND XCAS_PRIV_REQUIRES
         console
         esp_driver_uart
         fatfs
         esp_driver_usb_serial_jtag
         esp_usb_cdc_rom_console
         nvs_flash
         cmd_system
         cmd_wifi
         cmd_nvs)
endif()

idf_component_register(SRCS ${XCAS_SRCS}
                       PRIV_REQUIRES ${XCAS_PRIV_REQUIRES}
                       INCLUDE_DIRS ".")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
                           BF2GMP
                           HAVE_ALLOCA_H)

target_compile_options(${COMPONENT_LIB} PRIVATE
                       -std=gnu++17
                       -Wno-error)
