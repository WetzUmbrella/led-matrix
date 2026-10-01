# led-matrix

Firmware for the INF2004 "Distributed LED Matrix Video Wall" project: a Pico W
coordinator wirelessly syncs an animation across a grid of Waveshare
ESP32-S3-Matrix boards (8x8 WS2812 each, GPIO14, 64 LEDs per board), which
together display one animation scaled and cropped to fill the whole grid.

> **Older single-node design:** an earlier ESP-NOW controller/peer approach
> (one board pushing quadrant data to the other three) was designed, compiled,
> and passed a single-board Controller smoke test, but was abandoned in favor
> of the Pico W design below before the full 4-board flow was ever tried. That
> code is preserved at tag [`v0-esp-now-single-node`](../../tree/v0-esp-now-single-node).
> Testing on it did turn up a real bug worth remembering if any of that code
> gets reused: `lm_espnow_register_peers()` in `main/esp_now_sync.c` didn't
> skip the controller's own grid position when registering ESP-NOW peers, so
> duplicate placeholder MACs (e.g. all left at the default) triggered
> `ESP_ERR_ESPNOW_EXIST` inside an `ESP_ERROR_CHECK`, causing a silent
> crash-reboot loop with no symptom beyond "the LEDs never light up."

## Project Context

Original plan was a single 16x16 matrix; hardware issues forced a pivot to
**4x ESP32-S3-Matrix boards (8x8 each)**, tiled up to 2x2 (or fewer/more, see
below), to cover the same area. The team's focus is **inter-board sync**,
as the embedded-systems showcase for the course. A 3D-printed case/plate is
deferred.

## Architecture

```
                 [WiFi router]  2.4 GHz, WPA2, DHCP (e.g. 192.168.1.x)
          /         |          |          |         \
   [PICO-Main]   [ESP-1]    [ESP-2]    [ESP-3]  ... [ESP-8]
   sync master   ESP32-S3-Matrix panels, all running the SAME firmware
      |  UDP broadcast beacon to the subnet, 10x per second, port 4210
      |  (frame clock + layout: panel count, grid, who is in which slot)
      |  each panel sends a HELLO to the Pico every 0.3s, port 4211
```

- **Why a router:** the Pico W's own access point (CYW43439 soft-AP) accepts
  at most **4 clients**, so 5+ panels need a real router. The Pico is still
  the sync master and coordinator; the router only carries packets.
  Set `USE_ROUTER 0` in both projects to fall back to the Pico's own
  `LEDWALL` access point (max 4 panels, no router needed).
- **No hardcoded IPs.** The Pico gets its address from the router's DHCP and
  broadcasts to its subnet; each panel learns the Pico's IP from the source of
  its beacons. The same firmware works on any router.
- **Only sync messages go over WiFi.** Every panel stores the whole animation
  locally; each panel runs its own frame timer, and beacons just correct it,
  so a single lost packet doesn't freeze anything.
- **Permanent board numbers:** each panel is given a number once with
  `panel_sync/set_board_number.sh` (stored in NVS, so normal reflashes keep
  it). It's the board's router hostname (`ESP-1`, `ESP-2`, ...) and its order
  in the wall. The Pico shows up as `PICO-Main`.
- **The GIF splits by how many panels are connected** (1 = whole GIF, 2 = 2x1,
  3 = 3x1, 4 = 2x2, 5-6 = 3x2, 7-8 = 4x2). Connected panels line up by board
  number with no gaps (lowest = top-left), and **each panel crops its own
  piece**, scaled to fill the wall with no black bars. When a panel drops out,
  the rest close up and re-split within ~5s.
- **Why not ESP-NOW for the coordinator:** the Pico W can't do ESP-NOW (an
  Espressif-only protocol), and the course wants the Pico in the design. A
  router-free alternative for 5+ panels would be Pico -> one ESP32 over
  UART -> ESP-NOW to the rest of the panels.

## Repo layout

```
led-matrix/
├── boardtest/       ESP32 board test: MAC, IMU check, colour cycle, walks a pixel across all 64 LEDs
├── ledwall_sync/     Pico W coordinator (Pico SDK): joins the router (or runs its own AP) + UDP beacon + layout decisions
├── panel_sync/       ESP32 panel firmware (ESP-IDF): same firmware on every panel; receives sync, renders its own crop of the animation
│   └── set_board_number.sh   one-time: give a board its permanent number (ESP-<n>)
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

## Router setup (one-time)

On the router's admin page, for the **2.4 GHz** band (ESP32-S3 and Pico W
are 2.4 GHz only):

- Security **WPA2-Personal (AES)**. WPA2/WPA3 mixed also works; not WPA3-only.
- **Wireless → Professional → Set AP Isolated: No**. Otherwise panels can't
  reach the Pico.
- **DTIM Interval: 1**. With the default (often 3) the router holds
  broadcasts and releases them late in bursts, adding beacon jitter and loss.
- Optional: fixed channel (1/6/11), 20 MHz, Airtime Fairness off. Don't use a
  guest network (it usually isolates clients).

## WiFi credentials

Both projects read the router's name/password from a git-ignored
`wifi_secrets.h`. Copy the example next to it and fill it in:

```bash
cp ledwall_sync/wifi_secrets.example.h ledwall_sync/wifi_secrets.h
cp panel_sync/main/wifi_secrets.example.h panel_sync/main/wifi_secrets.h
# edit both: ROUTER_SSID / ROUTER_PASS ("" for an open network)
```

Never commit `wifi_secrets.h`. Without it the build warns and uses placeholders.

## Build & flash

### Panels (`panel_sync/`, ESP-IDF)

```bash
get_idf
cd panel_sync
idf.py set-target esp32s3        # first time only
./set_board_number.sh 1          # first time per board only: 1, 2, 3, ... (unique per board)
idf.py -p /dev/ttyACM0 flash monitor
```

Check the log for `I am ESP-<n>` → `joined <router>` → `Pico found at ...`
→ `beacon ... drops=...`. The board number survives `idf.py flash`; only
`idf.py erase-flash` (or a partition table change) wipes it, in which case
run the script again. A board without a number blinks blue and isn't given
a place in the wall.

Exit the monitor with `Ctrl+]`. If flashing hangs at "Connecting...", hold
**BOOT**, tap **RESET**, release **BOOT**, retry. Pass each board's USB
device into WSL first via `usbipd bind`/`usbipd attach` (Windows admin
PowerShell) — binding is per physical board, attach is per session/replug.
Once flashed, a panel only needs USB power (charger/power bank) to run.

### Pico W coordinator (`ledwall_sync/`, Pico SDK)

```bash
cd ledwall_sync
mkdir -p build && cd build
cmake .. && make -j4
```

Needs CMake 3.17+ (Ubuntu 20.04's is 3.16: `pip install --user cmake` and put
`~/.local/bin` first on `PATH`). Hold **BOOTSEL** on the Pico W, plug it in,
copy `ledwall_sync.uf2` onto the **RPI-RP2** drive. Check its USB serial log
for `Joined <router>, ip=... bcast=...`; the onboard LED blinks every ~0.5s
while beacons go out (it toggles only every ~15s while still trying to join).

### Board test (`boardtest/`, ESP-IDF)

```bash
get_idf
cd boardtest
idf.py -p /dev/ttyACM0 flash monitor
```

## Key settings

| Setting | Value |
|---|---|
| Network mode | `USE_ROUTER 1` (router) / `0` (Pico's own AP), set in both projects |
| Router WiFi | from `wifi_secrets.h` (git-ignored) |
| Fallback AP (USE_ROUTER 0) | `LEDWALL` / `ledwall123`, Pico at 192.168.4.1, max 4 panels |
| Hostnames | `PICO-Main`, `ESP-<board number>` |
| Max panels | 8 (`MAX_NODES`) |
| Protocol version | 3 (HELLO carries the board number; Pico and panels must match) |
| Beacon port / HELLO port | 4210 / 4211 |
| Master clock | 30 fps |
| Panel timeout | 5000 ms |
| LED data pin | GPIO 14 |
| Brightness cap | `BRIGHT 8` (out of 255) |
| Animation | `panel_sync/main/gif_frames.h`, 32x32, 30 frames, 15 fps |

## Current Status / Next Steps

_(last updated 2026-10-01)_

- [x] ESP-IDF v5.3 + Pico SDK set up in WSL
- [x] USB passthrough via usbipd working
- [x] Board test: all boards flash-verified (MAC, IMU, all 64 LEDs)
- [x] Pico W as WiFi hub ("LEDWALL" access point) — working, 0 dropped beacons (now the `USE_ROUTER 0` fallback)
- [x] Found the Pico W soft-AP 4-client cap; moved to an external router for 5+ panels
- [x] Router mode: Pico joins the router, panels find the Pico from its beacons — hardware-verified (Pico + panels on an ASUS AC1200)
- [x] Permanent board numbers + hostnames (`PICO-Main`, `ESP-<n>`) — hardware-verified
- [x] Red swirl animation stored on each panel — working
- [x] Auto-split ordered by board number (1-8 panels) — built; 2x1 seen on hardware, **confirm 3x1 and 2x2 with all four boards**
- [ ] **Beacon loss through the router is ~4.5%** (vs 0% on the Pico AP). Set DTIM to 1 on the router and re-measure; if still >1%, have the Pico send each beacon twice (a repeated `seq` is harmless to panels) or unicast per panel
- [ ] Measure inter-panel skew (240fps slow-mo of a hard frame cut), router vs Pico AP
- [ ] IMU orientation (QMI8658), Hall-sensor position detection, pushing new animations over WiFi, 3D-printed plate

| Panel shows | Meaning |
|---|---|
| Blinking red pixel | No beacon from the Pico yet (or Pico/panel protocol versions differ) |
| Blinking blue pixel | Hears the Pico but has no place in the wall — no board number set, or a duplicate number |
| Blue frame with a number (2s) | Its position in the wall just changed — place it there |
| Red swirl piece | Synced and playing |
| Solid red corner pixel | Lost the Pico for over 1s (still playing on its own clock) |

Troubleshooting: panel log `lost <router>, retrying` = WiFi link dropping
(check 2.4 GHz/WPA2/password); joined but never `Pico found` = AP isolation
is on or it's a guest network; Pico log `IGNORED ... board number` = run
`set_board_number.sh` on that board; `IGNORED ... already` = two boards share
a number; a fresh boot in a panel log = the panel is rebooting (check
power/brightness); repeated `LEAVE`/`JOIN` on the Pico = check-ins are being lost.
