/* Game Five launcher — game store client */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs.h"
#include "store.h"

static const char *TAG = "store";

#define NVS_NS "gf"
#define CHUNK 4096

/* ---------------- HTTP streaming download ---------------- */

typedef int (*sink_fn)(const void *data, int len, void *ctx);

static esp_err_t http_stream(const char *url, sink_fn sink, void *ctx,
                             int *content_len_out,
                             store_progress_cb progress, const char *phase)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = CHUNK,
    };
    esp_http_client_handle_t cl = esp_http_client_init(&cfg);
    if (!cl)
        return ESP_FAIL;

    esp_err_t err = esp_http_client_open(cl, 0);
    if (err != ESP_OK)
        goto out;
    int total = esp_http_client_fetch_headers(cl);
    int status = esp_http_client_get_status_code(cl);
    if (status != 200) {
        ESP_LOGE(TAG, "GET %s -> %d", url, status);
        err = ESP_FAIL;
        goto out;
    }
    if (content_len_out)
        *content_len_out = total;

    static char buf[CHUNK];
    int done = 0;
    for (;;) {
        int r = esp_http_client_read(cl, buf, sizeof(buf));
        if (r < 0) {
            err = ESP_FAIL;
            goto out;
        }
        if (r == 0)
            break;
        if (sink(buf, r, ctx) != 0) {
            err = ESP_FAIL;
            goto out;
        }
        done += r;
        if (progress)
            progress(phase, done, total > 0 ? total : 0);
    }
    err = ESP_OK;
out:
    esp_http_client_close(cl);
    esp_http_client_cleanup(cl);
    return err;
}

/* ---------------- index ---------------- */

typedef struct {
    char *buf;
    int len;
    int cap;
} membuf_t;

static int mem_sink(const void *data, int len, void *ctx)
{
    membuf_t *m = ctx;
    if (m->len + len >= m->cap)
        return -1;
    memcpy(m->buf + m->len, data, len);
    m->len += len;
    return 0;
}

esp_err_t store_fetch_index(store_index_t *out)
{
    static char raw[6144];
    membuf_t m = { .buf = raw, .len = 0, .cap = sizeof(raw) };
    esp_err_t err = http_stream(STORE_INDEX_URL, mem_sink, &m, NULL, NULL, NULL);
    if (err != ESP_OK)
        return err;
    raw[m.len] = '\0';

    out->count = 0;
    char *save = NULL;
    for (char *line = strtok_r(raw, "\r\n", &save);
         line && out->count < STORE_MAX_GAMES;
         line = strtok_r(NULL, "\r\n", &save)) {
        if (line[0] == '#' || line[0] == '\0')
            continue;
        /* id|title|version|bin_url|assets_url|assets_size */
        store_game_t *g = &out->games[out->count];
        memset(g, 0, sizeof(*g));
        char *f = line, *next;
        int i = 0;
        while (i < 6) {
            next = strchr(f, '|');
            if (next)
                *next = '\0';
            switch (i) {
            case 0: strlcpy(g->id, f, sizeof(g->id)); break;
            case 1: strlcpy(g->title, f, sizeof(g->title)); break;
            case 2: strlcpy(g->version, f, sizeof(g->version)); break;
            case 3: strlcpy(g->bin_url, f, sizeof(g->bin_url)); break;
            case 4: strlcpy(g->assets_url, f, sizeof(g->assets_url)); break;
            case 5: g->assets_size = atoi(f); break;
            }
            i++;
            if (!next)
                break;
            f = next + 1;
        }
        if (g->id[0] && g->bin_url[0])
            out->count++;
    }
    ESP_LOGI(TAG, "index: %d games", out->count);
    return out->count > 0 ? ESP_OK : ESP_FAIL;
}

/* ---------------- install ---------------- */

typedef struct {
    esp_ota_handle_t ota;
} ota_ctx_t;

static int ota_sink(const void *data, int len, void *ctx)
{
    ota_ctx_t *o = ctx;
    return esp_ota_write(o->ota, data, len) == ESP_OK ? 0 : -1;
}

typedef struct {
    const esp_partition_t *part;
    size_t off;
} part_ctx_t;

static int part_sink(const void *data, int len, void *ctx)
{
    part_ctx_t *p = ctx;
    if (p->off + len > p->part->size)
        return -1;
    if (esp_partition_write(p->part, p->off, data, len) != ESP_OK)
        return -1;
    p->off += len;
    return 0;
}

static void nvs_set_pair(const char *k1, const char *v1,
                         const char *k2, const char *v2)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, k1, v1);
        nvs_set_str(h, k2, v2);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool nvs_pair_matches(const char *k1, const char *v1,
                             const char *k2, const char *v2)
{
    char a[32] = { 0 }, b[32] = { 0 };
    size_t la = sizeof(a), lb = sizeof(b);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return false;
    esp_err_t e1 = nvs_get_str(h, k1, a, &la);
    esp_err_t e2 = nvs_get_str(h, k2, b, &lb);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK && !strcmp(a, v1) && !strcmp(b, v2);
}

bool store_is_installed(const store_game_t *g)
{
    return nvs_pair_matches("game_id", g->id, "game_ver", g->version);
}

bool store_has_game(void)
{
    char a[32];
    size_t la = sizeof(a);
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return false;
    esp_err_t e = nvs_get_str(h, "game_id", a, &la);
    nvs_close(h);
    return e == ESP_OK && a[0] != '\0';
}

esp_err_t store_install(const store_game_t *g, store_progress_cb progress)
{
    /* 1. assets (skip when unchanged — they can be several MB) */
    if (g->assets_url[0]) {
        if (!nvs_pair_matches("asset_id", g->id, "asset_ver", g->version)) {
            const esp_partition_t *ap = esp_partition_find_first(
                (esp_partition_type_t)0x42, (esp_partition_subtype_t)0x06, NULL);
            if (!ap) {
                ESP_LOGE(TAG, "assets partition missing");
                return ESP_FAIL;
            }
            if (g->assets_size <= 0 || g->assets_size > (int)ap->size) {
                ESP_LOGE(TAG, "bad assets_size %d", g->assets_size);
                return ESP_FAIL;
            }
            size_t erase = (g->assets_size + 0xFFF) & ~0xFFF;
            if (progress)
                progress("ERASING ASSETS", 0, 0);
            ESP_ERROR_CHECK(esp_partition_erase_range(ap, 0, erase));

            part_ctx_t pc = { .part = ap, .off = 0 };
            esp_err_t err = http_stream(g->assets_url, part_sink, &pc, NULL,
                                        progress, "ASSETS");
            if (err != ESP_OK)
                return err;
            if ((int)pc.off != g->assets_size)
                ESP_LOGW(TAG, "assets size mismatch: %u vs %d",
                         (unsigned)pc.off, g->assets_size);
            nvs_set_pair("asset_id", g->id, "asset_ver", g->version);
        } else {
            ESP_LOGI(TAG, "assets unchanged, skipping");
        }
    }

    /* 2. game binary into ota_0 */
    const esp_partition_t *op = esp_ota_get_next_update_partition(NULL);
    if (!op) {
        ESP_LOGE(TAG, "no ota partition");
        return ESP_FAIL;
    }
    ota_ctx_t oc;
    esp_err_t err = esp_ota_begin(op, OTA_SIZE_UNKNOWN, &oc.ota);
    if (err != ESP_OK)
        return err;
    err = http_stream(g->bin_url, ota_sink, &oc, NULL, progress, "GAME");
    if (err != ESP_OK) {
        esp_ota_abort(oc.ota);
        return err;
    }
    err = esp_ota_end(oc.ota); /* validates the image */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_end: %s", esp_err_to_name(err));
        return err;
    }
    nvs_set_pair("game_id", g->id, "game_ver", g->version);
    ESP_LOGI(TAG, "installed %s %s", g->id, g->version);
    return ESP_OK;
}

void store_boot_game(void)
{
    const esp_partition_t *op = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!op)
        return;
    if (esp_ota_set_boot_partition(op) == ESP_OK)
        esp_restart();
}
