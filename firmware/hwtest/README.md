# Game Five — hardware bring-up test (`hwtest`)

ESP-IDF firmware that exercises the Game Five board: the ST7789 display and
all eight buttons. Flash this first on a freshly assembled prototype.

## What it does

1. **Display test** (runs automatically at boot)
   - backlight PWM fade-in
   - full-screen fills: RED → GREEN → BLUE → WHITE → BLACK (each labeled)
   - 8 color bars, 32-step grayscale gradient, 8 px checkerboard
   - 1 px border + crosshair + colored corner markers (edge alignment check)
   - "GAME FIVE" splash
2. **Key test** (runs forever afterwards)
   - on-screen indicators for UP/DOWN/LEFT/RIGHT/A/B/START/SELECT that
     light up while pressed
   - raw 74HC165 byte shown in hex + binary (for wiring bring-up)
   - every edge logged over USB serial (`idf.py monitor`)
   - hold **SELECT + START for 1.5 s** to re-run the display test

## Build & flash

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh   # or your ESP-IDF v5.3+/v6 install
cd firmware/hwtest
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodem*  flash monitor   # XIAO's USB-C (native USB)
```

## Bring-up notes

- **Colors inverted** (RED shows as CYAN)? Set `GF_LCD_INVERT 1` in
  `main/gamefive_pins.h`.
- **Image upside down**? Set `GF_LCD_ROTATE_180 0`.
- **Wrong keys light up**? The raw binary line at the bottom shows the shift
  register byte directly (bit7 = input H = SELECT … bit0 = input A = UP,
  0 = pressed); adjust the mapping in `main/keys.h` to match.
- All pins are defined in one place: `main/gamefive_pins.h`.
