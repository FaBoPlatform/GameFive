/*
 * Game Five — hardware bring-up test
 *
 *   1. Display test: full-screen fills, color bars, gradient, checkerboard,
 *      edge/crosshair pattern, splash screen.
 *   2. Key test: live on-screen indicators for all 8 buttons read through
 *      the 74HC165, plus raw-bit readout and serial logging.
 *
 * Holding SELECT + START for ~1.5 s re-runs the display test.
 *
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "gamefive_pins.h"
#include "lcd.h"
#include "keys.h"

static const char *TAG = "gf_hwtest";

#define W GF_LCD_H_RES  /* 240 */
#define H GF_LCD_V_RES  /* 320 */

/* ---------------------------------------------------------------- display */

static void fill_step(uint16_t color, const char *name, uint16_t text_col)
{
    ESP_LOGI(TAG, "fill: %s", name);
    gf_lcd_clear(color);
    int tw = gf_lcd_text_w(name, 3);
    gf_lcd_text((W - tw) / 2, H / 2 - 12, name, 3, text_col, color);
    vTaskDelay(pdMS_TO_TICKS(700));
}

static void display_test(void)
{
    ESP_LOGI(TAG, "=== display test ===");

    fill_step(GF_RED,   "RED",   GF_WHITE);
    fill_step(GF_GREEN, "GREEN", GF_BLACK);
    fill_step(GF_BLUE,  "BLUE",  GF_WHITE);
    fill_step(GF_WHITE, "WHITE", GF_BLACK);
    fill_step(GF_BLACK, "BLACK", GF_WHITE);

    /* color bars */
    ESP_LOGI(TAG, "color bars");
    const uint16_t bars[8] = { GF_WHITE, GF_YELLOW, GF_CYAN, GF_GREEN,
                               GF_MAGENTA, GF_RED, GF_BLUE, GF_BLACK };
    for (int i = 0; i < 8; i++) {
        gf_lcd_fill_rect(i * (W / 8), 0, W / 8, H, bars[i]);
    }
    vTaskDelay(pdMS_TO_TICKS(1200));

    /* vertical grayscale gradient */
    ESP_LOGI(TAG, "gradient");
    for (int band = 0; band < 32; band++) {
        int v = band * 255 / 31;
        gf_lcd_fill_rect(0, band * H / 32, W, H / 32 + 1, GF_RGB(v, v, v));
    }
    vTaskDelay(pdMS_TO_TICKS(1200));

    /* checkerboard, 8 px cells */
    ESP_LOGI(TAG, "checkerboard");
    for (int y = 0; y < H; y += 8) {
        for (int x = 0; x < W; x += 8) {
            gf_lcd_fill_rect(x, y, 8, 8,
                             (((x ^ y) >> 3) & 1) ? GF_WHITE : GF_BLACK);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(1200));

    /* edge check: 1 px border, crosshair, corner squares */
    ESP_LOGI(TAG, "edges + crosshair");
    gf_lcd_clear(GF_BLACK);
    gf_lcd_rect(0, 0, W, H, 1, GF_WHITE);
    gf_lcd_fill_rect(W / 2 - 1, 0, 2, H, GF_GREEN);
    gf_lcd_fill_rect(0, H / 2 - 1, W, 2, GF_GREEN);
    gf_lcd_fill_rect(1, 1, 12, 12, GF_RED);            /* top-left    */
    gf_lcd_fill_rect(W - 13, 1, 12, 12, GF_YELLOW);    /* top-right   */
    gf_lcd_fill_rect(1, H - 13, 12, 12, GF_BLUE);      /* bottom-left */
    gf_lcd_fill_rect(W - 13, H - 13, 12, 12, GF_CYAN); /* bottom-right*/
    gf_lcd_text(20, 20, "TL", 1, GF_WHITE, GF_BLACK);
    vTaskDelay(pdMS_TO_TICKS(1500));

    /* splash */
    gf_lcd_clear(GF_BLACK);
    gf_lcd_rect(6, 6, W - 12, H - 12, 2, GF_ORANGE);
    gf_lcd_text((W - gf_lcd_text_w("GAME", 5)) / 2,  90, "GAME", 5,
                GF_ORANGE, GF_BLACK);
    gf_lcd_text((W - gf_lcd_text_w("FIVE", 5)) / 2, 140, "FIVE", 5,
                GF_WHITE, GF_BLACK);
    gf_lcd_text((W - gf_lcd_text_w("HW TEST", 2)) / 2, 200, "HW TEST", 2,
                GF_GRAY, GF_BLACK);
    gf_lcd_text((W - gf_lcd_text_w("Designed by Fable5", 1)) / 2, 300,
                "Designed by Fable5", 1, GF_DKGRAY, GF_BLACK);
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "=== display test done ===");
}

/* --------------------------------------------------------------- key test */

typedef struct {
    int x, y, w, h;
    const char *label;
} key_box_t;

/* Indexed by gf_key_t bit position; portrait layout mirroring the board:
 * D-pad left, B/A right, SELECT/START bottom center. */
static const key_box_t k_boxes[GF_KEY_COUNT] = {
    [0] = {  48, 100, 34, 34, "U" },  /* UP     */
    [1] = {  48, 172, 34, 34, "D" },  /* DOWN   */
    [2] = {  12, 136, 34, 34, "L" },  /* LEFT   */
    [3] = {  84, 136, 34, 34, "R" },  /* RIGHT  */
    [4] = { 192, 118, 40, 40, "A" },  /* A      */
    [5] = { 140, 154, 40, 40, "B" },  /* B      */
    [6] = { 130, 246, 76, 26, "STA" },/* START  */
    [7] = {  34, 246, 76, 26, "SEL" },/* SELECT */
};

static void draw_key(int i, bool pressed)
{
    const key_box_t *b = &k_boxes[i];
    uint16_t fill = pressed ? GF_GREEN : GF_DKGRAY;
    uint16_t text = pressed ? GF_BLACK : GF_GRAY;
    gf_lcd_fill_rect(b->x, b->y, b->w, b->h, fill);
    gf_lcd_rect(b->x, b->y, b->w, b->h, 2, pressed ? GF_WHITE : GF_GRAY);
    int tw = gf_lcd_text_w(b->label, 1);
    gf_lcd_text(b->x + (b->w - tw) / 2, b->y + (b->h - 8) / 2, b->label, 1,
                text, fill);
}

static void draw_key_screen(void)
{
    gf_lcd_clear(GF_BLACK);
    gf_lcd_text((W - gf_lcd_text_w("KEY TEST", 2)) / 2, 12, "KEY TEST", 2,
                GF_WHITE, GF_BLACK);
    gf_lcd_fill_rect(0, 36, W, 2, GF_ORANGE);
    for (int i = 0; i < GF_KEY_COUNT; i++) draw_key(i, false);
    gf_lcd_text(12, 288, "RAW", 1, GF_GRAY, GF_BLACK);
    gf_lcd_text(12, 304, "CNT", 1, GF_GRAY, GF_BLACK);
    gf_lcd_text((W - gf_lcd_text_w("SEL+STA: retest", 1)) / 2, 60,
                "SEL+STA: retest", 1, GF_DKGRAY, GF_BLACK);
}

static void key_test(void)
{
    ESP_LOGI(TAG, "=== key test === (press buttons; SELECT+START reruns display test)");
    draw_key_screen();

    uint8_t prev = 0;
    uint8_t prev_raw = 0xFF;
    uint32_t presses = 0;
    int hold_ticks = 0;
    char buf[24];

    /* force initial raw/count text */
    bool refresh = true;

    for (;;) {
        uint8_t raw;
        uint8_t keys = gf_keys_read(&raw);

        uint8_t changed = keys ^ prev;
        for (int i = 0; i < GF_KEY_COUNT; i++) {
            uint8_t bit = 1u << i;
            if (changed & bit) {
                bool pressed = keys & bit;
                draw_key(i, pressed);
                if (pressed) presses++;
                ESP_LOGI(TAG, "%-6s %s (raw=0x%02X)", gf_key_names[i],
                         (keys & bit) ? "DOWN" : "UP", raw);
            }
        }

        if (raw != prev_raw || refresh) {
            snprintf(buf, sizeof(buf), "0x%02X ", raw);
            gf_lcd_text(48, 288, buf, 1, GF_WHITE, GF_BLACK);
            for (int i = 7; i >= 0; i--) {
                gf_lcd_text(96 + (7 - i) * 10, 288, (raw & (1u << i)) ? "1" : "0",
                            1, (raw & (1u << i)) ? GF_GRAY : GF_GREEN, GF_BLACK);
            }
        }
        if (changed || refresh) {
            snprintf(buf, sizeof(buf), "%lu ", (unsigned long)presses);
            gf_lcd_text(48, 304, buf, 1, GF_WHITE, GF_BLACK);
        }

        /* SELECT + START held -> rerun the display test.
         * raw == 0x00 (all eight "pressed") only happens when the shift
         * register is absent/floating (a bare XIAO on the bench) — with the
         * 10k pull-ups on the real board an idle read is 0xFF. Ignore the
         * combo then, so the test doesn't loop by itself. */
        if (raw != 0x00 &&
            (keys & (GF_KEY_SELECT | GF_KEY_START)) ==
            (GF_KEY_SELECT | GF_KEY_START)) {
            hold_ticks++;
            if (hold_ticks >= 75) { /* 75 * 20 ms = 1.5 s */
                display_test();
                draw_key_screen();
                prev = 0;
                prev_raw = 0xFF;
                refresh = true;
                hold_ticks = 0;
                continue;
            }
        } else {
            hold_ticks = 0;
        }

        prev = keys;
        prev_raw = raw;
        refresh = false;
        vTaskDelay(pdMS_TO_TICKS(20)); /* 50 Hz poll */
    }
}

/* ------------------------------------------------------------------ main */

void app_main(void)
{
    ESP_LOGI(TAG, "Game Five HW test (XIAO ESP32-S3)");
    ESP_ERROR_CHECK(gf_lcd_init());
    ESP_ERROR_CHECK(gf_keys_init());

    /* fade the backlight in over ~0.5 s */
    for (int p = 0; p <= 100; p += 5) {
        gf_lcd_backlight(p);
        vTaskDelay(pdMS_TO_TICKS(25));
    }

    display_test();
    key_test(); /* never returns */
}
