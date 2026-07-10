idf_component_register(SRCS app_main.c i_main.c i_system.c i_video.c i_sound.c
                       PRIV_REQUIRES prboom gamefive esp_partition esp_timer
                                     esp_lcd esp_driver_gpio esp_driver_i2s
                                     app_update)

target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H)

# Same C standard as the core: the prboom headers we include define
# `typedef enum {false, true} boolean` which C23 rejects.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu17
    -Wno-pointer-sign
    -Wno-error=missing-field-initializers -Wno-error=implicit-fallthrough
    -Wno-error=type-limits)
