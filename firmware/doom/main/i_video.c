/*
 * Game Five DOOM — video + input layer for the Game Five BSP
 * (ST7789 320x240 landscape via esp_lcd, 74HC165 buttons on the shared bus)
 *
 * Derived from esp32-doom's prboom-esp32-compat/i_video.c (GPL-2.0):
 * PrBoom: Copyright (C) 1999-2006 id Software, Lee Killough, Colin Phipps et al.
 * ESP32 code: Copyright 2016-2017 Espressif Systems, Apache-2.0.
 *
 * The core renders 320x240 8-bit indexed into screens[0] (internal SRAM).
 * I_FinishUpdate palette-expands it through a byte-swapped RGB565 LUT in
 * CHUNK_LINES slices and pushes each slice with gf_lcd_blit() (esp_lcd DMA,
 * synchronous per slice).
 */

#include "config.h"
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include "m_argv.h"
#include "doomstat.h"
#include "doomdef.h"
#include "doomtype.h"
#include "v_video.h"
#include "r_draw.h"
#include "d_main.h"
#include "d_event.h"
#include "g_game.h"
#include "i_video.h"
#include "z_zone.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "lprintf.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "lcd.h"
#include "keys.h"

#include "doom_config.h"

#if DOOM_OLDBOARD_COMPAT
#define KEYS_IGNORE_MASK ((uint8_t)(GF_KEY_UP | GF_KEY_SELECT))
#else
#define KEYS_IGNORE_MASK ((uint8_t)0)
#endif

int use_fullscreen = 0;
int use_doublebuffer = 0;

/* keyboard emulation only — but the core links these joystick globals */
int usejoystick = 0;
int joyleft, joyright, joyup, joydown;

/* ---------------- input: 74HC165 buttons -> D_PostEvent ---------------- */

typedef struct {
    uint8_t mask;
    int *key; /* prboom runtime key binding variable */
} KeyMap;

static const KeyMap keymap[] = {
    { GF_KEY_UP,     &key_up },
    { GF_KEY_DOWN,   &key_down },
    { GF_KEY_LEFT,   &key_left },
    { GF_KEY_RIGHT,  &key_right },
    { GF_KEY_A,      &key_fire },       /* A posts both fire and menu-enter */
    { GF_KEY_A,      &key_menu_enter },
    { GF_KEY_B,      &key_use },
    { GF_KEY_START,  &key_escape },     /* menu open/close */
    { GF_KEY_SELECT, &key_map },        /* automap */
    { 0, NULL },
};

/* hold B+START ~2s -> back to the launcher (factory partition) */
static void check_exit_combo(uint8_t held)
{
    static int count;
    if ((held & (GF_KEY_B | GF_KEY_START)) == (GF_KEY_B | GF_KEY_START)) {
        if (++count >= 70) { /* 35 tics/s */
            const esp_partition_t *factory = esp_partition_find_first(
                ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY,
                NULL);
            if (factory && esp_ota_set_boot_partition(factory) == ESP_OK)
                esp_restart();
        }
    } else {
        count = 0;
    }
}

void I_StartTic(void)
{
    static uint8_t prev;
    uint8_t now = gf_keys_read(NULL) & (uint8_t)~KEYS_IGNORE_MASK;

    check_exit_combo(now);
    if (now == prev)
        return;

    event_t ev;
    for (int i = 0; keymap[i].key != NULL; i++) {
        if ((prev ^ now) & keymap[i].mask) {
            ev.type = (now & keymap[i].mask) ? ev_keydown : ev_keyup;
            ev.data1 = *keymap[i].key;
            D_PostEvent(&ev);
        }
    }
    prev = now;
}

/* ---------------------------- graphics --------------------------------- */

static unsigned char *screenbuf; /* 320x240 8-bit, internal SRAM */
static uint16_t lcdpal[256];     /* PLAYPAL -> byte-swapped RGB565 */

#define CHUNK_LINES 40           /* 320*40*2 = 25,600 B DMA slice buffer */
static uint16_t *chunkbuf;

void I_PreInitGraphics(void)
{
    lprintf(LO_INFO, "I_PreInitGraphics: fb %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
    screenbuf = heap_caps_malloc(SCREENWIDTH * SCREENHEIGHT,
                                 MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    chunkbuf = heap_caps_malloc(SCREENWIDTH * CHUNK_LINES * 2,
                                MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    assert(screenbuf && chunkbuf);
}

#if DOOM_PORTRAIT
/*
 * Portrait: downscale 320x240 -> 240x180 (3/4, aspect kept) and center on
 * the 240x320 panel (y offset 70). Nearest-neighbor via precomputed maps.
 */
#define OUT_W 240
#define OUT_H 180
#define OUT_Y0 ((320 - OUT_H) / 2)
#define OUT_CHUNK 45

void I_FinishUpdate(void)
{
    static uint16_t xmap[OUT_W];
    static uint16_t ymap[OUT_H];
    if (xmap[OUT_W - 1] == 0) {
        for (int x = 0; x < OUT_W; x++) xmap[x] = (x * 4) / 3;
        for (int y = 0; y < OUT_H; y++) ymap[y] = (y * 4) / 3;
    }

    const unsigned char *fb = (const unsigned char *)screens[0].data;
    for (int y0 = 0; y0 < OUT_H; y0 += OUT_CHUNK) {
        uint16_t *dst = chunkbuf;
        for (int y = y0; y < y0 + OUT_CHUNK; y++) {
            const unsigned char *row = fb + ymap[y] * SCREENWIDTH;
            for (int x = 0; x < OUT_W; x++)
                *dst++ = lcdpal[row[xmap[x]]];
        }
        gf_lcd_blit(0, OUT_Y0 + y0, OUT_W, OUT_CHUNK, chunkbuf);
    }
}
#else
void I_FinishUpdate(void)
{
    const unsigned char *src = (const unsigned char *)screens[0].data;

    for (int y = 0; y < SCREENHEIGHT; y += CHUNK_LINES) {
        uint16_t *dst = chunkbuf;
        for (int n = SCREENWIDTH * CHUNK_LINES; n > 0; n--)
            *dst++ = lcdpal[*src++];
        gf_lcd_blit(0, y, SCREENWIDTH, CHUNK_LINES, chunkbuf);
    }
}
#endif

void I_SetPalette(int pal)
{
    int pplump = W_GetNumForName("PLAYPAL");
    const byte *palette = W_CacheLumpNum(pplump);
    static int dumped;
    if (!dumped) {
        dumped = 1;
        /* index 176 is pure red (255,0,0) in the stock PLAYPAL */
        lprintf(LO_INFO,
                "I_SetPalette dump: pal=%d rgb[0]=(%d,%d,%d) rgb[176]=(%d,%d,%d)\n",
                pal, palette[0], palette[1], palette[2],
                palette[176 * 3], palette[176 * 3 + 1], palette[176 * 3 + 2]);
    }
    palette += pal * (3 * 256);
    for (int i = 0; i < 256; i++) {
        int v = ((palette[0] >> 3) << 11) | ((palette[1] >> 2) << 5)
                | (palette[2] >> 3);
        lcdpal[i] = (uint16_t)((v >> 8) | (v << 8)); /* SPI wire byte order */
        palette += 3;
    }
    W_UnlockLumpNum(pplump);
}

void I_SetRes(void)
{
    for (int i = 0; i < 3; i++) {
        screens[i].width = SCREENWIDTH;
        screens[i].height = SCREENHEIGHT;
        screens[i].byte_pitch = SCREENPITCH;
        screens[i].short_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE16);
        screens[i].int_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE32);
    }

    /* statusbar */
    screens[4].width = SCREENWIDTH;
    screens[4].height = (ST_SCALED_HEIGHT + 1);
    screens[4].byte_pitch = SCREENPITCH;
    screens[4].short_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE16);
    screens[4].int_pitch = SCREENPITCH / V_GetModePixelDepth(VID_MODE32);

    screens[0].not_on_heap = true;
    screens[0].data = screenbuf;
    assert(screens[0].data);

    lprintf(LO_INFO, "I_SetRes: using resolution %dx%d\n",
            SCREENWIDTH, SCREENHEIGHT);
}

void I_InitGraphics(void)
{
    static int firsttime = 1;

    if (firsttime) {
        firsttime = 0;
        atexit(I_ShutdownGraphics);
        lprintf(LO_INFO, "I_InitGraphics: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
        I_UpdateVideoMode();
    }
}

void I_UpdateVideoMode(void)
{
    lprintf(LO_INFO, "I_UpdateVideoMode: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);

    V_InitMode(VID_MODE8);
    V_DestroyUnusedTrueColorPalettes();
    V_FreeScreens();

    I_SetRes();

    V_AllocScreens();

    R_InitBuffer(SCREENWIDTH, SCREENHEIGHT);
}

int I_StartDisplay(void)
{
    return true; /* blits are synchronous; nothing in flight */
}

void I_EndDisplay(void)
{
}

void I_ShutdownGraphics(void)
{
}

void I_UpdateNoBlit(void)
{
}

void I_StartFrame(void)
{
}
