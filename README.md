# Mochi ESP32-C3 I2C OLED

**English** | [简体中文](README.zh-CN.md)

Play Dasai Mochi emote animations on an **ESP32-C3 SuperMini** with a **4-pin I2C OLED (SSD1306 128x64)**, featuring touch-to-pat interaction, haptic feedback, and two built-in single-button mini games.

> This project is a derivative port of [huykhoong/esp32_dasai_mochi_clone_and_how_to](https://github.com/huykhoong/esp32_dasai_mochi_clone_and_how_to). The original project drives a TFT/OLED with the U8g2 library and provides the idea and assets for converting GIFs into C++ arrays via [gif2cpp](https://huykhong.com/IOT/gif2cpp). This repository adapts that idea to a 4-pin I2C OLED and extends it with touch interaction and games. **Original concept, assets, and the GIF-to-C++ pipeline belong to the original author — with many thanks.**

---

## Features

- **Emote playback**: GIFs converted frame-by-frame into 1 bpp data stored in flash.
- **Touch-to-pat**: touching the TTP223 module plays the "laugh" emote and buzzes the vibration motor; releasing stops both instantly (per-frame interrupt for snappy response).
- **Random emotes**: while idle, mostly "angry", with ~1-in-10 "embarrassed" and ~1-in-10 "proud".
- **Haptic feedback**: PWM-driven vibration intensity (300 Hz — a low frequency is required to produce sustained torque on an inductive load).
- **Game menu**: long-press for 3 s to enter the menu with two single-button games:
  - **FLAPPY BIRD**: hold to rise, release to fall; score by passing through gaps.
  - **JUMP JUMP**: hold to charge, release to jump; score by landing on the next block.

---

## Hardware

| Component | Notes |
|-----------|-------|
| ESP32-C3 SuperMini | MCU |
| 0.96" 4-pin I2C OLED module | SSD1306, 128x64, I2C address `0x3C` |
| TTP223 capacitive touch module | Momentary mode (default), active-high |
| Vibration motor + MOS driver board | High = on, low = off |
| Breadboard + jumper wires | — |

---

## Wiring

### 4-pin OLED (I2C)

```
4-pin OLED          ESP32-C3 SuperMini
──────────────────────────────────────
GND              →  GND
VCC              →  3.3V
SCL              →  GPIO6
SDA              →  GPIO7
```

### TTP223 touch module

```
TTP223             ESP32-C3 SuperMini
──────────────────────────────────────
VCC              →  3.3V
GND              →  GND
I/O (output)     →  GPIO1
```

### Vibration motor (with MOS driver board)

```
Motor driver       ESP32-C3 SuperMini
──────────────────────────────────────
VCC              →  5V
GND              →  GND
IN (control)     →  GPIO3
```

> **Common ground is required**: the GND of the OLED, touch module, and motor driver must all connect to the ESP32's GND.
> The 4-pin I2C module has no RES/DC/CS pins — nothing else to wire.

---

## Build & Flash

Requires [PlatformIO](https://platformio.org/) (VSCode extension or CLI). The first build downloads the `espressif32` platform and toolchain automatically.

```bash
# Build + flash the main firmware
pio run -e esp32c3 -t upload

# Open the serial monitor
pio device monitor -b 115200
```

If `pio` is not in your PATH (PlatformIO installed under `~/.platformio`), use the full path:

```bash
~/.platformio/penv/bin/pio run -e esp32c3 -t upload
```

### I2C address scan (optional diagnostic)

If the screen stays dark, flash the scanner first to confirm the OLED is on the bus and detect its address:

```bash
~/.platformio/penv/bin/pio run -e scan -t upload
~/.platformio/penv/bin/pio device monitor -b 115200
```

You should see `FOUND device at 0x3C`. If no device is found, check the SCL/SDA wiring and power.

---

## Controls

| State | Action | Effect |
|-------|--------|--------|
| Idle | no touch | play angry (occasionally embarrassed / proud) |
| Idle | short tap | play "laugh" + vibrate (stops on release) |
| Idle | **long-press 3 s** | enter the game menu |
| Menu | short tap | move the cursor to the next game |
| Menu | long-press 2 s | enter the highlighted game |
| Flappy | hold / release | bird rises / falls; score through gaps |
| Jump | hold / release | charge / jump; score by landing on the next block |
| Game over | short tap | retry |
| Game over | long-press 3 s | back to idle (emote) mode |

---

## Tunable Parameters

All defined at the top of [`src/main.cpp`](src/main.cpp):

**Flappy Bird**
```c
#define PIPE_GAP     24   // gap height (smaller = harder)
#define PIPE_SPACING 52   // distance between pipes (smaller = denser)
#define FRAME_MS     32   // ms per frame (smaller = faster)
```

**Jump Jump**
```c
#define JUMP_GAP_MIN  8   // minimum block spacing
#define JUMP_GAP_VAR  6   // spacing randomness
#define JUMP_MAX_CHARGE 20 // max charge
```

**Vibration strength**: the `45` in `setVibrate(45)` is a duty-cycle percentage (0–100, lower = weaker).

---

## Implementation Notes

- **Hardware I2C**: uses the Arduino `Wire` library, `Wire.begin(SDA, SCL)` at 400 kHz. **Do not call `pinMode()` on SCL/SDA after `Wire.begin()`** — it overrides the I2C peripheral configuration and the screen will stay dark.
- **Full-page refresh**: `OLED_Refresh()` sends `0x40 + 128 bytes` per page in a single I2C transaction (`Wire.setBufferSize(160)` prevents overflow), cutting the 8 pages from 32 transactions to 8 for a significant speed-up.
- **GIF data format**: 1 bpp, row-major, MSB-first. Each frame is exactly 1024 bytes (128 x 64 / 8). The arrays are `const` (stored in flash) — do not drop `const`.
- **`ANIMATED_GIF_DEFINED` guard**: every GIF header carries this guard so multiple headers can be included in one translation unit. Do not remove it.

---

## Project Layout

```
esp32c3_4pin_oled/
├── platformio.ini          # PlatformIO config (main + scan environments)
├── src/
│   ├── main.cpp            # Main firmware (display driver + animations + touch + games)
│   └── scan.cpp            # I2C address scanner (diagnostic)
└── include/
    ├── font.h              # ASCII / Chinese font tables
    ├── angry.h             # Emote animation data (GIF → C++)
    ├── laugh.h
    ├── relaxed.h
    ├── embarrassed.h
    ├── proud.h
    ├── daichi_gundam.h     # Animations from the original open-source project
    └── daichi_intro.h
```

---

## Credits & License

- **Original project**: [huykhoong/esp32_dasai_mochi_clone_and_how_to](https://github.com/huykhoong/esp32_dasai_mochi_clone_and_how_to)
- **GIF-to-C++ tool**: [gif2cpp](https://huykhong.com/IOT/gif2cpp)
- **Character rights**: the Dasai Mochi character belongs to its original creator; this project is for learning and non-commercial use only.
- **OLED driver reference**: Zhongjingyuan Electronics (SSD1306 driver and font tables).
