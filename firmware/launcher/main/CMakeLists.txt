idf_component_register(SRCS main.c wifi_prov.c store.c
                       PRIV_REQUIRES gamefive nvs_flash esp_wifi esp_netif
                                     esp_http_server esp_http_client
                                     app_update esp_partition mbedtls)
