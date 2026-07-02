# Game Five

**A handheld game console fully engineered by an AI — Claude "Fable 5"**

![Game Five PCB (3D render)](images/board_3d.png)

## About this project

Game Five is an experiment in **end-to-end AI hardware development**. The entire product is designed by **Fable 5** (Anthropic's Claude), directed through natural-language conversation by its human co-designers:

| Deliverable | Developed by Fable 5 | Status |
|---|---|---|
| **Schematic design** | Component selection (all JLCPCB in-stock parts), netlist, power tree, GPIO budgeting via 74HC165 | 🏭 1st prototype in fabrication |
| **PCB layout** | 120 × 85 mm 2-layer board — placement, autorouting, GND pours, ~325 stitching vias, DRC clean, fab outputs | 🏭 1st prototype in fabrication |
| **Enclosure** | Two-piece 3D-printable case, generated parametrically through the Fusion 360 API | 🚧 In development |
| **Game software** | Game framework + games for ESP32-S3 (display, input, sound drivers) | 🚧 In development |

The AI operated the EDA tool (EasyEDA Pro) and the CAD tool (Autodesk Fusion 360) programmatically through their APIs — placing parts, wiring nets, routing, running DRC, exporting Gerbers, and solid-modeling the case — while humans reviewed each iteration and steered the design ("make the D-pad symmetric", "move the display up", "the flat cable folds here").

The hardware itself: a Seeed Studio **XIAO ESP32-S3** driving a 2.0-inch **HS20HS072RX** TFT (ST7789, 240×320, SPI), with GameBoy-style controls and I2S audio.

## Features

- **MCU**: Seeed XIAO ESP32-S3 (Wi-Fi / BLE, USB-C, built-in LiPo charger)
- **Display**: HS20HS072RX 2.0" TFT, 240×320, ST7789, 4-wire SPI (LCSC **C5329582**)
- **Controls**: D-pad + A/B buttons (Panasonic EVQQ1D06M round-plunger tactile switches) + SELECT/START (compact tactile switches)
- **Audio**: MAX98357A I2S amplifier + 8 Ω speaker (JST PH2.0 connector)
- **Power**: LiPo battery (JST PH2.0) wired directly to the XIAO BAT± pads — charging is handled by the XIAO's onboard charger
- **Button matrix via 74HC165**: the XIAO exposes only 11 GPIOs, so the 8 buttons are read through a **74HC165** shift register sharing the SPI bus (LCD + buttons + I2S audio + backlight PWM all fit)
- **PCB**: 120 × 85 mm, 2-layer, ground pours on both sides tied together with ~325 stitching vias, DRC clean
- **FPC notch**: a 26 mm wide notch in the top board edge — the display's flat cable folds back at the module edge, passes through the notch, and plugs into the FPC connector (J3) on the back side
- **Enclosure**: two-piece 3D-printed case (bottom tray + top lid) with a cross-shaped D-pad cutout and a raised clear window island over the display

## Repository contents

| Path | Contents |
|---|---|
| `fab/GameFive_Gerber.zip` | Gerber files for fabrication (JLCPCB, 2-layer) |
| `fab/GameFive_BOM.xlsx` | Bill of materials with LCSC part numbers (for JLCPCB PCBA) |
| `fab/GameFive_PickAndPlace.xlsx` | Pick-and-place data |
| `case/GameFive_Case_Bottom.stl` | Enclosure bottom tray (3D printing) |
| `case/GameFive_Case_Top.stl` | Enclosure top lid (clear material recommended) |
| `case/GameFive_Case_Assembly.step` | Enclosure assembly STEP model |
| `case/case_rebuild_fusion360.py` | Fusion 360 API script that regenerates the case parametrically |
| `easyeda/GameFive.epro` | **EasyEDA Pro project file** (full schematic + PCB source) — open via File → Open in EasyEDA Pro |
| `images/` | Board renders |

Game firmware will be added under `firmware/` as development progresses.

![PCB layout](images/board_layout.png)

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

- The LCD flat cable **folds back at the top edge of the module**, passes through the 26 mm notch in the top board edge, and inserts into **J3 on the back side** (XKB X05A20L12T, 12-pin 0.5 mm bottom-contact flip connector).
- Because the fold mirrors the cable, **J3 is wired in reverse pin order** (J3 pin *n* = LCD pin *13 − n*).
- The silkscreen frame (51.8 × 36.2 mm) on the front marks the display mounting position.
- The display itself (C5329582) is **not included in the BOM** — order it separately.

## Ordering guide (JLCPCB)

1. Upload `fab/GameFive_Gerber.zip` (2-layer, 1.6 mm, any color).
2. For PCBA, upload `GameFive_BOM.xlsx` and `GameFive_PickAndPlace.xlsx`.
3. **U1 (XIAO ESP32-S3) is permanently out of stock at JLC** — either consign the part or mark it DNP and hand-solder it.
4. **J1 (battery connector, through-hole)** is not supported by Economic PCBA — use Standard PCBA or hand-solder.
5. Order the display HS20HS072RX (C5329582), a LiPo battery, and an 8 Ω speaker separately.
6. 3D-print the case STLs (clear resin/filament recommended for the top lid). The case is fastened with four M2.5 screws.

## License

This project is licensed under the **CERN Open Hardware Licence Version 2 – Permissive (CERN-OHL-P-2.0)**.
See [LICENSE](LICENSE) for the full text.

## Credits

- Schematic, PCB layout, enclosure, and game software: **Fable 5** (Claude, Anthropic)
- Co-designed and directed by **Akira & Tom**
