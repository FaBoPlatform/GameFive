/* Game Five launcher — game store client (GitHub raw index + OTA install) */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define STORE_INDEX_URL \
    "https://raw.githubusercontent.com/FaBoPlatform/GameFive/main/store/index.txt"

#define STORE_MAX_GAMES 16

typedef struct {
    char id[24];
    char title[28];
    char version[12];
    char bin_url[200];
    char assets_url[200];
    int assets_size;
} store_game_t;

typedef struct {
    store_game_t games[STORE_MAX_GAMES];
    int count;
} store_index_t;

typedef void (*store_progress_cb)(const char *phase, int done, int total);

/* Downloads and parses the store index. */
esp_err_t store_fetch_index(store_index_t *out);

/* Downloads the game bin into ota_0 (and assets into the assets partition
 * if the game has any and they differ from what's installed). */
esp_err_t store_install(const store_game_t *g, store_progress_cb progress);

/* True if this exact game id+version is recorded as installed in ota_0. */
bool store_is_installed(const store_game_t *g);

/* True if ANY game is recorded as installed in ota_0. */
bool store_has_game(void);

/* 0 = not installed, 1 = update available, 2 = current version installed. */
int store_installed_state(const store_game_t *g);

/* Sets the boot partition to ota_0 and reboots into the game. */
void store_boot_game(void);
