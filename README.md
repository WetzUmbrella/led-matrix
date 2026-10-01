# led-matrix: Distributed LED Matrix Video Wall

[![Build firmware](https://github.com/WetzUmbrella/led-matrix/actions/workflows/build.yml/badge.svg)](https://github.com/WetzUmbrella/led-matrix/actions/workflows/build.yml)

*INF2004 Embedded Systems project overview*
*Last updated: 2026-10-01 (router mode, board numbers, auto-split by panel count)*

The badge shows whether the latest push compiles. Every push builds the Pico
coordinator and the ESP32 panel firmware on GitHub; see the Actions tab.
Everything else on this page is updated by hand. When you change something,
update the matching section (decisions, status, open questions) in the same commit.

---

## 1. What this project is

A wall of LED panels that plays one animation across all of them, kept in
sync wirelessly. Each panel is a Waveshare **ESP32-S3-Matrix** (8x8 WS2812,
GPIO14, 64 LEDs). A **Raspberry Pi Pico W** is the coordinator: it keeps the
master clock, decides the layout, and broadcasts sync beacons.

- The original plan was a single 16x16 matrix. Hardware issues forced a pivot
  to tiling small boards: 4 boards make 2x2, and the design scales to 8.
- **The main technical goal is inter-board sync** (real-time timing and
  distributed coordination), as the embedded-systems showcase.
- Plug in 1 panel and it shows the whole GIF. Plug in more and the GIF splits
  across them automatically (2x1, 3x1, 2x2, 3x2, 4x2).
- Deferred: the 3D-printed plate, IMU orientation, Hall-sensor position
  detection, and pushing new animations over WiFi.

## 2. Architecture

```
                 [WiFi router]  2.4 GHz, WPA2, DHCP (e.g. 192.168.1.x)
          /         |          |          |         \
   [PICO-Main]   [ESP-1]    [ESP-2]    [ESP-3]  ... [ESP-8]
   sync master   ESP32-S3-Matrix panels, all running the SAME firmware
      |  UDP broadcast beacon to the subnet, 10x per second, port 4210
      |  (frame clock + layout: panel count, grid, who is in which slot)
      |  each panel sends a HELLO to the Pico every 0.3s, port 4211
```

**Data path:** Pico clock → beacon (broadcast) → each panel corrects its own
frame timer → each panel draws its own crop of the locally stored GIF. Only
sync messages go over WiFi, never pixels.

### 2.1 Component roles

| Component | Role |
|---|---|
| Pico W (`ledwall_sync/`, Pico SDK) | Master frame clock (30 fps); tracks which panels are alive (HELLO, 5s timeout); orders them by board number; picks the grid; broadcasts beacons. Hostname `PICO-Main` |
| ESP32-S3 panels (`panel_sync/`, ESP-IDF) | Store the whole animation; run their own frame timer and correct it from beacons; find their slot in the beacon; render their crop via RMT. Hostname `ESP-<n>` |
| WiFi router | Only carries packets (DHCP and switching). It makes no decisions |
| `set_board_number.sh` | One-time per board: writes its permanent number to flash (NVS) |
| `boardtest/` | Hardware smoke test: MAC, IMU check, colour cycle, walks a pixel across all 64 LEDs |

### 2.2 Panel states (what the LEDs mean)

| Panel shows | State | Meaning |
|---|---|---|
| Blinking red pixel | No sync | No beacon from the Pico yet, or the Pico and panel protocol versions differ |
| Blinking blue pixel | Unplaced | Hears the Pico but has no place in the wall: no board number set, or a duplicate number |
| Blue frame with a number (2s) | Layout changed | Its position in the wall just changed. Place it there |
| Red swirl piece | Playing | Synced and playing its crop |
| Solid red corner pixel | Pico lost | No beacon for over 1s. Still playing on its own clock |

## 3. Settled decisions

| Decision | Detail / why |
|---|---|
| **Pico W is the coordinator** | The course wants the Pico in the design. It can't do ESP-NOW (Espressif-only), so sync is UDP over WiFi |
| **Broadcast sync + local playback** | Every panel stores the animation; beacons only correct each panel's clock, so a lost packet never freezes the wall |
| **External router, not the Pico's access point** | The Pico W soft-AP (CYW43439) accepts at most **4 clients**, so 5+ panels need a router. The Pico stays master. `USE_ROUTER 0` falls back to the Pico AP (max 4) |
| **No hardcoded IPs** | The Pico broadcasts to whatever subnet DHCP gives it; panels learn the Pico's IP from beacon source addresses. Works on any router |
| **Permanent board numbers** | Set once per board (`set_board_number.sh`, stored in NVS, survives reflashing). They give the router hostname `ESP-<n>` and the board's order in the wall |
| **Auto-split by panel count** | Connected panels line up by board number with no gaps (lowest = top-left). 1=1x1, 2=2x1, 3=3x1, 4=2x2, 5-6=3x2, 7-8=4x2. When a panel leaves, the rest re-split within ~5s |
| **Secrets stay local** | Router SSID/password live in git-ignored `wifi_secrets.h`; only examples are committed |
| **Brightness cap `BRIGHT 8`** | Full brightness caused colour dropout; below ~4, colours drop out |
| **ESP-IDF v5.3 + Pico SDK 2.3.1** | Pinned across the team and in CI so builds match |

### 3.1 Decisions from 2026-10-01 (router session)

| Decision | Detail |
|---|---|
| Router | A friend's ASUS AC1200 on the 2.4 GHz band, WPA2. It must have **AP isolation off** |
| Board numbering | Changed from join-order "sticky" numbers to a fixed number per board. The layout still grows and shrinks with panel count |
| Protocol v3 | HELLO now carries the board number. The Pico and every panel must run v3 |
| Hostnames | `PICO-Main`, `ESP-<n>` (hyphens, because routers reject `_` in hostnames) |

## 4. Current status

**Verification levels:** *built* = compiles · *flashed* = running on a board ·
*verified* = observed working on hardware, with logs.

| Item | Status |
|---|---|
| ESP-IDF v5.3 + Pico SDK set up in WSL, USB passthrough via usbipd | Verified |
| Board test: all boards (MAC, IMU, all 64 LEDs) | Verified |
| Pico AP mode (`USE_ROUTER 0`): sync with 0 dropped beacons | Verified (before the router move) |
| Router mode: Pico joins the router, panels find the Pico from its beacons | Verified (Pico + panels on ASUS AC1200) |
| Board numbers + hostnames (`PICO-Main`, `ESP-1`…`ESP-4`) | Verified on panels; router client list not yet checked |
| Auto-split ordered by board number | 2x1 verified. **3x1 and 2x2 with all four boards: confirm** |
| Beacon loss through the router | **~4.5%** (vs 0% on the Pico AP). The router was at DTIM 3 |
| Inter-panel skew measurement | Not started |
| CI build of all firmware on every push | Added 2026-10-01 |
| IMU orientation, Hall-sensor position, animation upload over WiFi, 3D plate | Not started |

## 5. Open questions

- Does setting **DTIM 1** on the router bring beacon loss back near 0%? If not: send each beacon twice (a repeated `seq` is harmless to panels), or unicast per panel?
- Will the friend's router be available on demo day, or do we need our own? The `USE_ROUTER 0` fallback only covers 4 panels.
- How do we measure skew? Proposed: 240 fps slow-mo of a hard frame cut, compared between router and Pico AP. What's the pass threshold?
- Demo layout: a fixed 2x2, or show the wall growing and shrinking live as panels are added and removed?
- Is 5+ panels needed for marks, or is 4 (2x2) the target?
- IMU orientation and Hall-sensor position detection: still in scope?
- Plate/case design and the power arrangement (one multi-port charger vs per-board).

## 6. Risks / gotchas

- **Beacon loss through the router (~4.5%).** Router broadcasts are never acknowledged or resent. Panels hide it by running their own clock, but it adds jitter and makes the "0 drops" claim untrue in router mode.
- **Depending on someone else's router.** Settings (AP isolation, DTIM) can be changed without us knowing. Check the panel log at boot: it prints `DTIM period = N`.
- **Protocol version mismatch.** A board on old firmware silently ignores beacons and blinks red. Reflash the Pico and *all* panels together.
- **`idf.py erase-flash` wipes the board number** (so does changing the partition table). Re-run `set_board_number.sh` afterwards.
- **Duplicate board numbers.** The second board is refused (blinks blue) and the Pico logs `IGNORED ... already`. Label boards physically.
- **Pico AP fallback is capped at 4 panels**, by the chip, not by our code.
- **Power.** Several boards on WiFi can brown out an unpowered hub. Use chargers or a powered hub, and keep brightness low.
- **Don't overstate verification.** "Working" means flashed and observed on hardware, not just compiled.

## 7. Suggested build order (from here)

1. Confirm 3x1 and 2x2 with all four boards; record `drops=` lines.
2. Set DTIM 1 on the router and re-measure beacon loss. If it's still over 1%, add the double-send.
3. Measure inter-panel skew (router vs Pico AP) for the report.
4. Merge `router-mode` into `master`.
5. Update the Week 6 design doc to match this page (router, 4-client limit, board numbering).
6. Then the features: IMU orientation → Hall-sensor position → animation upload → 3D plate.

---

## Repo layout

```
led-matrix/
├── .github/workflows/build.yml   CI: builds all firmware on every push
├── boardtest/        ESP32 board test (ESP-IDF)
├── ledwall_sync/     Pico W coordinator (Pico SDK)
├── panel_sync/       ESP32 panel firmware (ESP-IDF), same on every panel
│   └── set_board_number.sh   one-time: give a board its permanent number
└── README.md         this overview
```

Each of `boardtest/`, `ledwall_sync/`, `panel_sync/` is its own buildable
project. `build/`, `managed_components/`, `sdkconfig`, `sdkconfig.old`,
`dependencies.lock` and `wifi_secrets.h` are local-only and gitignored.

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
  Run `get_idf` in every new terminal before `idf.py`.
- Pico SDK **2.3.1** (for `ledwall_sync/` only):
  ```bash
  mkdir -p ~/pico && cd ~/pico
  git clone -b 2.3.1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
  echo 'export PICO_SDK_PATH=$HOME/pico/pico-sdk' >> ~/.bashrc
  ```
  Needs CMake 3.17+. Ubuntu 20.04 ships 3.16: run `pip install --user cmake` and put `~/.local/bin` first on `PATH`.

Clone this repo inside the WSL Linux filesystem (e.g. `~/esp-projects`), never
under `/mnt/c` or a cloud-synced drive.

## Router setup (one-time)

On the router's admin page, for the **2.4 GHz** band (ESP32-S3 and Pico W are 2.4 GHz only):

- Security **WPA2-Personal (AES)**. WPA2/WPA3 mixed also works; WPA3-only doesn't.
- **Wireless → Professional → Set AP Isolated: No.** Otherwise panels can't reach the Pico.
- **DTIM Interval: 1.** With the default (often 3) the router holds broadcasts and releases them late in bursts.
- Optional: fixed channel (1/6/11), 20 MHz, Airtime Fairness off. Don't use a guest network (it usually isolates clients).

## WiFi credentials

```bash
cp ledwall_sync/wifi_secrets.example.h ledwall_sync/wifi_secrets.h
cp panel_sync/main/wifi_secrets.example.h panel_sync/main/wifi_secrets.h
# edit both: ROUTER_SSID / ROUTER_PASS ("" for an open network)
```

Never commit `wifi_secrets.h`. Without it the build warns and uses placeholders (that's what CI does).

## Build & flash

### Panels (`panel_sync/`, ESP-IDF)

```bash
get_idf
cd panel_sync
idf.py set-target esp32s3        # first time only
./set_board_number.sh 1          # first time per board only: 1, 2, 3, ... (unique per board)
idf.py -p /dev/ttyACM0 flash monitor
```

Look for `I am ESP-<n>` → `joined <router>` → `Pico found at ...` → `beacon ... drops=...`.
Exit the monitor with `Ctrl+]`. If flashing hangs at "Connecting...", hold
**BOOT**, tap **RESET**, release **BOOT**, and retry.

Attach each board to WSL first: `usbipd list`, then `usbipd attach --wsl --busid <BUSID>`
(the first time per board, `usbipd bind` in an admin PowerShell). The BUSID follows the
physical USB port. Once flashed, a panel only needs USB power (charger or power bank).

### Pico W coordinator (`ledwall_sync/`, Pico SDK)

```bash
cd ledwall_sync
mkdir -p build && cd build
cmake .. && make -j4
```

Hold **BOOTSEL**, plug in the Pico, and copy `build/ledwall_sync.uf2` onto the **RPI-RP2**
drive. Its USB serial log should show `Joined <router>, ip=... bcast=...`. The onboard LED
blinks every ~0.5s while beacons go out, and toggles only every ~15s while it's still trying to join.

CI artifacts: each Actions run also uploads `ledwall_sync.uf2` and `panel_sync.bin`.
They're built with placeholder WiFi credentials, so use them to check builds, not to flash.

### Board test (`boardtest/`, ESP-IDF)

```bash
get_idf
cd boardtest
idf.py -p /dev/ttyACM0 flash monitor
```

## Key settings

| Setting | Value |
|---|---|
| Network mode | `USE_ROUTER 1` (router) / `0` (Pico's own AP); set the same in both projects |
| Router WiFi | from `wifi_secrets.h` (git-ignored) |
| Fallback AP (`USE_ROUTER 0`) | `LEDWALL` / `ledwall123`, Pico at 192.168.4.1, max 4 panels |
| Hostnames | `PICO-Main`, `ESP-<board number>` |
| Max panels | 8 (`MAX_NODES`) |
| Protocol version | 3 (the Pico and panels must match) |
| Beacon port / HELLO port | 4210 / 4211 |
| Master clock / beacon rate | 30 fps / 10 Hz |
| Panel timeout | 5000 ms |
| LED data pin | GPIO 14 |
| Brightness cap | `BRIGHT 8` (out of 255) |
| Animation | `panel_sync/main/gif_frames.h`, 32x32, 30 frames, 15 fps |

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Panel log `lost <router>, retrying` | WiFi link dropping: check 2.4 GHz, WPA2, password |
| Joined, but never `Pico found` | AP isolation is on, or it's a guest network |
| Pico log `IGNORED ... board number` | Run `set_board_number.sh` on that board |
| Pico log `IGNORED ... already` | Two boards share a number |
| Panel blinks red forever | Pico not running, or Pico/panel protocol versions differ |
| Fresh boot in a panel log | Panel is rebooting: check power/brightness |
| Repeated `LEAVE`/`JOIN` on the Pico | Check-ins being lost: signal, channel, DTIM |
| `CMakeCache.txt ... different directory` | Build folder copied from elsewhere: delete `build/` (`idf.py fullclean`) |
| `/dev/ttyACM0: No such file` | Board not attached to WSL (`usbipd attach`) |

## History

An earlier ESP-NOW controller/peer design (one board pushing quadrant data to
the other three) was abandoned before 4-board testing. It's preserved at tag
[`v0-esp-now-single-node`](../../tree/v0-esp-now-single-node). A bug found there,
worth remembering if that code is reused: `lm_espnow_register_peers()` didn't skip
the controller's own grid position, so duplicate placeholder MACs triggered
`ESP_ERR_ESPNOW_EXIST` inside `ESP_ERROR_CHECK`, a silent crash-reboot loop.
