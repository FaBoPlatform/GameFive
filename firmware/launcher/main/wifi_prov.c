/* Game Five launcher — WiFi credentials (NVS) + SoftAP browser provisioning */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "wifi_prov.h"

static const char *TAG = "wifi_prov";

#define NVS_NS "gf"

/* ---------------- NVS credentials ---------------- */

bool wifi_creds_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return false;
    esp_err_t e1 = nvs_get_str(h, "ssid", ssid, &ssid_len);
    esp_err_t e2 = nvs_get_str(h, "pass", pass, &pass_len);
    nvs_close(h);
    return e1 == ESP_OK && e2 == ESP_OK && ssid[0] != '\0';
}

static void wifi_creds_save(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open(NVS_NS, NVS_READWRITE, &h));
    ESP_ERROR_CHECK(nvs_set_str(h, "ssid", ssid));
    ESP_ERROR_CHECK(nvs_set_str(h, "pass", pass));
    ESP_ERROR_CHECK(nvs_commit(h));
    nvs_close(h);
}

void wifi_creds_clear(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, "ssid");
        nvs_erase_key(h, "pass");
        nvs_commit(h);
        nvs_close(h);
    }
}

/* ---------------- STA connect ---------------- */

static EventGroupHandle_t s_ev;
#define EV_GOT_IP  BIT0
#define EV_FAILED  BIT1
static int s_retries;
static int s_last_reason;

const char *wifi_fail_hint(void)
{
    switch (s_last_reason) {
    case WIFI_REASON_NO_AP_FOUND:
        return "Network not found (2.4GHz only!)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "Wrong password?";
    default:
        return "";
    }
}

static void sta_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        s_last_reason = d ? d->reason : 0;
        ESP_LOGW(TAG, "disconnected, reason=%d", s_last_reason);
        if (s_retries++ < 6) {
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_ev, EV_FAILED);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_ev, EV_GOT_IP);
    }
}

esp_err_t wifi_sta_connect(const char *ssid, const char *pass, int timeout_ms)
{
    s_ev = xEventGroupCreate();
    s_retries = 0;

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h1, h2;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &sta_event, NULL, &h1));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &sta_event, NULL, &h2));

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    /* v1 board runs on marginal charger-backfed power: cap TX bursts */
    esp_wifi_set_max_tx_power(34); /* 8.5 dBm */

    EventBits_t bits = xEventGroupWaitBits(s_ev, EV_GOT_IP | EV_FAILED,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    if (bits & EV_GOT_IP) {
        ESP_LOGI(TAG, "connected to %s", ssid);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "connect to %s failed", ssid);
    return ESP_FAIL;
}

/* ---------------- SoftAP + web form + live connect test ----------------
 *
 * The SSID field is a <select> filled ONLY from the device's own scan —
 * the ESP32-S3 radio is 2.4GHz-only, so 5GHz networks can never appear.
 * Submitting runs a connect test while the AP stays up (APSTA); the
 * browser polls /status and sees success (then save+reboot) or the exact
 * failure reason. The LCD mirrors the state via wifi_prov_status().
 */

static enum { PS_IDLE, PS_CONNECTING, PS_OK, PS_FAIL } s_pstate = PS_IDLE;
static char s_pssid[33], s_ppass[65];
static int s_pretries;
static char s_status_lcd[48] = "waiting for phone...";

const char *wifi_prov_status(void)
{
    return s_status_lcd;
}

static char s_scan_options[1200]; /* <option> list from the last scan */

static void scan_networks(void)
{
    if (s_pstate == PS_CONNECTING)
        return;
    wifi_scan_config_t sc = { 0 };
    if (esp_wifi_scan_start(&sc, true) != ESP_OK)
        return;
    uint16_t n = 15;
    static wifi_ap_record_t recs[15];
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK)
        return;
    char *p = s_scan_options;
    char *end = s_scan_options + sizeof(s_scan_options) - 1;
    for (int i = 0; i < n && p < end - 80; i++) {
        if (recs[i].ssid[0] == '\0')
            continue;
        int dup = 0; /* same SSID broadcast by several APs */
        for (int j = 0; j < i; j++)
            if (!strcmp((char *)recs[i].ssid, (char *)recs[j].ssid))
                dup = 1;
        if (dup)
            continue;
        p += snprintf(p, end - p, "<option>%s</option>", recs[i].ssid);
    }
    *p = '\0';
    ESP_LOGI(TAG, "scanned %d networks", n);
}

static void prov_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = data;
        s_last_reason = d ? d->reason : 0;
        if (s_pstate == PS_CONNECTING) {
            if (s_pretries++ < 4) {
                esp_wifi_connect();
            } else {
                s_pstate = PS_FAIL;
                snprintf(s_status_lcd, sizeof(s_status_lcd), "FAILED: %s",
                         wifi_fail_hint()[0] ? wifi_fail_hint() : "no link");
                ESP_LOGW(TAG, "prov connect failed, reason=%d", s_last_reason);
            }
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        if (s_pstate == PS_CONNECTING) {
            s_pstate = PS_OK;
            wifi_creds_save(s_pssid, s_ppass);
            snprintf(s_status_lcd, sizeof(s_status_lcd), "CONNECTED! rebooting");
            ESP_LOGI(TAG, "prov connect OK, creds saved");
        }
    }
}

static esp_err_t root_get(httpd_req_t *req)
{
    ESP_LOGI(TAG, "GET %s", req->uri);

    /* iOS/Android captive-portal probes: answer "internet OK" so the phone
     * keeps the WiFi usable and the user can submit from a real browser. */
    if (strstr(req->uri, "hotspot-detect") || strstr(req->uri, "success")) {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req,
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD>"
            "<BODY>Success</BODY></HTML>", HTTPD_RESP_USE_STRLEN);
    }
    if (strstr(req->uri, "generate_204") || strstr(req->uri, "gen_204")) {
        httpd_resp_set_status(req, "204 No Content");
        return httpd_resp_send(req, NULL, 0);
    }

    static char page[2600];
    snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Game Five WiFi Setup</title>"
        "<style>body{font-family:sans-serif;margin:2em;background:#111;color:#eee}"
        "select,input,button{font-size:1.2em;padding:.4em;width:100%%;"
        "box-sizing:border-box;margin:.3em 0}"
        "button{background:#e33;color:#fff;border:0;padding:.6em}"
        "h1{color:#e33}a{color:#8cf}small{color:#999}</style></head><body>"
        "<h1>GAME FIVE</h1><p>WiFi setup</p>"
        "<form method='POST' action='/save'>"
        "<label>Network (2.4GHz only)</label>"
        "<select name='ssid' required>%s</select>"
        "<small>List shows only networks this console can use. "
        "<a href='/rescan'>Rescan</a></small>"
        "<label>Password</label>"
        "<input name='pass' type='password' maxlength='64'>"
        "<button type='submit'>Connect</button></form>"
        "</body></html>", s_scan_options);
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t rescan_get(httpd_req_t *req)
{
    scan_networks();
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    return httpd_resp_send(req, NULL, 0);
}

static esp_err_t status_get(httpd_req_t *req)
{
    static char page[1200];
    const char *body;
    char fail[300];

    switch (s_pstate) {
    case PS_CONNECTING:
        body = "<meta http-equiv='refresh' content='2'>"
               "<h1 style='color:#fc0'>Connecting...</h1>"
               "<p>Testing the connection. This page refreshes "
               "automatically.</p>";
        break;
    case PS_OK:
        body = "<h1 style='color:#4c4'>Connected!</h1>"
               "<p>Settings saved. Game Five is rebooting into the game "
               "store. You can close this page.</p>";
        break;
    case PS_FAIL:
        snprintf(fail, sizeof(fail),
                 "<h1 style='color:#e33'>Connection failed</h1>"
                 "<p><b>%s</b></p><p><a href='/'>Back to setup</a></p>",
                 wifi_fail_hint()[0] ? wifi_fail_hint()
                                     : "Could not reach the network.");
        body = fail;
        break;
    default:
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        return httpd_resp_send(req, NULL, 0);
    }

    snprintf(page, sizeof(page),
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Game Five WiFi Setup</title>"
        "<style>body{font-family:sans-serif;margin:2em;background:#111;"
        "color:#eee}a{color:#8cf}</style></head><body>%s</body></html>",
        body);
    httpd_resp_set_type(req, "text/html");
    esp_err_t err = httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);

    if (s_pstate == PS_OK) {
        vTaskDelay(pdMS_TO_TICKS(1500));
        esp_restart();
    }
    return err;
}

static int url_decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '+') {
            *w++ = ' ';
            r++;
        } else if (*r == '%' && r[1] && r[2]) {
            char hex[3] = { r[1], r[2], 0 };
            *w++ = (char)strtol(hex, NULL, 16);
            r += 3;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
    return 0;
}

static bool form_field(const char *body, const char *key, char *out, size_t n)
{
    char pat[16];
    snprintf(pat, sizeof(pat), "%s=", key);
    const char *p = strstr(body, pat);
    if (!p)
        return false;
    p += strlen(pat);
    size_t i = 0;
    while (*p && *p != '&' && i < n - 1)
        out[i++] = *p++;
    out[i] = '\0';
    url_decode(out);
    return true;
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[300] = { 0 };
    int len = httpd_req_recv(req, body, sizeof(body) - 1);
    ESP_LOGI(TAG, "POST %s (%d bytes)", req->uri, len);
    if (len <= 0)
        return httpd_resp_send_500(req);

    char ssid[33] = { 0 }, pass[65] = { 0 };
    if (!form_field(body, "ssid", ssid, sizeof(ssid)) || ssid[0] == '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "SSID required", HTTPD_RESP_USE_STRLEN);
    }
    form_field(body, "pass", pass, sizeof(pass));

    /* start the connect test — creds are saved only on success */
    strlcpy(s_pssid, ssid, sizeof(s_pssid));
    strlcpy(s_ppass, pass, sizeof(s_ppass));
    s_pretries = 0;
    s_last_reason = 0;
    snprintf(s_status_lcd, sizeof(s_status_lcd), "connecting: %s", ssid);
    ESP_LOGI(TAG, "prov connect test: %s", ssid);

    wifi_config_t wc = { 0 };
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &wc);
    s_pstate = PS_CONNECTING;
    esp_wifi_disconnect();
    esp_wifi_connect();

    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/status");
    return httpd_resp_send(req, NULL, 0);
}

void wifi_provisioning_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();
    esp_netif_create_default_wifi_sta(); /* APSTA so scanning works */

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t h1, h2;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &prov_event, NULL, &h1));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &prov_event, NULL, &h2));

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, GF_PROV_AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len = strlen(GF_PROV_AP_SSID);
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_set_max_tx_power(34); /* 8.5 dBm — see wifi_sta_connect */

    scan_networks();

    httpd_handle_t server = NULL;
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.uri_match_fn = httpd_uri_match_wildcard;
    ESP_ERROR_CHECK(httpd_start(&server, &hc));
    static httpd_uri_t u_save = { .uri = "/save", .method = HTTP_POST,
                                  .handler = save_post };
    static httpd_uri_t u_save2 = { .uri = "/*", .method = HTTP_POST,
                                   .handler = save_post };
    static httpd_uri_t u_status = { .uri = "/status", .method = HTTP_GET,
                                    .handler = status_get };
    static httpd_uri_t u_rescan = { .uri = "/rescan", .method = HTTP_GET,
                                    .handler = rescan_get };
    static httpd_uri_t u_root = { .uri = "/*", .method = HTTP_GET,
                                  .handler = root_get };
    httpd_register_uri_handler(server, &u_save);
    httpd_register_uri_handler(server, &u_save2);
    httpd_register_uri_handler(server, &u_status);
    httpd_register_uri_handler(server, &u_rescan);
    httpd_register_uri_handler(server, &u_root);

    ESP_LOGI(TAG, "provisioning AP '%s' up at %s", GF_PROV_AP_SSID, GF_PROV_URL);
    /* /status reboots on success; the caller keeps its own key loop */
}
