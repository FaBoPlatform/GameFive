/*
 * Game Five — ST7789 display helper (esp_lcd based)
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_check.h"

#include "gamefive_pins.h"
#include "lcd.h"
#include "font8x8_basic.h"

static const char *TAG = "gf_lcd";

/* One DMA-capable line-chunk buffer: 240 x 40 px x 2 B = 19.2 KB */
#define CHUNK_LINES 40
static uint16_t *s_chunk;
static esp_lcd_panel_handle_t s_panel;
static SemaphoreHandle_t s_flush_done;

/*
 * esp_lcd's SPI tx_color QUEUES the pixel payload and returns before the DMA
 * transfer finishes — reusing the source buffer right away would corrupt the
 * pixels still on the wire. The IO layer fires this callback once per
 * draw_bitmap (on its final chunk); gf_lcd_blit blocks on it so every draw
 * is fully synchronous and buffers are always safe to rewrite.
 */
static bool flush_done_cb(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

#define BL_LEDC_TIMER   LEDC_TIMER_0
#define BL_LEDC_CHANNEL LEDC_CHANNEL_0
#define BL_LEDC_RES     LEDC_TIMER_10_BIT
#define BL_LEDC_FREQ_HZ 20000

esp_err_t gf_lcd_init(void)
{
    /* --- shared SPI bus (LCD + 74HC165) --- */
    spi_bus_config_t bus_cfg = {
        .sclk_io_num = GF_PIN_SPI_SCK,
        .mosi_io_num = GF_PIN_SPI_MOSI,
        .miso_io_num = GF_PIN_SPI_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = GF_LCD_H_RES * CHUNK_LINES * 2 + 16,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(GF_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO),
                        TAG, "spi bus init");

    /* --- panel IO --- */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = GF_PIN_LCD_DC,
        .cs_gpio_num = GF_PIN_LCD_CS,
        .pclk_hz = GF_LCD_PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = flush_done_cb,
    };
    s_flush_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_flush_done, ESP_ERR_NO_MEM, TAG, "sem alloc");
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)GF_SPI_HOST,
                                                 &io_cfg, &io),
                        TAG, "panel io");

    /* --- ST7789 panel --- */
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GF_PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(io, &panel_cfg, &s_panel),
                        TAG, "st7789 new");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, GF_LCD_INVERT), TAG, "inv");
#if GF_LCD_ROTATE_180
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, true, true), TAG, "mirror");
#endif
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp on");

    s_chunk = heap_caps_malloc(GF_LCD_H_RES * CHUNK_LINES * 2, MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(s_chunk, ESP_ERR_NO_MEM, TAG, "chunk alloc");

    /* --- backlight PWM (off until gf_lcd_backlight) --- */
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_LEDC_RES,
        .timer_num = BL_LEDC_TIMER,
        .freq_hz = BL_LEDC_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&tcfg), TAG, "ledc timer");
    ledc_channel_config_t ccfg = {
        .gpio_num = GF_PIN_LCD_BL,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = BL_LEDC_CHANNEL,
        .timer_sel = BL_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ccfg), TAG, "ledc chan");

    ESP_LOGI(TAG, "ST7789 %dx%d ready (rotate180=%d invert=%d)",
             GF_LCD_H_RES, GF_LCD_V_RES, GF_LCD_ROTATE_180, GF_LCD_INVERT);
    return ESP_OK;
}

void *gf_lcd_panel(void)
{
    return s_panel;
}

void gf_lcd_backlight(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    uint32_t max_duty = (1u << BL_LEDC_RES) - 1;
    uint32_t duty = (max_duty * (uint32_t)percent) / 100u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_LEDC_CHANNEL);
}

void gf_lcd_blit(int x, int y, int w, int h, const uint16_t *pix)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(s_panel, x, y, x + w, y + h, pix));
    xSemaphoreTake(s_flush_done, portMAX_DELAY); /* wait for the DMA flush */
}

void gf_lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GF_LCD_H_RES) w = GF_LCD_H_RES - x;
    if (y + h > GF_LCD_V_RES) h = GF_LCD_V_RES - y;
    if (w <= 0 || h <= 0) return;

    int max_px = GF_LCD_H_RES * CHUNK_LINES;
    int fill_px = (w * h > max_px) ? max_px : w * h;
    for (int i = 0; i < fill_px; i++) s_chunk[i] = color;

    int lines_per_pass = max_px / w;
    for (int row = 0; row < h; row += lines_per_pass) {
        int n = (h - row > lines_per_pass) ? lines_per_pass : (h - row);
        gf_lcd_blit(x, y + row, w, n, s_chunk);
    }
}

void gf_lcd_clear(uint16_t color)
{
    gf_lcd_fill_rect(0, 0, GF_LCD_H_RES, GF_LCD_V_RES, color);
}

void gf_lcd_rect(int x, int y, int w, int h, int t, uint16_t color)
{
    gf_lcd_fill_rect(x, y, w, t, color);
    gf_lcd_fill_rect(x, y + h - t, w, t, color);
    gf_lcd_fill_rect(x, y, t, h, color);
    gf_lcd_fill_rect(x + w - t, y, t, h, color);
}

void gf_lcd_text(int x, int y, const char *s, int scale,
                 uint16_t fg, uint16_t bg)
{
    if (scale < 1) scale = 1;
    int cell = 8 * scale;
    /* glyph cell buffer rendered one character at a time (max scale 4) */
    static uint16_t glyph[32 * 32];
    if (cell > 32) return;

    for (; *s != '\0'; s++, x += cell) {
        if (x + cell <= 0 || x >= GF_LCD_H_RES) continue;
        unsigned char c = (unsigned char)*s;
        if (c > 127) c = '?';
        const char *rows = font8x8_basic[c];
        for (int gy = 0; gy < 8; gy++) {
            uint8_t bits = (uint8_t)rows[gy];
            for (int gx = 0; gx < 8; gx++) {
                uint16_t col = (bits & (1u << gx)) ? fg : bg;
                for (int sy = 0; sy < scale; sy++) {
                    int py = (gy * scale + sy) * cell;
                    for (int sx = 0; sx < scale; sx++) {
                        glyph[py + gx * scale + sx] = col;
                    }
                }
            }
        }
        gf_lcd_blit(x, y, cell, cell, glyph);
    }
}
