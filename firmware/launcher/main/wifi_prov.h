/* Game Five launcher — WiFi credentials + SoftAP provisioning */
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#define GF_PROV_AP_SSID "GameFive-Setup"
#define GF_PROV_URL     "http://192.168.4.1"

/* Loads saved credentials from NVS. Returns false if none saved. */
bool wifi_creds_load(char *ssid, size_t ssid_len, char *pass, size_t pass_len);

/* Erases saved credentials. */
void wifi_creds_clear(void);

/* Connects as STA with the given credentials. Blocks up to timeout_ms.
 * Returns ESP_OK once got an IP. */
esp_err_t wifi_sta_connect(const char *ssid, const char *pass, int timeout_ms);

/* Starts the SoftAP + web form and returns; the caller keeps its own loop.
 * When the user submits the form, credentials are saved to NVS and the
 * device REBOOTS. */
void wifi_provisioning_start(void);

/* Human-readable hint for the last STA connect failure ("" if none). */
const char *wifi_fail_hint(void);

/* Short status line for the LCD while provisioning is active. */
const char *wifi_prov_status(void);

/* Live phone-side activity ("network: X", "password: *** (n)") for the LCD,
 * fed by the provisioning page over WebSocket. */
const char *wifi_prov_activity(void);
