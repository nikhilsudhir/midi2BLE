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

## Repository structure

```
midi2BLE/
├── main/           Firmware — see main/README.md
├── hardware/
│   ├── ecad/       PCB schematic & layout        (coming soon)
│   └── mcad/       Enclosure CAD files           (coming soon)
├── CMakeLists.txt
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
