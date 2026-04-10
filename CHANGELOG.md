# Changelog

All notable changes to this project are documented here.

---

## [Unreleased]

### In progress
- PCB schematic and layout (ECAD)
- Enclosure design (MCAD)
- Hardware photos

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
