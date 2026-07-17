/*
 * Game Five — round-display bring-up test (GoldenMorning 2.10" 360x360, GC9B72)
 *
 * The round module is plugged into J3 of the fabricated v1 board in the
 * REVERSED orientation, so its pins land on these board nets / GPIOs:
 *
 *   round pin   board net   ESP32-S3 GPIO
 *   SCL         SPI_SCK     7
 *   SDA (MOSI)  LCD_DC      2
 *   CS          SPI_MOSI    9
 *   DC          LCD_RST     3
 *   RST         LCD_CS      1
 *   VDD/GND     3V3/GND     -
 *   LED-A/K     BL_A/BL_K   backlight PWM = GPIO 5 (Q1 low-side)
 *   TE          n.c.        -
 *   SDO         3V3 (!)     never issue read commands
 *
 * ESP32-S3 routes SPI through the GPIO matrix, so this is firmware-only.
 *
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"

static const char *TAG = "roundtest";

#define PIN_SCLK 7
#define PIN_MOSI 2
#define PIN_CS   9
#define PIN_DC   3
#define PIN_RST  1
#define PIN_BL   5

#define H_RES 360
#define V_RES 360
#define SPI_HZ (40 * 1000 * 1000)
#define CHUNK_LINES 40

static esp_lcd_panel_io_handle_t s_io;
static uint16_t *s_buf; /* CHUNK_LINES lines, RGB565 big-endian on the wire */

/* ---------------------------------------------------------------- init---- */

typedef struct {
    uint8_t cmd;
    uint8_t data[32];
    uint8_t len;    /* 0xFF = end of list */
    uint16_t delay_ms;
} lcd_cmd_t;

/* GC9B72 360x360 init — from xboot/xstar fb-gc9b72.c (verified against the
 * MaliosDark/Arduino_GC9B72 port). Extended registers need the FE/EF unlock
 * and are re-locked with FE/EE at the end. COLMOD 0x05 = RGB565, no INVON,
 * no x/y gap offsets. */
static const lcd_cmd_t GC9B72_INIT[] = {
    { 0xFE, {0}, 0, 0 },                       /* inter-register enable 1 */
    { 0xEF, {0}, 0, 0 },                       /* inter-register enable 2 */
    { 0x80, {0x19}, 1, 0 },
    { 0x82, {0x09}, 1, 0 },
    { 0x83, {0x03}, 1, 0 },
    { 0x88, {0x00}, 1, 0 },
    { 0x89, {0x38}, 1, 0 },
    { 0x8A, {0x40}, 1, 0 },
    { 0x8B, {0x0A}, 1, 0 },
    { 0x8C, {0x00}, 1, 0 },
    { 0x81, {0xFF}, 1, 0 },
    { 0x84, {0xFF}, 1, 0 },
    { 0x85, {0xFF}, 1, 0 },
    { 0x86, {0xFF}, 1, 0 },
    { 0x87, {0xFF}, 1, 0 },
    { 0x8E, {0xFF}, 1, 0 },
    { 0x8F, {0xFF}, 1, 0 },
    { 0x98, {0x3E}, 1, 0 },
    { 0x99, {0x3E}, 1, 0 },
    { 0x7D, {0x72}, 1, 0 },
    { 0x70, {0x02, 0x03, 0x03, 0x06, 0x03, 0x03, 0x09, 0x07, 0x09, 0x03}, 10, 0 },
    { 0x90, {0x06, 0x06, 0x01, 0x01}, 4, 0 },
    { 0x93, {0x02, 0xFF, 0x00}, 3, 0 },
    { 0xCB, {0x02}, 1, 0 },
    { 0xFB, {0x00, 0x00}, 2, 0 },
    { 0xF6, {0xC0}, 1, 0 },
    { 0x6C, {0x00, 0x00, 0x22, 0x00, 0xCC, 0x04, 0x58}, 7, 0 },
    { 0xAA, {0x0B, 0x00}, 2, 0 },
    { 0xEC, {0x07}, 1, 0 },
    { 0xF9, {0x40}, 1, 0 },
    { 0xEB, {0x01, 0x67}, 2, 0 },
    { 0x74, {0x01, 0x60, 0x00, 0x00, 0x00, 0x00}, 6, 0 },
    { 0xB5, {0x14, 0x14, 0x14}, 3, 0 },
    { 0x6E, {0x0B, 0x0B, 0x09, 0x09, 0x13, 0x13, 0x11, 0x11,
             0x16, 0x15, 0x01, 0x04, 0x00, 0x0D, 0x1D, 0x00,
             0x00, 0x1D, 0x0D, 0x00, 0x04, 0x08, 0x15, 0x16,
             0x12, 0x12, 0x14, 0x14, 0x0A, 0x0A, 0x0C, 0x0C}, 32, 0 }, /* CGOUT */
    { 0x60, {0x38, 0x1C, 0x13, 0x56}, 4, 0 },
    { 0x61, {0xF8, 0x0A, 0x13, 0x56}, 4, 0 },
    { 0x62, {0xF8, 0x0B, 0x13, 0x56}, 4, 0 },
    { 0x63, {0x38, 0x1C, 0x13, 0x56}, 4, 0 },
    { 0x64, {0x38, 0x20, 0x72, 0xF8, 0x13, 0x56}, 6, 0 },
    { 0x65, {0x78, 0x1A, 0x70, 0x0B, 0x56, 0x13}, 6, 0 },
    { 0x66, {0x38, 0x24, 0x72, 0xFC, 0x13, 0x56}, 6, 0 },
    { 0x68, {0xB3, 0x08, 0x0E, 0x08, 0x0E, 0x0A, 0x0A}, 7, 0 },
    { 0x69, {0xB3, 0x08, 0x0E, 0x08, 0x0E, 0x0A, 0x0A}, 7, 0 },
    { 0x6A, {0x00, 0x00}, 2, 0 },
    { 0x3A, {0x05}, 1, 0 },                    /* COLMOD: RGB565 */
    { 0x36, {0x00}, 1, 0 },                    /* MADCTL: rot0, RGB order */
    { 0x7C, {0xB6, 0x29}, 2, 0 },
    { 0xAC, {0x40}, 1, 0 },
    { 0xC3, {0x1A}, 1, 0 },                    /* VREG voltages */
    { 0xC4, {0x24}, 1, 0 },
    { 0xC9, {0x2F}, 1, 0 },
    { 0xF0, {0x11, 0x17, 0x08, 0x06, 0x05, 0x38}, 6, 0 },  /* gamma 1 */
    { 0xF1, {0x4D, 0x72, 0x72, 0x2D, 0x34, 0x8F}, 6, 0 },  /* gamma 2 */
    { 0xF2, {0x11, 0x17, 0x08, 0x06, 0x05, 0x38}, 6, 0 },  /* gamma 3 */
    { 0xF3, {0x4D, 0x72, 0x72, 0x2D, 0x34, 0x8F}, 6, 0 },  /* gamma 4 */
    { 0xB4, {0x0A}, 1, 0 },
    { 0x35, {0x00}, 1, 0 },                    /* TE on, V-blank (pad floats) */
    { 0xFE, {0}, 0, 0 },                       /* re-lock */
    { 0xEE, {0}, 0, 0 },
    { 0x11, {0}, 0, 120 },                     /* SLPOUT */
    { 0x29, {0}, 0, 20 },                      /* DISPON */
    { 0x00, {0}, 0xFF, 0 },
};

static void lcd_cmd(uint8_t cmd, const uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, cmd, data, len));
}

static void lcd_run_init(const lcd_cmd_t *seq)
{
    for (; seq->len != 0xFF; seq++) {
        lcd_cmd(seq->cmd, seq->len ? seq->data : NULL, seq->len);
        if (seq->delay_ms)
            vTaskDelay(pdMS_TO_TICKS(seq->delay_ms));
    }
}

static void lcd_set_window(int x0, int y0, int x1, int y1)
{
    uint8_t ca[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t ra[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    lcd_cmd(0x2A, ca, 4);
    lcd_cmd(0x2B, ra, 4);
}

/* stream CHUNK_LINES-tall stripes; fill_fn paints one stripe into s_buf */
typedef void (*stripe_fn)(int y0, int lines);

static void lcd_draw(stripe_fn fn)
{
    for (int y = 0; y < V_RES; y += CHUNK_LINES) {
        int lines = (y + CHUNK_LINES > V_RES) ? V_RES - y : CHUNK_LINES;
        fn(y, lines);
        lcd_set_window(0, y, H_RES - 1, y + lines - 1);
        ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x2C, s_buf,
                                                  (size_t)H_RES * lines * 2));
    }
}

/* ---------------------------------------------------------------- patterns */

static inline uint16_t swap16(uint16_t v) { return (v >> 8) | (v << 8); }
static inline uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return swap16(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static uint16_t s_fill;
static void st_solid(int y0, int lines)
{
    for (int i = 0; i < H_RES * lines; i++) s_buf[i] = s_fill;
}

static void st_bars(int y0, int lines)
{
    static const uint8_t bar[8][3] = {
        {255,255,255},{255,255,0},{0,255,255},{0,255,0},
        {255,0,255},{255,0,0},{0,0,255},{0,0,0} };
    for (int x = 0; x < H_RES; x++) {
        int i = x * 8 / H_RES;
        uint16_t c = rgb(bar[i][0], bar[i][1], bar[i][2]);
        for (int l = 0; l < lines; l++) s_buf[l * H_RES + x] = c;
    }
}

static void st_rings(int y0, int lines)
{
    for (int l = 0; l < lines; l++) {
        int y = y0 + l;
        for (int x = 0; x < H_RES; x++) {
            float dx = x - H_RES / 2.0f, dy = y - V_RES / 2.0f;
            int d = (int)sqrtf(dx * dx + dy * dy);
            uint16_t c;
            if (d > 178)      c = rgb(255, 0, 0);      /* rim ring: edge check */
            else if (d > 174) c = rgb(255, 255, 255);
            else              c = ((d / 20) & 1) ? rgb(0, 90, 200) : rgb(0, 0, 0);
            if (d < 4) c = rgb(255, 255, 0);           /* center dot */
            s_buf[l * H_RES + x] = c;
        }
    }
}

static void st_gradient(int y0, int lines)
{
    for (int l = 0; l < lines; l++) {
        int v = (y0 + l) * 255 / (V_RES - 1);
        for (int x = 0; x < H_RES; x++)
            s_buf[l * H_RES + x] = rgb(v, v, v);
    }
}

/* ---------------------------------------------------------------- main --- */

void app_main(void)
{
    ESP_LOGI(TAG, "round display test: GC9B72 360x360 on remapped pins");
    ESP_LOGI(TAG, "SCLK=%d MOSI=%d CS=%d DC=%d RST=%d BL=%d",
             PIN_SCLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST, PIN_BL);

    /* backlight full on (Q1 low-side gate) */
    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_BL,
                         .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(PIN_BL, 1);

    /* hardware reset */
    gpio_config_t rst = { .pin_bit_mask = 1ULL << PIN_RST,
                          .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(130));

    /* SPI bus + panel IO */
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = H_RES * CHUNK_LINES * 2,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &s_io));

    s_buf = heap_caps_malloc(H_RES * CHUNK_LINES * 2,
                             MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(s_buf);

    ESP_LOGI(TAG, "sending GC9B72 init");
    lcd_run_init(GC9B72_INIT);

    const struct { const char *name; uint8_t r, g, b; } fills[] = {
        { "RED", 255, 0, 0 }, { "GREEN", 0, 255, 0 }, { "BLUE", 0, 0, 255 },
        { "WHITE", 255, 255, 255 }, { "BLACK", 0, 0, 0 },
    };

    for (int round = 0;; round++) {
        for (int i = 0; i < 5; i++) {
            ESP_LOGI(TAG, "fill: %s", fills[i].name);
            s_fill = rgb(fills[i].r, fills[i].g, fills[i].b);
            lcd_draw(st_solid);
            vTaskDelay(pdMS_TO_TICKS(900));
        }
        ESP_LOGI(TAG, "color bars");
        lcd_draw(st_bars);
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP_LOGI(TAG, "grayscale gradient");
        lcd_draw(st_gradient);
        vTaskDelay(pdMS_TO_TICKS(1500));
        ESP_LOGI(TAG, "rings + rim edge check");
        lcd_draw(st_rings);
        vTaskDelay(pdMS_TO_TICKS(2500));
        ESP_LOGI(TAG, "=== round %d done ===", round);
    }
}
