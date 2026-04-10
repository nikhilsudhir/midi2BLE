<div align="center">

# midi2BLE

**Wireless MIDI transmitter firmware for ESP32-S3**

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0-blue?logo=espressif)](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/)
[![Target](https://img.shields.io/badge/target-ESP32--S3-red?logo=espressif)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Status](https://img.shields.io/badge/status-in%20development-yellow)]()
[![License](https://img.shields.io/badge/license-MIT-lightgrey)](LICENSE)

Part of a larger project to build a custom, battery-powered BLE MIDI adapter with a bespoke PCB and 3D-printed enclosure. This firmware bridges a USB MIDI keyboard to a Mac over Bluetooth LE — eliminating the cable between instrument and computer.

[**View project page →**](https://nikhilsudhir.github.io/projects/midi2BT.html)

</div>

---

## Signal flow

```
┌─────────────────────┐     USB      ┌─────────────┐    BLE MIDI    ┌───────────┐     ┌─────┐
│  USB MIDI keyboard  │ ──────────── │  ESP32-S3   │ ────────────── │   macOS   │ ──► │ DAW │
│  (e.g. MPK Mini III)│              │  (this fw)  │                │           │     └─────┘
└─────────────────────┘              └─────────────┘                └───────────┘
```

---

## Features

| | |
|---|---|
| **USB MIDI host** | Enumerates any class-compliant USB MIDI controller — no driver needed |
| **BLE MIDI peripheral** | Apple BLE MIDI spec, appears as a native MIDI port in Audio MIDI Setup |
| **MIDI panic** | On USB disconnect, sends Note Off + CC 123/120 for every active note on all 16 channels |
| **Auto-advertising** | Restarts BLE advertising automatically on every disconnect or reboot |

---

## Hardware

| Component | Details |
|---|---|
| MCU | ESP32-S3 |
| USB | Native USB OTG peripheral, running as host |
| Wireless | Internal BLE radio, NimBLE stack |
| Power | LiPo (custom PCB) |

> Tested with an **Akai MPK Mini III**. Any USB class-compliant MIDI device should work.

---

## Getting started

### Requirements

- [ESP-IDF v6.0](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/get-started/)
- ESP32-S3 board with USB OTG

### Build & flash

```bash
idf.py build
idf.py -p PORT flash monitor
```

### Pairing with macOS

1. Open **Audio MIDI Setup** → **Window** → **Show MIDI Studio**
2. Click the **Bluetooth** icon in the toolbar
3. Find **MIDI2BLE** and click **Connect**

The device appears as a standard MIDI port in any DAW. Reconnect via the same panel after any power cycle.

---

## Repository structure

```
midi2BLE/
├── main/
│   ├── usb_host_lib_main.c     entry point, USB host library task
│   ├── class_driver.c          USB class driver, MIDI parsing, note tracking & panic
│   ├── ble_midi.c              BLE MIDI peripheral, GATT service, advertising
│   └── ble_midi.h              public API
├── hardware/
│   ├── ecad/                   PCB schematic & layout        (coming soon)
│   └── mcad/                   Enclosure CAD files           (coming soon)
└── sdkconfig.defaults
```

---

## Build status

| Component | Status |
|---|---|
| Firmware | ✅ Functional |
| PCB design | 🔧 In progress |
| Enclosure | 🔧 In progress |

---

<div align="center">

[nikhilsudhir.github.io](https://nikhilsudhir.github.io/projects/midi2BT.html)

</div>
