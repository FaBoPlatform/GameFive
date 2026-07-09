# Game Five

**A handheld game console fully engineered by an AI — Claude "Fable 5"**

![Game Five PCB (top view)](images/board_layout.png)

## About this project

Game Five is an experiment in **end-to-end AI hardware development**. The entire product is designed by **Fable 5** (Anthropic's Claude), directed through natural-language conversation by its human co-designers:

| Deliverable | Developed by Fable 5 | Status |
|---|---|---|
| **Schematic design** | Component selection (all JLCPCB in-stock parts), netlist, power tree, GPIO budgeting via 74HC165 | 🏭 1st prototype in fabrication |
| **PCB layout** | 120 × 61 mm 2-layer board — placement, autorouting, GND pours, stitching vias, DRC clean, fab outputs | 🏭 rev.1 prototype fabricated / rev.2i current (I2S audio restored, battery connector by the D-pad) |
| **Enclosure** | Two-piece 3D-printable case, generated parametrically through the Fusion 360 API | 🚧 In development |
| **Game software** | Board support component + games for ESP32-S3 (display, input, sound drivers) | 🚧 In development — HW test + SNAKE running on the XIAO |

The AI operated the EDA tool (EasyEDA Pro) and the CAD tool (Autodesk Fusion 360) programmatically through their APIs — placing parts, wiring nets, routing, running DRC, exporting Gerbers, and solid-modeling the case — while humans reviewed each iteration and steered the design ("make the D-pad symmetric", "move the display up", "the flat cable folds here").

The hardware itself: a Seeed Studio **XIAO ESP32-S3** driving a 2.0-inch **HS20HS072RX** TFT (ST7789, 240×320, SPI), with GameBoy-style controls and I2S audio.

## Features

- **MCU**: Seeed XIAO ESP32-S3 (Wi-Fi / BLE, USB-C, built-in LiPo charger)
- **Display**: HS20HS072RX 2.0" TFT, 240×320, ST7789, 4-wire SPI (LCSC **C5329582**)
- **Controls**: D-pad + A/B buttons (Panasonic EVQQ1D06M round-plunger tactile switches) + SELECT/START (Omron B3F-1025 through-hole tactile switches — orange round button, crisp 2.55 N click)
- **Audio**: **MAX98357A** I2S mono Class-D amplifier (U3) driving a speaker via a JST 1.25mm 2-pin connector (J2, LCSC **C10819**); SD_MODE pulled up (R12) so the amp is enabled by default, GAIN floated (9 dB)
- **Power**: LiPo battery (JST PH2.0) wired directly to the XIAO BAT± pads — charging is handled by the XIAO's onboard charger
- **Button matrix via 74HC165**: the XIAO exposes only 11 GPIOs, so the 8 buttons are read through a **74HC165** shift register sharing the SPI bus (LCD + buttons + backlight PWM all fit, with I2S pins to spare)
- **PCB**: 120 × 61 mm, 2-layer, ground pours on both sides tied together with ~190 stitching vias, DRC clean
- **Display connector under the panel**: the flat cable folds back at its root (the module's natural fold) and plugs into J3 mounted on the front, underneath the display — a clean rectangular board with no notch and no through-board pass
- **Enclosure**: two-piece 3D-printed case with 1 mm rounded edges — bottom tray with a 10 mm LiPo/speaker cavity and speaker grille, flat solid top cover (print it in clear material so the LCD shows through) with press-through holes for every key (Ø10 mm D-pad/A/B, Ø5 mm SELECT/START); four M2.5 screws pass through the top cover **and the PCB mounting holes** into the bottom bosses, clamping the whole stack together

## Repository contents

| Path | Contents |
|---|---|
| `fab/GameFive_Gerber.zip` | Gerber files for fabrication (JLCPCB, 2-layer) |
| `fab/GameFive_BOM.xlsx` | Bill of materials with LCSC part numbers (for JLCPCB PCBA) |
| `fab/GameFive_PickAndPlace.xlsx` | Pick-and-place data |
| `case/GameFive_Case_Bottom.stl` / `.step` | Enclosure bottom tray — 10 mm cavity under the PCB for the LiPo + speaker, speaker grille, drop-in USB-C slot |
| `case/GameFive_Case_Top.stl` / `.step` | Enclosure top cover — flat solid deck (clear material recommended), through-holes for all 8 keys |
| `case/case_bottom_fusion360.py` / `case_top_fusion360.py` | Fusion 360 API scripts that regenerate the case parametrically (verification-gated export) |
| `easyeda/GameFive.epro` | **EasyEDA Pro project file** (full schematic + PCB source) — open via File → Open in EasyEDA Pro |
| `firmware/components/gamefive/` | **Board support component** — ST7789 driver, 74HC165 buttons, backlight PWM, pin map, 8×8 font |
| `firmware/hwtest/` | **ESP-IDF hardware bring-up test** — display test patterns + live key test (see its README) |
| `firmware/snake/` | **SNAKE** — the first game (D-pad steer, score, speed-up, high score) |
| `images/` | Board renders |

More games will be added under `firmware/` as development progresses.

## Pin map

| XIAO pin | GPIO | Function |
|---|---|---|
| D0 | GPIO1 | LCD CS |
| D1 | GPIO2 | LCD DC (RS) |
| D2 | GPIO3 | LCD RST |
| D3 | GPIO4 | 74HC165 LATCH (SH/LD#) |
| D4 | GPIO5 | Backlight PWM (SI2302 low-side switch) |
| D5 | GPIO6 | I2S BCLK (MAX98357A) |
| D6 | GPIO43 | I2S LRCLK |
| D7 | GPIO44 | I2S DIN |
| D8 | GPIO7 | SPI SCK (shared by LCD and 74HC165) |
| D9 | GPIO8 | SPI MISO (74HC165 QH) |
| D10 | GPIO9 | SPI MOSI (LCD SDA) |

74HC165 inputs: A=UP, B=DOWN, C=LEFT, D=RIGHT, E=A, F=B, G=START, H=SELECT (10 kΩ pull-ups, active-low).

## Display connection

- The LCD flat cable **folds back at its root** (the module's natural fold, per the datasheet) and plugs into **J3 on the front side, underneath the display** (XKB X05A20L12T, 12-pin 0.5 mm bottom-contact flip connector; mouth toward the module's top edge).
- **J3 is wired in straight pin order** (J3 pin *n* = LCD pin *n*), with the connector rotated 180° to meet the folded tail.
- The silkscreen frame (51.8 × 36.2 mm) on the front marks the display mounting position.
- The display itself (C5329582) is **not included in the BOM** — order it separately.

## Ordering guide (JLCPCB)

1. Upload `fab/GameFive_Gerber.zip` (2-layer, 1.6 mm, any color).
2. For PCBA, upload `GameFive_BOM.xlsx` and `GameFive_PickAndPlace.xlsx`.
3. **U1 (XIAO ESP32-S3) is permanently out of stock at JLC** — either consign the part or mark it DNP and hand-solder it.
4. **J1 (battery connector) and SW7/SW8 (Omron B3F-1025) are through-hole** — not supported by Economic PCBA; use Standard PCBA or hand-solder them.
5. Order the display HS20HS072RX (C5329582) and a LiPo battery separately.
6. 3D-print the case STLs. The case is fastened with four M2.5 screws (~18 mm) that pass through the top cover and the PCB mounting holes into the bottom boss pilot holes — one screw stack holds top, board, and bottom together.

## License

This project is licensed under the **CERN Open Hardware Licence Version 2 – Permissive (CERN-OHL-P-2.0)**.
See [LICENSE](LICENSE) for the full text.

## Credits

- Schematic, PCB layout, enclosure, and game software: **Fable 5** (Claude, Anthropic)
- Co-designed and directed by **Akira & Tom**
