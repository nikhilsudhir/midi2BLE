#pragma once
#include <stdint.h>

/**
 * Initialize the BLE MIDI peripheral and start advertising.
 * Call once from app_main before any tasks that use ble_midi_send().
 */
void ble_midi_init(void);

/**
 * Send 1–3 raw MIDI bytes as a BLE MIDI notification.
 * No-ops if no host is connected.
 */
void ble_midi_send(const uint8_t *midi_bytes, int len);
