idf_component_register(
    SRCS "cardputer_bsp.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_uart esp_driver_gpio esp_timer lvgl)