/*
 * Game Five launcher — resident menu (factory partition).
 * Flow: WiFi creds? -> STA connect -> fetch store index -> menu -> install/boot.
 * No creds (or B on the failure screen) -> SoftAP browser provisioning.
 * Games return here with B+START held (they set the boot partition back).
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lcd.h"
#include "keys.h"
#include "wifi_prov.h"
#include "store.h"

static const char *TAG = "launcher";

/* Old-prototype board: UP and SELECT lines are stuck low — mask them and
 * navigate with LEFT/RIGHT/DOWN. Set to 0 for rev.1+ boards. */
#define LAUNCHER_OLDBOARD_COMPAT 1

#if LAUNCHER_OLDBOARD_COMPAT
#define KEYS_IGNORE ((uint8_t)(GF_KEY_UP | GF_KEY_SELECT))
#else
#define KEYS_IGNORE ((uint8_t)0)
#endif

#define W 240
#define H 320

#define C_BG     GF_BLACK
#define C_TITLE  GF_RGB(255, 48, 48)
#define C_TEXT   GF_WHITE
#define C_DIM    GF_GRAY
#define C_SEL    GF_RGB(255, 220, 0)
#define C_OK     GF_GREEN

static store_index_t s_index;

/* ---------------- tiny UI helpers ---------------- */

static void ui_clear(void)
{
    gf_lcd_clear(C_BG);
}

static void ui_center(int y, const char *s, int scale, uint16_t fg)
{
    int x = (W - gf_lcd_text_w(s, scale)) / 2;
    gf_lcd_text(x < 0 ? 0 : x, y, s, scale, fg, C_BG);
}

static void ui_header(void)
{
    ui_center(16, "GAME FIVE", 3, C_TITLE);
    ui_center(44, "game store", 1, C_DIM);
}

static void ui_status(const char *line1, const char *line2)
{
    ui_clear();
    ui_header();
    if (line1)
        ui_center(140, line1, 1, C_TEXT);
    if (line2)
        ui_center(160, line2, 1, C_DIM);
}

static uint8_t keys_edges(void)
{
    static uint8_t prev;
    uint8_t now = gf_keys_read(NULL) & (uint8_t)~KEYS_IGNORE;
    uint8_t edges = (uint8_t)(now & ~prev);
    prev = now;
    return edges;
}

static void progress_cb(const char *phase, int done, int total)
{
    static int last_pct = -1;
    char line[40];
    if (total > 0) {
        int pct = (int)((int64_t)done * 100 / total);
        if (pct == last_pct)
            return;
        last_pct = pct;
        snprintf(line, sizeof(line), "%s %d%%", phase, pct);
        int bar = (W - 40) * pct / 100;
        gf_lcd_fill_rect(20, 210, bar, 10, C_OK);
        gf_lcd_rect(20, 210, W - 40, 10, 1, C_DIM);
    } else {
        snprintf(line, sizeof(line), "%s...", phase);
    }
    int tw = gf_lcd_text_w("ASSETS 100%", 1);
    gf_lcd_fill_rect((W - tw) / 2 - 8, 186, tw + 16, 12, C_BG);
    ui_center(186, line, 1, C_TEXT);
}

/* ---------------- screens ---------------- */

static void screen_provisioning(void)
{
    ui_clear();
    ui_header();
    ui_center(90, "WiFi SETUP", 2, C_SEL);
    gf_lcd_text(12, 130, "1. On your phone, join", 1, C_TEXT, C_BG);
    gf_lcd_text(24, 146, "WiFi: " GF_PROV_AP_SSID, 1, C_OK, C_BG);
    gf_lcd_text(12, 170, "2. Open in a browser:", 1, C_TEXT, C_BG);
    gf_lcd_text(24, 186, GF_PROV_URL, 1, C_OK, C_BG);
    gf_lcd_text(12, 210, "3. Enter your WiFi and", 1, C_TEXT, C_BG);
    gf_lcd_text(24, 226, "save. I will reboot.", 1, C_TEXT, C_BG);
    wifi_provisioning_run(); /* never returns */
}

static void screen_wifi_failed(const char *ssid)
{
    ui_clear();
    ui_header();
    ui_center(120, "WiFi FAILED", 2, C_TITLE);
    ui_center(156, ssid, 1, C_DIM);
    ui_center(172, wifi_fail_hint(), 1, C_SEL);
    ui_center(200, "A: retry   B: WiFi setup", 1, C_TEXT);
    for (;;) {
        uint8_t e = keys_edges();
        if (e & GF_KEY_A)
            esp_restart();
        if (e & GF_KEY_B) {
            wifi_creds_clear();
            esp_restart(); /* boots into provisioning */
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void menu_draw(int sel)
{
    ui_clear();
    ui_header();
    for (int i = 0; i < s_index.count; i++) {
        store_game_t *g = &s_index.games[i];
        int y = 80 + i * 26;
        uint16_t fg = (i == sel) ? C_SEL : C_TEXT;
        gf_lcd_text(8, y, (i == sel) ? ">" : " ", 2, C_SEL, C_BG);
        gf_lcd_text(28, y, g->title, 2, fg, C_BG);
        if (store_is_installed(g))
            gf_lcd_text(W - 30, y + 4, "OK", 1, C_OK, C_BG);
    }
    int y = 80 + s_index.count * 26;
    gf_lcd_text(8, y, (sel == s_index.count) ? ">" : " ", 2, C_SEL, C_BG);
    gf_lcd_text(28, y, "WIFI SETUP", 2,
                (sel == s_index.count) ? C_SEL : C_DIM, C_BG);

    ui_center(H - 24, "L/R/DOWN: move   A: play", 1, C_DIM);
}

static void screen_menu(void)
{
    int sel = 0;
    int items = s_index.count + 1; /* + WiFi setup entry */
    menu_draw(sel);
    for (;;) {
        uint8_t e = keys_edges();
        if (e & (GF_KEY_DOWN | GF_KEY_RIGHT)) {
            sel = (sel + 1) % items;
            menu_draw(sel);
        } else if (e & (GF_KEY_LEFT | GF_KEY_UP)) {
            sel = (sel + items - 1) % items;
            menu_draw(sel);
        } else if (e & GF_KEY_A) {
            if (sel == s_index.count) {
                wifi_creds_clear();
                esp_restart();
            }
            store_game_t *g = &s_index.games[sel];
            if (store_is_installed(g)) {
                ui_status("Starting...", g->title);
                store_boot_game(); /* reboots */
            }
            ui_clear();
            ui_header();
            ui_center(120, g->title, 2, C_SEL);
            ui_center(150, "downloading", 1, C_DIM);
            if (store_install(g, progress_cb) == ESP_OK) {
                ui_status("Starting...", g->title);
                vTaskDelay(pdMS_TO_TICKS(400));
                store_boot_game(); /* reboots */
            }
            ui_status("INSTALL FAILED", "A: back to menu");
            for (;;) {
                if (keys_edges() & GF_KEY_A)
                    break;
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            menu_draw(sel);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

/* ---------------- entry ---------------- */

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    ESP_ERROR_CHECK(gf_lcd_init());
    ESP_ERROR_CHECK(gf_keys_init());
    gf_lcd_clear(C_BG);
    gf_lcd_backlight(60); /* modest: leave headroom for WiFi TX bursts */

    char ssid[33], pass[65];
    if (!wifi_creds_load(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "no creds -> provisioning");
        screen_provisioning(); /* never returns */
    }

    ui_status("Connecting to WiFi...", ssid);
    if (wifi_sta_connect(ssid, pass, 20000) != ESP_OK)
        screen_wifi_failed(ssid); /* never returns */

    ui_status("Loading game list...", NULL);
    if (store_fetch_index(&s_index) != ESP_OK) {
        ui_status("STORE UNREACHABLE", "A: retry");
        for (;;) {
            if (keys_edges() & GF_KEY_A)
                esp_restart();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

    screen_menu();
}
