/*
 * Game Five — 74HC165 button reader (shares the LCD SPI bus)
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Pressed-state bit positions (1 = pressed) as returned by gf_keys_read(). */
typedef enum {
    GF_KEY_UP     = 1 << 0,  /* 74HC165 input A */
    GF_KEY_DOWN   = 1 << 1,  /* input B */
    GF_KEY_LEFT   = 1 << 2,  /* input C */
    GF_KEY_RIGHT  = 1 << 3,  /* input D */
    GF_KEY_A      = 1 << 4,  /* input E */
    GF_KEY_B      = 1 << 5,  /* input F */
    GF_KEY_START  = 1 << 6,  /* input G */
    GF_KEY_SELECT = 1 << 7,  /* input H */
} gf_key_t;

#define GF_KEY_COUNT 8

/* Key names indexed by bit position 0..7. */
extern const char *const gf_key_names[GF_KEY_COUNT];

/* Adds the shift register as a device on the (already initialized) SPI bus. */
esp_err_t gf_keys_init(void);

/*
 * Reads all 8 buttons. Returns a pressed mask (1 = pressed) built from
 * gf_key_t bits. raw_out (optional) receives the raw shift-register byte
 * before inversion/remap — useful for bring-up debugging.
 */
uint8_t gf_keys_read(uint8_t *raw_out);
