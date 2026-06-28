idf_component_register(SRCS "console_example_main.c" "console_settings.c" "cmd_xcas.cpp"
                       PRIV_REQUIRES console esp_driver_uart fatfs esp_driver_usb_serial_jtag
                                     esp_usb_cdc_rom_console nvs_flash cmd_system cmd_wifi cmd_nvs giac
                       INCLUDE_DIRS ".")

target_compile_definitions(${COMPONENT_LIB} PRIVATE
                           BF2GMP
                           HAVE_ALLOCA_H)

target_compile_options(${COMPONENT_LIB} PRIVATE
                       -std=gnu++17
                       -Wno-error)
