<div align="center">

# midi2BLE

**Custom wireless MIDI adapter — ESP32-S3, LiPo powered, custom PCB**

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/)
[![Target](https://img.shields.io/badge/target-ESP32--S3-red?logo=espressif)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Status](https://img.shields.io/badge/status-in%20development-yellow)]()
[![License](https://img.shields.io/badge/license-MIT-lightgrey)](LICENSE)

A custom, battery-powered BLE MIDI adapter with a bespoke PCB and 3D-printed enclosure. Bridges a USB MIDI keyboard to a Mac over Bluetooth LE, eliminating the cable between instrument and computer.

[**View project page →**](https://nikhilsudhir.github.io/projects/midi2BT.html)

</div>

---

## Signal flow

<div align="center">

| **USB MIDI Keyboard** | | **ESP32-S3** | | **macOS** | | **DAW** |
|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| *(e.g. MPK Mini III)* | `── USB ──►` | *midi2BLE firmware* | `── BLE MIDI ──►` | *Audio MIDI Setup* | `──►` | *FL Studio* |

</div>

---

## Features

| | |
|---|---|
| **Wireless mode** | ESP32-S3 acts as USB host; MIDI data forwarded over BLE to macOS |
| **Passthrough mode** | Keyboard connected directly to upstream USB-C via hardware switch — ESP32 out of data path |
| **USB MIDI host** | Enumerates any class-compliant USB MIDI controller — no driver needed |
| **BLE MIDI peripheral** | Apple BLE MIDI spec, appears as a native MIDI port in Audio MIDI Setup |
| **MIDI panic** | On USB disconnect, sends Note Off + CC 123/120 for every active note on all 16 channels |
| **Auto-advertising** | Restarts BLE advertising automatically on every disconnect or reboot |
| **OLED display** | SPI-connected display shows mode, BLE status, battery %, and estimated runtime |
| **Battery management** | LiPo charge, power-path control, and ADC-based battery sense |

---

## Hardware

| Component | Part | Role |
|---|---|---|
| MCU / BLE | ESP32-S3-WROOM-1 | Application processor, USB host, BLE MIDI |
| LiPo charger | BQ24074RGTT | 1-cell USB-C LiPo charging |
| Buck regulator | TPS62162DSG | 3.3 V system rail |
| Boost converter | TLV61047DDC | 5 V VBUS supply for keyboard (wireless mode) |
| Power mux | TPS2116DRL | Battery / USB-C power-path switching |
| USB switch | TS3USB30EDGSR | Routes keyboard D+/D− to ESP32 or upstream port |
| USB ESD | PRTR5V0U2X | ESD protection on USB lines |
| Display | SPI OLED | Mode, connection status, battery info |

> Tested with an **Akai MPK Mini III**. Any USB class-compliant MIDI device should work.

---

## Repository structure

- `main/` — Firmware (ESP-IDF / C) — see [main/README.md](main/README.md)
- `hardware/`
  - `ecad/` — KiCad schematic & PCB layout — see [hardware/ecad/README.md](hardware/ecad/README.md)
  - `mcad/` — Fusion 360 enclosure — see [hardware/mcad/README.md](hardware/mcad/README.md)
- `CMakeLists.txt`
- `sdkconfig.defaults`

---

## Build status

| Component | Status |
|---|---|
| Firmware | ✅ Functional |
| Schematic | ✅ Complete (v1) |
| PCB layout | 🔧 In progress |
| Enclosure | 🔧 In progress |

---

<div align="center">

[nikhilsudhir.github.io](https://nikhilsudhir.github.io/projects/midi2BT.html)

</div>
