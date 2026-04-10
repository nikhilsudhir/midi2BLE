# Firmware

ESP32-S3 firmware for the midi2BLE adapter. Written in C using ESP-IDF v6.0 and NimBLE.

---

## Architecture

Three tasks run concurrently under FreeRTOS:

| Task | Priority | Description |
|---|---|---|
| `usb_host` | 2 | Runs the USB host library event loop |
| `class` | 3 | USB class driver — enumerates devices, handles MIDI transfers |
| NimBLE host | — | Managed internally by NimBLE port |

`app_main` initialises BLE, starts both tasks, then returns.

---

## File structure

```
main/
├── usb_host_lib_main.c     Entry point, USB host library task
├── class_driver.c          USB class driver, MIDI parsing, note tracking & panic
├── ble_midi.c              BLE MIDI peripheral, GATT service, advertising
├── ble_midi.h              Module interface
├── idf_component.yml       Component dependencies
└── CMakeLists.txt
```

---

## Module overview

### `usb_host_lib_main.c`
Entry point. Initialises the BLE MIDI peripheral, installs the USB host library, and spawns the USB host and class driver tasks.

### `class_driver.c`
Registers as a USB host client and waits for MIDI devices to connect. On connection it claims the MIDI Streaming interface and begins bulk IN transfers. Incoming 4-byte USB MIDI event packets are parsed, forwarded over BLE, and tracked for active note state. On disconnect, fires a MIDI panic before releasing the device.

### `ble_midi.c` / `ble_midi.h`
NimBLE peripheral implementing the Apple BLE MIDI spec. Exposes two functions:

```c
void ble_midi_init(void);
void ble_midi_send(const uint8_t *midi_bytes, int len);
```

Advertising starts automatically on boot and restarts on every disconnect.

---

## MIDI panic

If a USB MIDI device is unplugged while notes are held, the host would otherwise receive no Note Off and notes would drone indefinitely. On every USB disconnect the firmware:

1. Sends Note Off for every tracked active note
2. Sends CC 123 (All Notes Off) on all 16 channels
3. Sends CC 120 (All Sound Off) on all 16 channels

---

## Building

Requires [ESP-IDF v6.0](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/get-started/).

```bash
idf.py build
idf.py -p PORT flash monitor
```

Target is fixed to `esp32s3` via `sdkconfig.defaults`.

---

## Dependencies

| Component | Source |
|---|---|
| `espressif/usb ^1.0.0` | [ESP-IDF Component Registry](https://components.espressif.com/components/espressif/usb) |
| NimBLE | Bundled with ESP-IDF (`CONFIG_BT_NIMBLE_ENABLED`) |
