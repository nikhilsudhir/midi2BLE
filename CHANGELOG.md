# Changelog

All notable changes to this project are documented here.

---

## [Unreleased]

### In progress
- PCB schematic and layout (ECAD)
- Enclosure design (MCAD)
- Hardware photos

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
