/*
 * Game Five — SNAKE
 *
 * Controls:
 *   D-pad ......... steer
 *   START ......... start game / pause
 *   A ............. start game / restart after game over
 *   SELECT (hold) . back to title
 *
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"

#include "gamefive_pins.h"
#include "lcd.h"
#include "keys.h"

static const char *TAG = "snake";

#define W GF_LCD_H_RES            /* 240 */
#define H GF_LCD_V_RES            /* 320 */

#define CELL      8
#define HUD_H     24
#define COLS      (W / CELL)              /* 30 */
#define ROWS      ((H - HUD_H) / CELL)    /* 37 */
#define FIELD_Y   HUD_H
#define MAX_LEN   (COLS * ROWS)

#define TICK_MS          20
#define STEP_START_MS    180
#define STEP_MIN_MS      70
#define STEP_ACCEL_MS    5     /* faster by this much per food */

typedef struct { int8_t x, y; } cell_t;

typedef enum { DIR_UP, DIR_DOWN, DIR_LEFT, DIR_RIGHT } dir_t;

static const int8_t k_dx[] = { 0, 0, -1, 1 };
static const int8_t k_dy[] = { -1, 1, 0, 0 };

/* snake body as a ring buffer of cells; head_i points at the head */
static cell_t s_body[MAX_LEN];
static int s_head_i, s_len;
/* occupancy grid for O(1) self-collision + food placement */
static uint8_t s_occ[COLS][ROWS];

static uint32_t s_score, s_hiscore;

static inline cell_t body_at(int back)   /* back=0 → head */
{
    int i = s_head_i - back;
    while (i < 0) i += MAX_LEN;
    return s_body[i % MAX_LEN];
}

static void cell_fill(cell_t c, uint16_t color)
{
    gf_lcd_fill_rect(c.x * CELL, FIELD_Y + c.y * CELL, CELL, CELL, color);
}

static void draw_snake_cell(cell_t c, bool head)
{
    int x = c.x * CELL, y = FIELD_Y + c.y * CELL;
    gf_lcd_fill_rect(x, y, CELL, CELL, head ? GF_GREEN : GF_RGB(0, 180, 0));
    gf_lcd_fill_rect(x + 1, y + 1, CELL - 2, CELL - 2,
                     head ? GF_RGB(160, 255, 160) : GF_GREEN);
}

static void draw_food(cell_t c)
{
    int x = c.x * CELL, y = FIELD_Y + c.y * CELL;
    gf_lcd_fill_rect(x + 1, y + 1, CELL - 2, CELL - 2, GF_RED);
    gf_lcd_fill_rect(x + 2, y + 2, 2, 2, GF_ORANGE);
}

static void hud_draw(void)
{
    char buf[24];
    gf_lcd_fill_rect(0, 0, W, HUD_H, GF_BLACK);
    gf_lcd_fill_rect(0, HUD_H - 2, W, 2, GF_ORANGE);
    snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)s_score);
    gf_lcd_text(6, 4, buf, 2, GF_WHITE, GF_BLACK);
    snprintf(buf, sizeof(buf), "HI %lu", (unsigned long)s_hiscore);
    gf_lcd_text(W - 6 - gf_lcd_text_w(buf, 1), 8, buf, 1, GF_GRAY, GF_BLACK);
}

static cell_t food_place(void)
{
    cell_t c;
    do {
        c.x = (int8_t)(esp_random() % COLS);
        c.y = (int8_t)(esp_random() % ROWS);
    } while (s_occ[c.x][c.y]);
    draw_food(c);
    return c;
}

/* ---- key edge helper ---- */
static uint8_t s_prev_keys;
static uint8_t keys_pressed_edges(uint8_t *held_out)
{
    uint8_t now = gf_keys_read(NULL);
    uint8_t edges = (uint8_t)(now & ~s_prev_keys);
    s_prev_keys = now;
    if (held_out) *held_out = now;
    return edges;
}

/* ---------------------------------------------------------------- title */

static void title_screen(void)
{
    gf_lcd_clear(GF_BLACK);
    gf_lcd_rect(6, 6, W - 12, H - 12, 2, GF_GREEN);
    gf_lcd_text((W - gf_lcd_text_w("SNAKE", 6)) / 2, 80, "SNAKE", 6,
                GF_GREEN, GF_BLACK);

    /* little snake + food doodle */
    for (int i = 0; i < 7; i++) {
        cell_t c = { (int8_t)(8 + i), 18 };
        draw_snake_cell(c, i == 6);
    }
    draw_food((cell_t){ 18, 18 });

    gf_lcd_text((W - gf_lcd_text_w("PRESS START", 2)) / 2, 210,
                "PRESS START", 2, GF_WHITE, GF_BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "HI SCORE %lu", (unsigned long)s_hiscore);
    gf_lcd_text((W - gf_lcd_text_w(buf, 1)) / 2, 250, buf, 1,
                GF_GRAY, GF_BLACK);
    gf_lcd_text((W - gf_lcd_text_w("GAME FIVE", 1)) / 2, 300, "GAME FIVE", 1,
                GF_DKGRAY, GF_BLACK);

    ESP_LOGI(TAG, "title screen");
    for (;;) {
        uint8_t edges = keys_pressed_edges(NULL);
        if (edges & (GF_KEY_START | GF_KEY_A)) return;
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* ----------------------------------------------------------------- game */

static void game_over(void)
{
    ESP_LOGI(TAG, "game over, score=%lu", (unsigned long)s_score);
    if (s_score > s_hiscore) s_hiscore = s_score;

    /* flash the snake */
    for (int f = 0; f < 4; f++) {
        for (int i = 0; i < s_len; i++) {
            cell_fill(body_at(i), (f & 1) ? GF_GREEN : GF_RED);
        }
        vTaskDelay(pdMS_TO_TICKS(150));
    }

    int bw = 200, bh = 96;
    int bx = (W - bw) / 2, by = (H - bh) / 2;
    gf_lcd_fill_rect(bx, by, bw, bh, GF_BLACK);
    gf_lcd_rect(bx, by, bw, bh, 2, GF_RED);
    gf_lcd_text(bx + (bw - gf_lcd_text_w("GAME OVER", 2)) / 2, by + 14,
                "GAME OVER", 2, GF_RED, GF_BLACK);
    char buf[24];
    snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)s_score);
    gf_lcd_text(bx + (bw - gf_lcd_text_w(buf, 2)) / 2, by + 44, buf, 2,
                GF_WHITE, GF_BLACK);
    gf_lcd_text(bx + (bw - gf_lcd_text_w("A: RETRY", 1)) / 2, by + 76,
                "A: RETRY", 1, GF_GRAY, GF_BLACK);

    for (;;) {
        uint8_t edges = keys_pressed_edges(NULL);
        if (edges & (GF_KEY_A | GF_KEY_START)) return;
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

static void pause_loop(void)
{
    gf_lcd_text((W - gf_lcd_text_w("PAUSE", 3)) / 2, H / 2 - 12, "PAUSE", 3,
                GF_YELLOW, GF_BLACK);
    for (;;) {
        uint8_t edges = keys_pressed_edges(NULL);
        if (edges & GF_KEY_START) return;
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* returns when the run ends (game over or SELECT-hold back to title) */
static bool play(void) /* true = back to title requested */
{
    memset(s_occ, 0, sizeof(s_occ));
    s_score = 0;
    s_len = 3;
    s_head_i = s_len - 1;
    dir_t dir = DIR_UP;
    /* two-deep turn queue so quick successive taps (e.g. UP then LEFT
     * within one step) are both honored */
    dir_t dq[2];
    int dq_n = 0;

    gf_lcd_clear(GF_BLACK);
    hud_draw();

    for (int i = 0; i < s_len; i++) {
        cell_t c = { COLS / 2, (int8_t)(ROWS / 2 + (s_len - 1 - i)) };
        s_body[i] = c;
        s_occ[c.x][c.y] = 1;
        draw_snake_cell(c, i == s_len - 1);
    }
    cell_t food = food_place();

    int step_ms = STEP_START_MS;
    int elapsed = 0;
    int sel_hold = 0;

    ESP_LOGI(TAG, "run start");
    for (;;) {
        uint8_t held;
        uint8_t edges = keys_pressed_edges(&held);

        /* queue d-pad edges; each is validated against the direction the
         * snake will have when the turn applies (dir opposites are x^1) */
        static const struct { uint8_t key; dir_t d; } k_map[4] = {
            { GF_KEY_UP, DIR_UP }, { GF_KEY_DOWN, DIR_DOWN },
            { GF_KEY_LEFT, DIR_LEFT }, { GF_KEY_RIGHT, DIR_RIGHT },
        };
        for (int k = 0; k < 4; k++) {
            if (!(edges & k_map[k].key)) continue;
            dir_t base = dq_n ? dq[dq_n - 1] : dir;
            dir_t d = k_map[k].d;
            if (d != base && d != (base ^ 1) && dq_n < 2) dq[dq_n++] = d;
        }

        if (edges & GF_KEY_START) pause_loop();

        if (held == GF_KEY_SELECT) {
            if (++sel_hold >= 1500 / TICK_MS) return true;
        } else {
            sel_hold = 0;
        }

        elapsed += TICK_MS;
        if (elapsed >= step_ms) {
            elapsed = 0;
            if (dq_n > 0) {
                dir = dq[0];
                dq[0] = dq[1];
                dq_n--;
            }

            cell_t head = body_at(0);
            cell_t nh = { (int8_t)(head.x + k_dx[dir]),
                          (int8_t)(head.y + k_dy[dir]) };

            /* wall / self collision */
            if (nh.x < 0 || nh.x >= COLS || nh.y < 0 || nh.y >= ROWS ||
                s_occ[nh.x][nh.y]) {
                game_over();
                return false;
            }

            bool grow = (nh.x == food.x && nh.y == food.y);
            if (!grow) {
                cell_t tail = body_at(s_len - 1);
                s_occ[tail.x][tail.y] = 0;
                cell_fill(tail, GF_BLACK);
            } else {
                s_len++;
                s_score++;
                if (step_ms > STEP_MIN_MS) step_ms -= STEP_ACCEL_MS;
                hud_draw();
            }

            /* old head becomes body */
            draw_snake_cell(head, false);
            s_head_i = (s_head_i + 1) % MAX_LEN;
            s_body[s_head_i] = nh;
            s_occ[nh.x][nh.y] = 1;
            draw_snake_cell(nh, true);

            if (grow) food = food_place();
        }
        vTaskDelay(pdMS_TO_TICKS(TICK_MS));
    }
}

/* ----------------------------------------------------------------- main */

void app_main(void)
{
    ESP_LOGI(TAG, "Game Five SNAKE");
    ESP_ERROR_CHECK(gf_lcd_init());
    ESP_ERROR_CHECK(gf_keys_init());
    for (int p = 0; p <= 100; p += 5) {
        gf_lcd_backlight(p);
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    for (;;) {
        title_screen();
        while (!play()) {
            /* retry until the player asks for the title screen */
        }
    }
}
