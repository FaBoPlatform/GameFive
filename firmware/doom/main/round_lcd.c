/*
 * Game Five DOOM — GC9B72 2.1" round 360x360 driver (experimental)
 *
 * Round module in J3 (reversed orientation) — pins land on these nets:
 *
 *   round pin   board net   ESP32-S3 GPIO
 *   SCL         SPI_SCK     7   (shared with the 74HC165 keyboard)
 *   SDA (MOSI)  LCD_DC      2
 *   CS          SPI_MOSI    9
 *   DC          LCD_RST     3
 *   RST         LCD_CS      1
 *   LED-A/K     BL_A/BL_K   backlight PWM/GPIO 5 (Q1 low-side)
 *   SDO         3V3 (!)     write-only: never issue read commands
 *
 * Init sequence: xboot/xstar fb-gc9b72.c, verified against the
 * MaliosDark/Arduino_GC9B72 port. SPI mode 0, COLMOD 0x05 (RGB565),
 * no INVON, no window gap offsets.
 *
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"

#include "gamefive_pins.h"  /* GF_SPI_HOST */
#include "round_lcd.h"

static const char *TAG = "round_lcd";

#define PIN_SCLK 7
#define PIN_MOSI 2
#define PIN_CS   9
#define PIN_DC   3
#define PIN_RST  1
#define PIN_BL   5
#define PIN_MISO 8          /* 74HC165 QH — bus is shared with the keyboard */

#define SPI_HZ (40 * 1000 * 1000)
#define MAX_CHUNK_PX (ROUND_LCD_RES * 40)

static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_done;

typedef struct {
    uint8_t cmd;
    uint8_t data[32];
    uint8_t len;    /* 0xFF = end of list */
    uint16_t delay_ms;
} lcd_cmd_t;

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
             0x12, 0x12, 0x14, 0x14, 0x0A, 0x0A, 0x0C, 0x0C}, 32, 0 },
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
    { 0xC3, {0x1A}, 1, 0 },
    { 0xC4, {0x24}, 1, 0 },
    { 0xC9, {0x2F}, 1, 0 },
    { 0xF0, {0x11, 0x17, 0x08, 0x06, 0x05, 0x38}, 6, 0 },
    { 0xF1, {0x4D, 0x72, 0x72, 0x2D, 0x34, 0x8F}, 6, 0 },
    { 0xF2, {0x11, 0x17, 0x08, 0x06, 0x05, 0x38}, 6, 0 },
    { 0xF3, {0x4D, 0x72, 0x72, 0x2D, 0x34, 0x8F}, 6, 0 },
    { 0xB4, {0x0A}, 1, 0 },
    { 0x35, {0x00}, 1, 0 },
    { 0xFE, {0}, 0, 0 },                       /* re-lock */
    { 0xEE, {0}, 0, 0 },
    { 0x11, {0}, 0, 120 },                     /* SLPOUT */
    { 0x29, {0}, 0, 20 },                      /* DISPON */
    { 0x00, {0}, 0xFF, 0 },
};

static bool on_color_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *ev, void *ctx)
{
    BaseType_t hp = pdFALSE;
    xSemaphoreGiveFromISR(s_done, &hp);
    return hp == pdTRUE;
}

esp_err_t round_lcd_init(void)
{
    /* backlight on (Q1 low-side gate) */
    gpio_config_t bl = { .pin_bit_mask = 1ULL << PIN_BL,
                         .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&bl), TAG, "bl gpio");
    gpio_set_level(PIN_BL, 1);

    /* hardware reset */
    gpio_config_t rst = { .pin_bit_mask = 1ULL << PIN_RST,
                          .mode = GPIO_MODE_OUTPUT };
    ESP_RETURN_ON_ERROR(gpio_config(&rst), TAG, "rst gpio");
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(130));

    /* shared SPI bus: MISO stays on the 74HC165 QH line for gf_keys */
    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = PIN_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = MAX_CHUNK_PX * 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(GF_SPI_HOST, &bus, SPI_DMA_CH_AUTO),
                        TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = SPI_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 2,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(GF_SPI_HOST, &io_cfg, &s_io),
                        TAG, "panel io");

    s_done = xSemaphoreCreateBinary();
    esp_lcd_panel_io_callbacks_t cbs = { .on_color_trans_done = on_color_done };
    ESP_RETURN_ON_ERROR(esp_lcd_panel_io_register_event_callbacks(s_io, &cbs, NULL),
                        TAG, "cbs");

    for (const lcd_cmd_t *c = GC9B72_INIT; c->len != 0xFF; c++) {
        ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(s_io, c->cmd,
                                                      c->len ? c->data : NULL,
                                                      c->len),
                            TAG, "init cmd");
        if (c->delay_ms)
            vTaskDelay(pdMS_TO_TICKS(c->delay_ms));
    }

    round_lcd_clear(0x0000);
    ESP_LOGI(TAG, "GC9B72 360x360 ready (SCLK=%d MOSI=%d CS=%d DC=%d RST=%d)",
             PIN_SCLK, PIN_MOSI, PIN_CS, PIN_DC, PIN_RST);
    return ESP_OK;
}

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t ca[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t ra[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0x2A, ca, 4));
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(s_io, 0x2B, ra, 4));
}

void round_lcd_blit(int x, int y, int w, int h, const uint16_t *pix)
{
    set_window(x, y, x + w - 1, y + h - 1);
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(s_io, 0x2C, pix,
                                              (size_t)w * h * 2));
    xSemaphoreTake(s_done, portMAX_DELAY); /* caller reuses its buffer */
}

void round_lcd_clear(uint16_t color)
{
    const int lines = 40;
    uint16_t *buf = heap_caps_malloc(ROUND_LCD_RES * lines * 2,
                                     MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    assert(buf);
    for (int i = 0; i < ROUND_LCD_RES * lines; i++) buf[i] = color;
    for (int y = 0; y < ROUND_LCD_RES; y += lines)
        round_lcd_blit(0, y, ROUND_LCD_RES, lines, buf);
    free(buf);
}
