/*
 * Game Five DOOM — entry point
 * PrBoom (esp32-doom lineage) on XIAO ESP32-S3 + HS20HS072RX (ST7789).
 *
 * Display orientation is set by DOOM_PORTRAIT in doom_config.h. The WAD
 * lives in the 'assets' partition (type 0x42) — installed by the launcher's
 * game store, or manually:
 *   esptool --chip esp32s3 -p /dev/cu.usbmodem101 write-flash 0x390000 wad/doom1-gamefive.wad
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_lcd_panel_ops.h"
#include "driver/gpio.h"
#include "lcd.h"
#include "keys.h"

/* from i_main.c — not via i_system.h: prboom's doomtype.h (enum boolean)
 * clashes with stdbool.h pulled in by FreeRTOS headers */
extern int doom_main(int argc, char const *const *argv);

#include "doom_config.h"

static const char *TAG = "doom";

static void doom_task(void *arg)
{
    static const char *const doom_argv[] = { "doom", "-cout", "ICWEFDA", NULL };
    doom_main(3, doom_argv);
    ESP_LOGE(TAG, "doom_main returned");
    vTaskDelete(NULL);
}

void app_main(void)
{
    ESP_LOGI(TAG, "Game Five DOOM starting");

    const esp_partition_t *wad =
        esp_partition_find_first((esp_partition_type_t)0x42,
                                 (esp_partition_subtype_t)0x06, NULL);
    if (wad == NULL) {
        ESP_LOGE(TAG, "wad partition not found — flash the WAD to 0x200000");
        return;
    }
    ESP_LOGI(TAG, "wad partition: offset 0x%" PRIx32 " size %" PRIu32,
             wad->address, wad->size);

    ESP_ERROR_CHECK(gf_lcd_init());
    gf_lcd_clear(GF_BLACK); /* clear in portrait before any rotation */

    esp_lcd_panel_handle_t panel = (esp_lcd_panel_handle_t)gf_lcd_panel();
#if DOOM_PORTRAIT && DOOM_PORTRAIT_FLIP
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, true));
#endif
#if !DOOM_PORTRAIT
    /* landscape: full-screen 320x240, hold the console sideways */
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
#if DOOM_ROTATE_CW
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
#else
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, false, true));
#endif
#endif

#if DOOM_LCD_INVERT
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
#endif

    ESP_ERROR_CHECK(gf_keys_init());
    /* diagnostic: weak pulldown on MISO. If the 74HC165 chain is fitted it
     * actively drives the line (reads stay 0xff idle); if the XIAO is bare
     * the floating line is dragged low and raw reads 0x00. */
    gpio_pulldown_en(8);
    gf_lcd_backlight(100);

    /* prboom has deep BSP recursion — keep the big internal stack */
    xTaskCreatePinnedToCore(doom_task, "doom", 24576, NULL, 5, NULL, 0);
}
