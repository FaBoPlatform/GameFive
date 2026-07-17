/*
 * Game Five DOOM — GC9B72 2.1" round 360x360 display (experimental)
 *
 * The round module plugs into J3 of the v1 board in the reversed
 * orientation; its control pins land on repurposed board nets, remapped
 * here through the ESP32-S3 GPIO matrix (see round_lcd.c).
 *
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#define ROUND_LCD_RES 360   /* 360x360, circular active area */

esp_err_t round_lcd_init(void);   /* also initializes the shared SPI bus
                                     (SCK GPIO7 + MISO GPIO8) so gf_keys
                                     can add its 74HC165 device afterwards */
void round_lcd_clear(uint16_t color565_swapped);
void round_lcd_blit(int x, int y, int w, int h, const uint16_t *pix);
