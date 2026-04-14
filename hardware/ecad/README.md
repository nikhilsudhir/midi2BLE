# midi2BLE

## Overview

This device is a portable USB-to-BLE MIDI bridge designed to interface with USB MIDI controllers such as the Akai MPK Mini III.

It supports two user-selectable modes:

* Wireless BLE MIDI operation
* Wired USB passthrough operation

The device is battery powered with USB-C charging and includes an onboard OLED display for status and battery information.

---

## Operating Modes

### 1. Wireless Mode

In this mode, the device acts as a BLE MIDI bridge.

#### Behavior

* ESP32-S3 operates as a USB host
* MIDI data is read from the keyboard and transmitted via BLE
* Device is powered from internal LiPo battery
* OLED displays:

  * battery percentage
  * estimated runtime
  * BLE connection status
  * keyboard connection status

#### Power

* Battery supplies system power
* Optional charging via USB-C

---

### 2. Passthrough Mode

In this mode, the device behaves as a direct USB cable between the keyboard and a computer.

#### Behavior

* Keyboard is directly connected to upstream USB-C port via hardware switching
* ESP32 is not part of the USB data path
* BLE functionality is disabled
* OLED remains active and displays:

  * "WIRED MODE"
  * charging status
  * battery percentage

#### Power

* System is powered from upstream USB-C
* Battery does NOT power the system
* Battery may still be charged

---

## Mode Selection

* A physical switch selects between:

  * WIRELESS
  * WIRED
* The switch is authoritative
* Mode switching is NOT live
* Device must be restarted after changing modes

---

## Ports

### Upstream USB-C Port

* Used for:

  * charging the device
  * connecting to a computer in passthrough mode
* Supplies system power when connected

### Keyboard USB-C Port

* Used to connect the MIDI controller
* Behavior depends on mode:

  * Wireless: connected to ESP32 USB host
  * Passthrough: connected directly to upstream USB-C

---

## Power Architecture

* 1-cell LiPo battery
* USB-C charging input
* Power-path design allows:

  * operation while charging
  * seamless transition between power sources

### Power Priorities

#### Wireless Mode

1. Battery powers system
2. USB-C supplements or charges battery

#### Passthrough Mode

1. Upstream USB-C powers system
2. Battery is isolated from load
3. Battery may still charge

---

## USB Routing

### Wireless Mode

* Keyboard D+/D- connected to ESP32 USB host

### Passthrough Mode

* Keyboard D+/D- connected directly to upstream USB-C port

This is implemented using a USB 2.0 switch/multiplexer.

---

## Keyboard Power (VBUS)

### Wireless Mode

* Supplied by internal 5V boost converter

### Passthrough Mode

* Supplied directly from upstream USB-C VBUS

---

## Display (OLED)

* SPI-connected monochrome OLED display
* Displays:

  * battery percentage
  * estimated runtime
  * current mode
  * connection status

---

## Firmware Behavior

### Wireless Mode

* USB host enabled
* BLE MIDI enabled
* MIDI data forwarded USB → BLE

### Passthrough Mode

* USB host disabled
* BLE disabled
* ESP32 used only for UI/status

---

## V1 Scope

### Included

* BLE MIDI output
* USB passthrough
* battery operation
* USB-C charging
* OLED display
* manual mode switch

### Excluded

* custom RF protocols
* MIDI remapping
* display UI complexity
* automatic mode switching
* USB hub functionality

---

## Hardware Design

The schematic and PCB layout are both complete. The design is implemented in **KiCad 10.0**.

| | |
|---|---|
| Revision | 1 |
| Schematic date | 2026-04-11 |
| PCB date | 2026-04-12 |

The schematic is split across seven sheets:

| Sheet | Description |
|---|---|
| Root | Top-level block diagram and inter-sheet connections |
| Power | LiPo charging, buck/boost regulation, and power-path control |
| ESP32_S3 | ESP32-S3-WROOM-1 module, decoupling, and GPIO assignments |
| USB Data | USB 2.0 data switching between keyboard and upstream port |
| VBUS Switching | 5V VBUS supply selection for the keyboard port |
| OLED UI | SPI OLED display and mode select switch |
| Battery Sense | Resistor divider and ADC input for battery voltage measurement |

### Key ICs

| IC | Part | Function |
|---|---|---|
| U1 | ESP32-S3-WROOM-1 | Main MCU + Wi-Fi/BLE module |
| U2 | BQ24074RGTT | 1-cell LiPo charger |
| U3 | TPS62162DSG | 3.3 V synchronous buck regulator |
| U4 | TLV61047DDC | 5 V boost converter (keyboard VBUS in wireless mode) |
| U5 | TPS2116DRL | Dual-input power mux (battery vs. USB-C power path) |
| U6 | TS3USB30EDGSR | USB 2.0 high-speed switch (keyboard data routing) |
| U7 | PRTR5V0U2X | USB ESD protection |

### PCB

The PCB is laid out in KiCad. All components are placed and fully routed.

**Board dimensions:** 75 × 40mm  
**Thickness:** 1.6mm  
**Layers:** 4  

#### Stackup

| Layer | Type | Thickness | Material |
|---|---|---|---|
| F.Cu | Signal | 35µm | Copper |
| Dielectric 1 | Prepreg | 0.1mm | FR4 |
| In1.Cu | GND plane | 35µm | Copper |
| Dielectric 2 | Core | 1.24mm | FR4 |
| In2.Cu | 3.3V power plane | 35µm | Copper |
| Dielectric 3 | Prepreg | 0.1mm | FR4 |
| B.Cu | Signal | 35µm | Copper |

Inner layers are solid copper fills: In1 = GND, In2 = SYS_3V3.

**Test points:** TP1–TP6  
**Via size:** 0.45mm diameter, 0.2mm drill

---

## Summary

This V1 design prioritizes:

* simplicity
* reliability
* clear user control

The system uses a manual mode switch and hardware-level USB routing to ensure predictable behavior and robust operation.

**Current status:** Schematic and PCB layout complete. Ready for fabrication review.
