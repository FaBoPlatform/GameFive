/* Game Five DOOM — build-time display/input options */
#pragma once

/*
 * 1 = portrait: hold the console normally; the 320x240 game is downscaled
 *     4:3 -> 240x180 and centered on the 240x320 panel (black bars).
 * 0 = landscape: full-screen 320x240, hold the console sideways.
 */
#define DOOM_PORTRAIT 1

/* Portrait only: 1 = extra 180 flip on top of the BSP's GF_LCD_ROTATE_180.
 * The BSP now carries the panel's base orientation — keep this 0. */
#define DOOM_PORTRAIT_FLIP 0

/* Landscape only: 1 = image top toward the board's left edge; 0 = flip 180. */
#define DOOM_ROTATE_CW 1

/*
 * Old-prototype compatibility (HandyGame rev.0 board): the UP and SELECT
 * shift-register lines are stuck low, so they must be masked or the engine
 * sees them held down forever. Set to 0 for the Game Five rev.1+ board.
 */
#define DOOM_OLDBOARD_COMPAT 0

/* The HS20HS072RX panel needs INVON — now handled by the BSP
 * (GF_LCD_INVERT=1 in gamefive_pins.h), so keep this 0. */
#define DOOM_LCD_INVERT 0
