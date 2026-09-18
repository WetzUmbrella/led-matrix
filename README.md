# led-matrix

Firmware for a 2x2 grid of Waveshare ESP32-S3-Matrix boards (8x8 WS2812
each, GPIO14, 64 LEDs per board) that together display one synced animation
across a 16x16 canvas. Built with ESP-IDF v5.3.

Scope: LED matrix + inter-board sync only. Each board's onboard QMI8658 IMU
(I2C GPIO11/12) is not used here.

## Project Context

This is a building block for a larger "Distributed LED Matrix Video Wall"
project: the original plan was a single 16x16 matrix, but hardware issues
forced a pivot to **4x ESP32-S3-Matrix boards (8x8 each), tiled 2x2** to
cover the same 16x16 area, mounted in a 3D-printed case/plate (planned for
later). The team's current focus is getting the 4 boards **synced at
60fps**, as the embedded-systems showcase for the course; the case and
other polish are deferred.

## Architecture

One board is configured as the **controller**, the other three as **peers**:

- The controller generates the shared animation (currently: a single dot
  walking across all 256 positions of the 16x16 canvas, raster order, one
  step every frame tick) at a fixed 60fps tick driven by an `esp_timer`
  periodic timer + a FreeRTOS task notification.
- Each tick, the controller splits the 16x16 frame into 4 quadrant buffers
  (8x8 RGB each) and **unicasts** the 3 quadrants it doesn't own to the
  other boards over **ESP-NOW** (no WiFi AP/router needed), then renders
  its own quadrant locally.
- Each peer just renders whatever quadrant packet it receives, as soon as
  it arrives — no separate timer on peers.
- A single lit pixel walking across the whole grid makes any sync problem
  immediately visible: it should cross quadrant boundaries smoothly, with
  no stutter, jump, or duplicate/missing frame at the seams. That's
  deliberate — the point of this pattern is to expose timing bugs, not
  hide them.
- ESP-NOW packets are tiny (197 bytes: a frame sequence number + quadrant
  ID + 192 bytes of pixel data), well under ESP-NOW's 250-byte limit, and
  WiFi modem sleep is disabled so delivery latency stays well under the
  16.7ms (60fps) frame budget.

**Not yet validated on real hardware** (no boards were available while
writing this) — see the Status section below.

Key files: `main/led-matrix.c` (app_main, role branching, the frame/render
loop), `main/led_matrix.{c,h}` (thin LED-strip driver wrapper), `main/
esp_now_sync.{c,h}` (ESP-NOW init, peer registration, send/receive),
`main/Kconfig.projbuild` (per-board role/position/peer-MAC configuration).

## What it does

Lights a single pixel at ~10% brightness (dim white) and walks it through
all 64 LED positions in raster order, looping forever, one step every 100ms.

## Prerequisites (one-time, per machine)

- WSL2 with an Ubuntu distro (tested on Ubuntu 20.04)
- [usbipd-win](https://github.com/dorssel/usbipd-win) on the Windows host, to pass the board's USB serial device into WSL
- `gh` (GitHub CLI), if you want to push changes

### 1. Install build prerequisites (inside WSL)

```bash
sudo apt update && sudo apt install -y git wget flex bison gperf python3 python3-pip python3-venv python3.8-venv cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0
```

### 2. Install ESP-IDF v5.3 (once, shared across all your ESP-IDF projects -- NOT inside this repo)

```bash
mkdir -p ~/esp
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32s3
echo "alias get_idf=\". \$HOME/esp/esp-idf/export.sh\"" >> ~/.bashrc
source ~/.bashrc
```

### 3. Clone this repo (inside the WSL Linux filesystem, e.g. `~/esp-projects` -- never under `/mnt/c` or a cloud-synced drive; cross-filesystem builds are slow and sync tools can corrupt the build directory)

```bash
mkdir -p ~/esp-projects
cd ~/esp-projects
git clone <this-repo-url> led-matrix
cd led-matrix
get_idf
idf.py set-target esp32s3
```

`idf_component.yml` (already committed in `main/`) declares the
`espressif/led_strip` dependency; the component itself is NOT committed --
it downloads automatically into `managed_components/` on your first build.

### 3b. Configure this board's role (do this separately, per physical board)

Each of the 4 boards needs its own build config -- run this once per board,
on whichever machine is about to flash it:

```bash
idf.py menuconfig
```

Go to **LED Matrix Video Wall** and set:

- **Board role**: exactly one board is `Controller`, the other three are `Peer`.
- **This board's position**: which quadrant of the 16x16 grid this board covers.
- **WiFi channel**: must be the *same* number on all 4 boards.
- On the **controller only**: the MAC address of each of the other 3 boards
  (grab each board's MAC from its boot log, e.g. `idf.py monitor`, or
  `esp_read_mac()` -- the entry for the controller's own position is
  ignored). Get all 3 MACs before doing this step.

This writes to the local, gitignored `sdkconfig` -- it's intentionally not
committed, since it's different per physical board.

### 4. Plug in the board and pass it into WSL (Windows admin PowerShell)

```powershell
usbipd list
usbipd bind --busid <BUSID>
usbipd attach --wsl --busid <BUSID>
```

Confirm it shows up inside WSL:

```bash
ls /dev/ttyACM0
```

You'll need to re-run the `usbipd attach` command every time you unplug and
replug the board, or reboot.

### 5. Build, flash, and watch it run

```bash
get_idf
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Exit the monitor with `Ctrl+]`.

If flashing hangs at "Connecting...", hold **BOOT**, tap **RESET**, then
release **BOOT** to force download mode and retry.

## Repo layout

```
led-matrix/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild
│   ├── idf_component.yml
│   ├── led-matrix.c
│   ├── led_matrix.{c,h}
│   └── esp_now_sync.{c,h}
├── sdkconfig.defaults
└── README.md
```

`build/`, `managed_components/`, `sdkconfig`, `sdkconfig.old`, and
`dependencies.lock` are all regenerated locally and are gitignored.

## Current Status / Next Steps

_(last updated 2026-09-18)_

- [x] ESP-IDF v5.3 installed at `~/esp/esp-idf` (WSL), `get_idf` alias set up
- [x] Project scaffolded, target set to `esp32s3`, `led_strip` dependency added
- [x] Single-board walking-dot LED code written and **flash-verified working** (`main/led-matrix.c`, before the sync rewrite below)
- [x] Repo pushed to GitHub, public
- [x] ESP-NOW-based controller/peer sync architecture designed and implemented (`main/esp_now_sync.{c,h}`, `main/led_matrix.{c,h}`, `main/Kconfig.projbuild`, rewritten `main/led-matrix.c`)
- [x] Both the controller build and the peer build compile cleanly (`idf.py build`, verified by toggling `CONFIG_LM_ROLE_*` and rebuilding)
- [x] **Single-board Controller-role smoke test run on real hardware and visually verified** — one board flashed as Controller with all 4 peer-MAC fields set to distinct dummy values, dot renders correctly on the board's own quadrant. Sends to the 3 fake peers fail harmlessly (logged `esp_now_send failed` warnings), as designed.
- [ ] **Multi-board (4-board) flow still unverified** — role/position/real-peer-MAC `menuconfig` config, flashing all 4, ESP-NOW pairing, and whether it actually holds 60fps without tearing at the seams.

**Known bug found during single-board testing:** `lm_espnow_register_peers()` in `main/esp_now_sync.c` loops over all 4 grid positions and calls `esp_now_add_peer()` for each, wrapped in `ESP_ERROR_CHECK()`. If two positions share the same MAC (e.g. all 4 left at the default placeholder `AA:AA:AA:AA:AA:AA`, as happens by default on an unconfigured board), the second `esp_now_add_peer()` call returns `ESP_ERR_ESPNOW_EXIST`, and `ESP_ERROR_CHECK` aborts, causing an infinite crash-reboot loop with **no board-level symptom other than "nothing happens."** The Kconfig help text says a controller's own-position peer entry is "ignored," but the code never actually skips registering a peer for `OWN_POSITION` — it should, both to honor that comment and to avoid this crash. Workaround used for single-board testing: give all 4 peer-MAC fields distinct dummy values. **TODO:** fix `lm_espnow_register_peers()` to skip `OWN_POSITION`, and/or tolerate `ESP_ERR_ESPNOW_EXIST` instead of hard-aborting.

**To pick this up (same machine or a second machine, e.g. a PC after setting it up on a laptop):**

1. If this is a machine that hasn't been set up before: work through Prerequisites → step 3b above first (WSL2+Ubuntu, `usbipd-win`, ESP-IDF v5.3 install, clone this repo, `set-target esp32s3`, `idf.py build`). Machine-level installs (WSL, `usbipd-win`, ESP-IDF itself, `gh` CLI) are per-machine and are **not** carried by git — each new machine needs them installed fresh; only this repo's contents come from `git clone`.
2. With all 4 boards in hand: flash each once as **Peer** (default config, just to read its MAC from the boot log via `idf.py monitor`), collecting all 4 MACs.
3. Pick one board as **Controller**: run `idf.py menuconfig`, set its role/position, plug in the other 3 boards' MACs, reflash it.
4. Reconfigure and reflash the other 3 as **Peer**, each with its own distinct grid position.
5. Power all 4 simultaneously and confirm visually: a single dim (~10% brightness) pixel should walk smoothly across the full 16x16 grid, including across the seams between boards, with no stutter/jump/duplicate. If it hangs at "Connecting..." while flashing, hold BOOT, tap RESET, release BOOT, retry.
6. If seams visibly desync or drop below 60fps, that's the next debugging target — check ESP-NOW send failures in the controller's log first (`esp_now_send failed` warnings), then timing jitter in the render loop.
7. Once confirmed, update this checklist and commit.
