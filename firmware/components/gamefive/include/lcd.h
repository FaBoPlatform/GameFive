/*
 * Game Five — ST7789 display helper (esp_lcd based)
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/* RGB565 with the byte swap the SPI wire order requires, so buffers can be
 * sent as-is. Use GF_RGB() for all colors. */
#define GF_RGB(r, g, b)                                                     \
    ((uint16_t)(((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) >> 8) | \
     (uint16_t)(((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)) << 8))

#define GF_BLACK   GF_RGB(0, 0, 0)
#define GF_WHITE   GF_RGB(255, 255, 255)
#define GF_RED     GF_RGB(255, 0, 0)
#define GF_GREEN   GF_RGB(0, 255, 0)
#define GF_BLUE    GF_RGB(0, 0, 255)
#define GF_YELLOW  GF_RGB(255, 255, 0)
#define GF_CYAN    GF_RGB(0, 255, 255)
#define GF_MAGENTA GF_RGB(255, 0, 255)
#define GF_GRAY    GF_RGB(128, 128, 128)
#define GF_DKGRAY  GF_RGB(48, 48, 48)
#define GF_ORANGE  GF_RGB(255, 160, 0)

/* Initializes the shared SPI bus, the panel, and the backlight (off). */
esp_err_t gf_lcd_init(void);

/* Backlight brightness 0..100 (%). */
void gf_lcd_backlight(int percent);

/* Raw esp_lcd panel handle (cast to esp_lcd_panel_handle_t) for apps that
 * need direct panel ops, e.g. landscape rotation. NULL before gf_lcd_init.
 * Note: the fill/clear/text helpers keep portrait clipping after a rotation —
 * use gf_lcd_blit (no clipping) in rotated modes. */
void *gf_lcd_panel(void);

/* Filled rectangle. Coordinates clipped to the panel. */
void gf_lcd_fill_rect(int x, int y, int w, int h, uint16_t color);

/* Full-screen fill. */
void gf_lcd_clear(uint16_t color);

/* Rectangle outline, t = line thickness. */
void gf_lcd_rect(int x, int y, int w, int h, int t, uint16_t color);

/* 8x8 font text, integer scale >= 1. bg drawn behind each glyph cell. */
void gf_lcd_text(int x, int y, const char *s, int scale,
                 uint16_t fg, uint16_t bg);

/* Width in pixels of a string at a given scale (8 px per glyph). */
static inline int gf_lcd_text_w(const char *s, int scale)
{
    int n = 0;
    while (s[n] != '\0') n++;
    return n * 8 * scale;
}

/* Raw bitmap push (RGB565, already byte-swapped as GF_RGB produces). */
void gf_lcd_blit(int x, int y, int w, int h, const uint16_t *pix);
