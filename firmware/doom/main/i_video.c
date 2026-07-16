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

/* ---------------- input: 74HC165 buttons -> D_PostEvent ----------------
 *
 * In game:  up/down = move, left/right = STRAFE, hold B + left/right = turn,
 *           A = fire, START = use (open doors/walls), SELECT = open menu.
 *           START held ~0.6s also opens the menu (fallback while the v1
 *           board's SELECT line is broken).
 * In menu:  d-pad navigates, SELECT/A = confirm item, B = back/close.
 * Hold A+B+START ~2s anywhere -> back to the launcher.
 * (On unrepaired v1 boards the SELECT line is stuck — the BSP auto-enables
 * it once the line is seen healthy.)
 */

extern boolean menuactive; /* m_menu.c */

enum {
    VK_FWD, VK_BACK, VK_TURN_L, VK_TURN_R, VK_STRAFE_L, VK_STRAFE_R,
    VK_FIRE, VK_USE, VK_ESC, VK_ENTER, VK_COUNT
};

static int *const vk_bind[VK_COUNT] = {
    &key_up, &key_down, &key_left, &key_right,
    &key_strafeleft, &key_straferight,
    &key_fire, &key_use, &key_escape, &key_menu_enter,
};

/* hold A+B+START ~2s -> back to the launcher (factory partition) */
static void check_exit_combo(uint8_t held)
{
    static int count;
    if ((held & (GF_KEY_A | GF_KEY_B | GF_KEY_START))
        == (GF_KEY_A | GF_KEY_B | GF_KEY_START)) {
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
    uint8_t raw;
    uint8_t b = gf_keys_read(&raw) & (uint8_t)~KEYS_IGNORE_MASK;

    /* calibration log: every button-state change (physical wiring on the
     * old board maps buttons to unexpected register bits — measure it) */
    static uint8_t log_prev;
    static int log_count;
    if (b != log_prev && log_count < 100) {
        log_count++;
        lprintf(LO_INFO, "keys: 0x%02x -> 0x%02x (raw=0x%02x)\n",
                log_prev, b, raw);
        log_prev = b;
    }

    check_exit_combo(b);

    int menu = menuactive;
    uint16_t cur = 0;

    if (b & GF_KEY_UP)
        cur |= 1 << VK_FWD;
    if (b & GF_KEY_DOWN)
        cur |= 1 << VK_BACK;

    if (menu) {
        /* raw arrows navigate the menu */
        if (b & GF_KEY_LEFT)
            cur |= 1 << VK_TURN_L;
        if (b & GF_KEY_RIGHT)
            cur |= 1 << VK_TURN_R;
        if (b & GF_KEY_A)
            cur |= 1 << VK_ENTER; /* confirm item */
        if (b & GF_KEY_B)
            cur |= 1 << VK_ESC;   /* back / close */
    } else {
        /* left/right strafe; hold B to turn instead */
        if (b & GF_KEY_LEFT)
            cur |= 1 << ((b & GF_KEY_B) ? VK_TURN_L : VK_STRAFE_L);
        if (b & GF_KEY_RIGHT)
            cur |= 1 << ((b & GF_KEY_B) ? VK_TURN_R : VK_STRAFE_R);

        if (b & GF_KEY_A)
            cur |= 1 << VK_FIRE;
    }

    if (b & GF_KEY_START)
        cur |= 1 << VK_USE;

    /* START held ~0.6s opens the menu — usable fallback while the SELECT
     * line is physically broken on the v1 board (short press = use). */
    static int start_hold;
    if (!menu && (b & GF_KEY_START)) {
        if (++start_hold == 21) {
            event_t ev;
            ev.type = ev_keydown;
            ev.data1 = *vk_bind[VK_ESC];
            D_PostEvent(&ev);
            ev.type = ev_keyup;
            D_PostEvent(&ev);
        }
    } else {
        start_hold = 0;
    }

    /* SELECT: resolved at press time (game: open menu / menu: confirm) and
     * held to that meaning until release, so opening the menu doesn't
     * instantly confirm the first item */
    static int sel_vk = -1;
    if ((b & GF_KEY_SELECT) && sel_vk < 0)
        sel_vk = menu ? VK_ENTER : VK_ESC;
    if (!(b & GF_KEY_SELECT))
        sel_vk = -1;
    if (sel_vk >= 0)
        cur |= 1 << sel_vk;

    static uint16_t prev;
    if (cur == prev)
        return;
    event_t ev;
    for (int k = 0; k < VK_COUNT; k++) {
        if ((prev ^ cur) & (1 << k)) {
            ev.type = (cur & (1 << k)) ? ev_keydown : ev_keyup;
            ev.data1 = *vk_bind[k];
            D_PostEvent(&ev);
        }
    }
    prev = cur;
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
