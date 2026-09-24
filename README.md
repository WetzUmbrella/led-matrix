# led-matrix

Firmware for the INF2004 "Distributed LED Matrix Video Wall" project: a Pico W
coordinator wirelessly syncs an animation across a grid of Waveshare
ESP32-S3-Matrix boards (8x8 WS2812 each, GPIO14, 64 LEDs per board), which
together display one animation scaled and cropped to fill the whole grid.

> **Older single-node design:** an earlier ESP-NOW controller/peer approach
> (one board pushing quadrant data to the other three) was designed and
> compiled but never hardware-tested, then abandoned in favor of the Pico W
> design below. That code is preserved at tag [`v0-esp-now-single-node`](../../tree/v0-esp-now-single-node).

## Project Context

Original plan was a single 16x16 matrix; hardware issues forced a pivot to
**4x ESP32-S3-Matrix boards (8x8 each)**, tiled up to 2x2 (or fewer/more, see
below), to cover the same area. The team's focus is **inter-board sync**,
as the embedded-systems showcase for the course. A 3D-printed case/plate is
deferred.

## Architecture

```
        [Pico W]  sync master + WiFi access point "LEDWALL" (192.168.4.1)
            |  UDP broadcast beacon, 10x per second, port 4210
            |  (frame clock + layout: panel count, grid, who is in which slot)
   +--------+--------+--------+
 [P1]     [P2]     [P3]     [P4]    ESP32-S3-Matrix panels, all running the SAME firmware
            |  each panel sends a HELLO to the Pico every 0.3s, port 4211
```

- **Only sync messages go over WiFi.** Every panel stores the whole animation
  locally; each panel runs its own frame timer, and beacons just correct it,
  so a single lost packet doesn't freeze anything.
- **The Pico decides the layout** from which panels are alive (1 panel = 1x1,
  2 = 2x1, 3 = 3x1, 4 = 2x2, 5-6 = 3x2, 7-8 = 4x2) and **each panel crops its
  own piece** of the animation, scaled to fill the wall with no black bars.
- **Panel numbers are sticky:** the first panel to join stays #1 until it
  resets; a panel that reboots gets its old number back; a panel that leaves
  goes dark in its old slot rather than reshuffling everyone else.
- **Why not ESP-NOW for the coordinator:** the Pico W can't do ESP-NOW (an
  Espressif-only protocol), and the course wants the Pico in the design. If
  the Pico's own WiFi access point turns out unreliable, the fallback is
  Pico -> one ESP32 over UART -> ESP-NOW to the rest of the panels.

## Repo layout

```
led-matrix/
├── boardtest/       ESP32 board test: MAC, IMU check, colour cycle, walks a pixel across all 64 LEDs
├── ledwall_sync/     Pico W coordinator (Pico SDK): WiFi AP + UDP beacon + layout decisions
├── panel_sync/       ESP32 panel firmware (ESP-IDF): same firmware on every panel; receives sync, renders its own crop of the animation
└── README.md
```

Each of `boardtest/`, `ledwall_sync/`, `panel_sync/` is its own buildable
project with its own `CMakeLists.txt`. `build/`, `managed_components/`,
`sdkconfig`, `sdkconfig.old`, and `dependencies.lock` are all regenerated
locally per-project and gitignored.

## Prerequisites (one-time, per machine)

- WSL2 with an Ubuntu distro (developed on Ubuntu 20.04; 22.04/24.04 recommended for newer CMake/Python/GCC)
- [usbipd-win](https://github.com/dorssel/usbipd-win) on the Windows host, to pass each board's USB serial device into WSL
- ESP-IDF **v5.3** (shared across all ESP-IDF projects, installed outside this repo):
  ```bash
  mkdir -p ~/esp
  git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
  ~/esp/esp-idf/install.sh esp32s3
  echo "alias get_idf='. $HOME/esp/esp-idf/export.sh'" >> ~/.bashrc
  ```
  Run `get_idf` in every new terminal before `idf.py`. **Use v5.3 across the team so builds match.**
- Pico SDK (for `ledwall_sync/` only):
  ```bash
  mkdir -p ~/pico && cd ~/pico
  git clone --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
  echo 'export PICO_SDK_PATH=$HOME/pico/pico-sdk' >> ~/.bashrc
  ```
  Needs CMake 3.17+; Ubuntu 20.04 ships 3.16 — see `ledwall_sync/` build notes below if `cmake` is too old.

Clone this repo inside the WSL Linux filesystem (e.g. `~/esp-projects`) —
never under `/mnt/c` or a cloud-synced drive; cross-filesystem builds are
slow and sync tools can corrupt the build output.

## Build & flash

### Panels (`panel_sync/`, ESP-IDF)

```bash
get_idf
cd panel_sync
idf.py set-target esp32s3        # first time only
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with `Ctrl+]`. If flashing hangs at "Connecting...", hold
**BOOT**, tap **RESET**, release **BOOT**, retry. Pass each board's USB
device into WSL first via `usbipd bind`/`usbipd attach` (Windows admin
PowerShell) — binding is per physical board, attach is per session/replug.

### Pico W coordinator (`ledwall_sync/`, Pico SDK)

```bash
cd ledwall_sync
mkdir -p build && cd build
cmake .. && make -j4
```

Hold **BOOTSEL** on the Pico W, plug it in, drag `ledwall_sync.uf2` onto the
**RPI-RP2** drive. Check: onboard LED blinks every ~0.5s, and **LEDWALL**
shows up in WiFi scans.

### Board test (`boardtest/`, ESP-IDF)

```bash
get_idf
cd boardtest
idf.py -p /dev/ttyACM0 flash monitor
```

## Key settings

| Setting | Value |
|---|---|
| WiFi name / password | `LEDWALL` / `ledwall123` |
| Pico IP | 192.168.4.1 |
| Beacon port / HELLO port | 4210 / 4211 |
| Master clock | 30 fps |
| Panel timeout | 5000 ms |
| LED data pin | GPIO 14 |
| Brightness cap | `BRIGHT 8` (out of 255) |
| Animation | `panel_sync/main/gif_frames.h`, 32x32, 30 frames, 15 fps |

## Current Status / Next Steps

_(last updated 2026-09-24)_

- [x] ESP-IDF v5.3 + Pico SDK set up in WSL
- [x] USB passthrough via usbipd working
- [x] Board test: all boards flash-verified (MAC, IMU, all 64 LEDs)
- [x] Pico W as WiFi hub ("LEDWALL" access point) — working
- [x] Wireless sync, Pico -> panels (UDP broadcast) — working, 0 dropped beacons in logs
- [x] Red swirl animation stored on each panel — working
- [x] Auto-split: animation spreads across 1-4 panels — working
- [x] Sticky panel numbers — built, **needs a proper test**
- [ ] **Reliability fix** (5s timeout, panels only flash on their own slot/grid change) — built, compiles, **not yet tested on real hardware**
- [ ] Decide: grow the grid with panel count vs. fixed 2x2 with dark empty slots
- [ ] IMU orientation (QMI8658), Hall-sensor position detection, 4th panel for full 2x2, pushing new animations over WiFi

**To pick this up:** reflash the Pico and all panels with the latest
`ledwall_sync/main.c` and `panel_sync/main/panel_sync.c`, then watch panel
behavior against the table below.

| Panel shows | Meaning |
|---|---|
| Blinking red pixel | No beacon from the Pico yet |
| Blinking blue pixel | Hears the Pico, waiting for a number |
| Blue frame with a number (2s) | Number/slot just changed — place it there |
| Red swirl piece | Synced and playing |
| Solid red corner pixel | Lost the Pico for over 1s (still playing on its own clock) |

If it's still flaky after reflashing: panel log showing `lost LEDWALL,
retrying` means the WiFi link is dropping (Pico AP limits — consider the
UART+ESP-NOW fallback in Architecture above); a fresh boot in the log means
the panel itself is rebooting (check power/brightness); repeated
`LEAVE`/`JOIN` on the Pico means check-ins are being lost.
