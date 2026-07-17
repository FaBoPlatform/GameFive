idf_component_register(SRCS app_main.c i_main.c i_system.c i_video.c i_sound.c
                            round_lcd.c
                       PRIV_REQUIRES prboom oplmusic gamefive esp_partition
                                     esp_timer esp_lcd esp_driver_gpio
                                     esp_driver_spi esp_driver_i2s app_update)

# dboolean=int: mus2mid.h (prboom-plus lineage) uses it in its prototype;
# our prboom 2.5.0 doomtype.h predates the name.
target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H dboolean=int)

# Same C standard as the core: the prboom headers we include define
# `typedef enum {false, true} boolean` which C23 rejects.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu17
    -Wno-pointer-sign
    -Wno-error=missing-field-initializers -Wno-error=implicit-fallthrough
    -Wno-error=type-limits)
