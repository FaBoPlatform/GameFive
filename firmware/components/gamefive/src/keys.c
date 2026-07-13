/*
 * Game Five — 74HC165 button reader (shares the LCD SPI bus)
 *
 * Wiring (Game Five PCB rev.1):
 *   SH/LD# (pin 1)  <- GPIO4, pulse LOW to load the 8 button states
 *   CP     (pin 2)  <- shared SPI SCK (GPIO7); CLK INH (pin 15) tied to GND
 *   QH     (pin 9)  -> SPI MISO (GPIO8)
 *   Inputs A..H     <- buttons, 10k pull-ups, pressed = LOW
 *
 * After a load pulse QH presents input H; each rising CP edge shifts the
 * next stage out (G, F, ... A). Reading one byte MSB-first therefore gives
 *   bit7=H bit6=G bit5=F bit4=E bit3=D bit2=C bit1=B bit0=A
 * which conveniently mirrors into the gf_key_t bit order (bit0=A=UP).
 *
 * The LCD shares SCK: its transactions also clock the HC165, but that only
 * scrambles the shift register — every read starts with a fresh load pulse,
 * and the SPI master driver serializes bus access between the two devices.
 *
 * SPDX-License-Identifier: CERN-OHL-P-2.0
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "rom/ets_sys.h"
#include "esp_log.h"
#include "esp_check.h"

#include "gamefive_pins.h"
#include "keys.h"

static const char *TAG = "gf_keys";

const char *const gf_key_names[GF_KEY_COUNT] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "START", "SELECT",
};

static spi_device_handle_t s_dev;

esp_err_t gf_keys_init(void)
{
    gpio_config_t latch_cfg = {
        .pin_bit_mask = 1ULL << GF_PIN_KEYS_LATCH,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&latch_cfg), TAG, "latch gpio");
    gpio_set_level(GF_PIN_KEYS_LATCH, 1); /* idle high = shift mode */

    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 2 * 1000 * 1000,
        .mode = 0,             /* HC165 shifts on rising edge; its propagation
                                  delay means mode-0 sampling reads the
                                  pre-shift bit — the standard arrangement. */
        .spics_io_num = -1,    /* no CS; SH/LD# is driven manually */
        .queue_size = 2,
    };
    ESP_RETURN_ON_ERROR(spi_bus_add_device(GF_SPI_HOST, &dev_cfg, &s_dev),
                        TAG, "add device");
    ESP_LOGI(TAG, "74HC165 ready (latch GPIO%d)", GF_PIN_KEYS_LATCH);
    return ESP_OK;
}

uint8_t gf_keys_read(uint8_t *raw_out)
{
    /* Own the bus so the load pulse and the shift-out are atomic with
     * respect to LCD transfers on the same SCK. */
    ESP_ERROR_CHECK(spi_device_acquire_bus(s_dev, portMAX_DELAY));

    gpio_set_level(GF_PIN_KEYS_LATCH, 0); /* parallel load */
    ets_delay_us(2);
    gpio_set_level(GF_PIN_KEYS_LATCH, 1); /* back to shift mode */
    ets_delay_us(1);

    spi_transaction_t t = {
        .flags = SPI_TRANS_USE_RXDATA,
        .rxlength = 8,
        .length = 8,
    };
    esp_err_t err = spi_device_polling_transmit(s_dev, &t);
    spi_device_release_bus(s_dev);
    ESP_ERROR_CHECK(err);

    uint8_t raw = t.rx_data[0]; /* bit7=H .. bit0=A, 1 = released */
    if (raw_out) *raw_out = raw;

    /* raw 0x00 = all 8 keys "pressed" at once: physically implausible — it
     * means the shift register isn't driving MISO (board not fitted, loose
     * connection). Treat as no input, or phantom keys fire app actions
     * (a stuck B+START combo kept kicking DOOM back to the launcher). */
    if (raw == 0x00)
        raw = 0xFF;

    /* Debounce: a change is accepted only after two consecutive identical
     * samples (~40 ms at the apps' 50 Hz poll), filtering both mechanical
     * bounce and noise from a floating/unfitted shift register. */
    static uint8_t s_stable = 0, s_last = 0xEE; /* 0xEE: impossible first value */
    uint8_t sample = (uint8_t)~raw; /* 1 = pressed, bit0=A=UP .. bit7=H=SELECT */
    if (sample == s_last) {
        s_stable = sample;
    }
    s_last = sample;
    return s_stable;
}
