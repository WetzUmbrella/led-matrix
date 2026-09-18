# led-matrix

Walking-dot test for the onboard 8x8 WS2812 LED matrix on a Waveshare
ESP32-S3-Matrix board (GPIO14, 64 LEDs). Built with ESP-IDF v5.3.

Scope: LED matrix only. The board's onboard QMI8658 IMU (I2C GPIO11/12) is
not used here.

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
│   ├── idf_component.yml
│   └── led-matrix.c
├── sdkconfig.defaults
└── README.md
```

`build/`, `managed_components/`, `sdkconfig`, `sdkconfig.old`, and
`dependencies.lock` are all regenerated locally and are gitignored.

## Current Status / Next Steps

_(last updated 2026-09-18)_

- [x] ESP-IDF v5.3 installed at `~/esp/esp-idf` (WSL), `get_idf` alias set up
- [x] Project scaffolded, target set to `esp32s3`, `led_strip` dependency added
- [x] Walking-dot LED code written (`main/led-matrix.c`)
- [x] `idf.py build` verified successful (compiles clean, `led_strip` resolves)
- [x] Repo pushed to GitHub, public
- [ ] **Flash + visual confirmation** — not done yet, board wasn't available this session

**To pick this up (same machine or a second machine, e.g. a PC after setting it up on a laptop):**

1. If this is a machine that hasn't been set up before: work through Prerequisites → step 5 above first (WSL2+Ubuntu, `usbipd-win`, ESP-IDF v5.3 install, clone this repo, `set-target esp32s3`, `idf.py build`). Machine-level installs (WSL, `usbipd-win`, ESP-IDF itself, `gh` CLI) are per-machine and are **not** carried by git — each new machine needs them installed fresh; only this repo's contents come from `git clone`.
2. Plug in the board, then in an admin PowerShell: `usbipd list` → note the BUSID → `usbipd bind --busid <BUSID>` → `usbipd attach --wsl --busid <BUSID>`.
3. In WSL, confirm it shows up: `ls /dev/ttyACM0`.
4. `get_idf && idf.py -p /dev/ttyACM0 flash monitor`.
5. Confirm visually: exactly one dim (~10% brightness) pixel lit at a time, walking through all 64 LEDs in sequence, looping. If it hangs at "Connecting...", hold BOOT, tap RESET, release BOOT, retry.
6. Once confirmed, update this checklist and commit.
