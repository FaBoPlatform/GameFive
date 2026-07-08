idf_component_register(SRCS "src/lcd.c" "src/keys.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       PRIV_REQUIRES esp_lcd esp_driver_spi esp_driver_gpio esp_driver_ledc esp_rom)
