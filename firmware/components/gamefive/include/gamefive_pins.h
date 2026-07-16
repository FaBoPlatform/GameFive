/*
 * Game Five — board pin map (Seeed XIAO ESP32-S3)
 *
 * Matches the Game Five PCB rev.1 (see repository README pin map table).
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#pragma once

/* ---- SPI bus (shared: ST7789 LCD + 74HC165 button shift register) ---- */
#define GF_SPI_HOST        SPI2_HOST
#define GF_PIN_SPI_SCK     7   /* XIAO D8  — LCD SCL + 74HC165 CP  */
#define GF_PIN_SPI_MOSI    9   /* XIAO D10 — LCD SDA               */
#define GF_PIN_SPI_MISO    8   /* XIAO D9  — 74HC165 QH            */

/* ---- ST7789 LCD (HS20HS072RX, 240x320, 4-wire SPI, write-only) ---- */
#define GF_PIN_LCD_CS      1   /* XIAO D0 */
#define GF_PIN_LCD_DC      2   /* XIAO D1 */
#define GF_PIN_LCD_RST     3   /* XIAO D2 */
#define GF_LCD_H_RES       240
#define GF_LCD_V_RES       320
#define GF_LCD_PCLK_HZ     (40 * 1000 * 1000)

/*
 * Screen orientation. Set to 1 if your module shows everything upside down.
 * The current display unit (HS20HS072RX 2025.12 batch, as mounted) needs
 * the 180 flip — confirmed 2026-07-10 on the launcher and DOOM.
 */
#define GF_LCD_ROTATE_180  1

/*
 * Some ST7789 modules (most IPS variants) need color inversion enabled.
 * If RED shows as CYAN during the fill test, flip this to 1.
 * Confirmed 2026-07-09 with a 3-row color-bar test on the HS20HS072RX
 * (2025.12 batch): this panel needs INVON.
 */
#define GF_LCD_INVERT      1

/*
 * HandyGame v1 board: the button column is wired one 74HC165 input off —
 * each button lands on input N+1 (UP→B, ... START→H), input A is stuck low,
 * and SELECT drives that dead input A. Shifting the sample right by one
 * realigns every button with its silkscreen; SELECT then always reads
 * released (7 usable buttons). Set to 0 for correctly-wired boards (rev.2+).
 * Verified 2026-07-10 with the hwtest key screen.
 */
#define GF_KEYS_BIT_SHIFT  1

/* ---- Backlight: SI2302 low-side NMOS, PWM, active high ---- */
#define GF_PIN_LCD_BL      5   /* XIAO D4 */

/* ---- 74HC165 button shift register ---- */
#define GF_PIN_KEYS_LATCH  4   /* XIAO D3 — SH/LD#, pulse LOW to load */

/* ---- I2S audio (MAX98357A) — unused by this test, listed for reference ---- */
#define GF_PIN_I2S_BCLK    6   /* XIAO D5 */
#define GF_PIN_I2S_LRCLK   43  /* XIAO D6 */
#define GF_PIN_I2S_DIN     44  /* XIAO D7 */
