# Changelog

All notable changes to this project are documented here.

---

## [Unreleased]

---

## [2.0.0] — 05/2026

### Changed — Power Section

#### U1 — Battery Charger
- Replaced BQ24079RGTT with BQ24075RGT
- Fixes SYSOFF floating issue — BQ24075 does not have a SYSOFF pin

#### U3 — 5V Boost Converter
- Replaced TLV61047DDC with MT3608 (SOT-23-6) + external SS14 Schottky diode
- FB divider: R_upper = 68kΩ, R_lower = 8.2kΩ → 5.0V output
- EN pin driven by MODE_SEL GPIO — boost disabled in passthrough mode

#### U2 — 3.3V Buck Regulator (TPS62162DSG, unchanged IC)
- EN fix: previously tied to U3 VIN causing circular startup dependency; now connected directly to U1_OUT
- FB fix: previously connected to SYS_3V3 (wrong); now tied to GND

#### Miscellaneous Power
- R5 removed — 10kΩ VBUS_IN to GND bleed resistor served no purpose

### Changed — USB Switching (U6 TS3USB30E, unchanged IC)
- OE pin: previously driven by MODE_SEL (disconnected switch in one mode); now tied directly to GND (switch always enabled)

### Changed — ESP32 GPIO Assignments
- OLED SPI reassigned from GPIO11/12 → GPIO35/36 (GPIO11/12 conflict with octal flash on N8R2 variant)
- MODE_SEL reassigned from GPIO0 → GPIO2 (GPIO0 is boot strapping pin)

---

## [1.0.0] — 04/2026

### Known Issues (reason for v2 redesign)
- SYSOFF pin on BQ24079 left floating — internally pulled HIGH through 5MΩ to VBAT, disabling battery output and charging entirely; board appeared completely dead
- U2 EN tied to U3 VIN causing circular startup dependency
- U2 FB incorrectly connected to SYS_3V3 instead of GND
- U6 OE driven by MODE_SEL instead of tied to GND, disconnecting USB switch in one mode
- OLED SPI on GPIO11/12 conflicting with octal flash on ESP32-S3-WROOM-1-N8R2
- MODE_SEL on GPIO0 (boot strapping pin) causing boot issues

### Added

#### ECAD
- 7-sheet KiCad schematic — ERC clean
- Full 4-layer PCB routing complete — DRC clean
- Board: 100×40mm, JLCPCB JLC04161H-7628 stackup
- Surface finish: HASL, black soldermask
- Mounting holes: M2.5

#### MCAD
- Enclosure body v1 designed in Fusion 360 — USB-C cutout for keyboard port, fitment verified against reference keyboard model
- Rudimentary reference keyboard model created to assist with clearance and alignment

---

## [0.4.1] — 04/2026

### Added
- Full USB device name on screen — reads both manufacturer and product strings from USB descriptor, word-wraps to second line if longer than 21 characters
- Battery monitor code complete — ADC oneshot on GPIO4 with curve/line fitting calibration, battery bar and percentage on display; pending physical wiring (100kΩ resistors)

### Fixed
- Display driver rewritten for SH1106 compatibility — switched from horizontal addressing mode (SSD1306 only) to page-by-page writes which work on both SH1106 and SSD1306
- Charge pump command corrected for SH1106 (0xAD, 0x8B)
- Mode switch moved from GPIO0 to GPIO2 to avoid conflict with ESP32-S3 boot pin
- Build errors resolved — wrong USB host function name (`usb_host_device_info`), unused static function ordering

### Changed
- Display rotated 180° via segment remap and COM scan direction commands
- All screen text centred

---

## [0.4.0] — 04/2026

### Added
- GPIO mode switch — HIGH = WIRELESS (BLE), LOW = PASSTHROUGH (wired); reads at boot with internal pull-up
- Mode change detection via NVS — shows restart screen and reboots cleanly when switch position changes
- SH1106 SPI OLED display driver (128×64) with built-in 5×7 bitmap font
- Status screens for both modes — BLE connection state, USB device name, MIDI activity indicator
- USB product and manufacturer strings read from device descriptor and shown on screen
- 1 Hz OLED update task
- `mode_manager`, `oled_display`, and `battery` modules added as separate files

### Changed
- NVS initialisation moved to `app_main` so all modules share one init
- `app_main` branches on mode — PASSTHROUGH skips BLE and USB host entirely

---

## [0.3.0] — 04/2026

### Added
- MIDI panic on USB disconnect — sends Note Off for all active notes, plus CC 123 (All Notes Off) and CC 120 (All Sound Off) on all 16 channels
- Active note tracking per channel to avoid duplicate Note Off messages
- BLE advertising restarts automatically on every disconnect

### Changed
- Cleaned up repository structure — removed devcontainer, tidied .gitignore, added LICENSE and README

---

## [0.2.0] — 04/2026

### Added
- Full BLE MIDI peripheral implementation (Apple BLE MIDI spec)
- NimBLE GATT service with correct service and characteristic UUIDs
- MIDI forwarding from USB to BLE with proper timestamp framing
- Akai MPK Mini III confirmed working end-to-end

---

## [0.1.0] — 03/2026

### Added
- ESP32-S3 running as USB MIDI host
- USB MIDI device enumeration and MIDI Streaming interface detection
- Raw MIDI data passthrough to serial monitor for verification
