#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "mode_manager.h"
#include "driver/gpio.h"

// SPI pins — adjust to match PCB once schematic is finalised
#define OLED_CLK   GPIO_NUM_12
#define OLED_MOSI  GPIO_NUM_11
#define OLED_DC    GPIO_NUM_10   // Data/Command select
#define OLED_CS    GPIO_NUM_9    // Chip select
#define OLED_RES   GPIO_NUM_8    // Hardware reset

/**
 * Initialise the SSD1306 over I2C and clear the display.
 * Safe to call even if no display is physically connected — subsequent calls
 * will silently no-op if initialisation fails.
 */
void oled_init(void);

// ---- State setters (callable from any task) ---------------------------------

void oled_set_mode(device_mode_t mode);
void oled_set_ble_connected(bool connected);

/**
 * @param connected   true when a USB MIDI device is enumerated
 * @param device_name Product string from the USB descriptor, or "" if unavailable.
 *                    Truncated to 21 characters on screen.
 */
void oled_set_usb_connected(bool connected, const char *device_name);

/** Call each time a MIDI packet is received — clears automatically after redraw. */
void oled_set_midi_active(bool active);

void oled_set_battery(uint8_t percent);

// ---- Display actions --------------------------------------------------------

/**
 * Show "MODE CHANGED / RESTARTING..." for ~2 s.
 * Intended to be called just before esp_restart().
 */
void oled_show_mode_change(void);

/**
 * Redraw the full screen from current state and push to hardware.
 * Call at ~1 Hz from the OLED update task.
 */
void oled_update(void);
