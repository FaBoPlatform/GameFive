# Game Five Store

Games installable over WiFi from the on-device launcher.

## How it works

The launcher (factory partition) fetches `index.txt` from this directory via
GitHub raw, lists the games on screen, and installs the selected one into the
`ota_0` partition (game data goes to the `assets` partition). Hold **B+START
for 2 seconds** inside any game to return to the launcher.

## Index format

One line per game: `id|title|version|bin_url|assets_url|assets_size`
(`assets_url`/`assets_size` empty and 0 when the game has no data file).

## Adding your game

1. Build an ESP-IDF app for the XIAO ESP32-S3 using the shared
   `firmware/launcher/partitions.csv` table (app must fit the 2MB `ota_0`
   slot) and the `gamefive` BSP component.
2. Add the B+START exit combo (see `firmware/snake/main/main.c`).
3. Put the built `.bin` here and add a line to `index.txt` via pull request.
   Games written with AI assistance are very welcome — this whole console is
   an AI-development experiment.
