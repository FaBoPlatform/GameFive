# Game Five — SNAKE

The first game for Game Five: classic snake on a 30×37 grid.

- **D-pad** — steer
- **START / A** — start, retry, pause (START)
- **SELECT (hold 1.5 s)** — back to the title screen
- Eat food to grow; every food speeds the snake up a little.
- Session high score shown on the HUD and title screen.

## Build & flash

```bash
. ~/.espressif/v6.0.1/esp-idf/export.sh
cd firmware/snake
idf.py set-target esp32s3
idf.py -p /dev/cu.usbmodem* flash monitor
```

Board support (display, buttons, backlight, pins) lives in the shared
component [`firmware/components/gamefive`](../components/gamefive).
