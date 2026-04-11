#pragma once

#include "driver/gpio.h"

// Physical pin connected to the mode-select switch.
// HIGH = WIRELESS (BLE), LOW = PASSTHROUGH (wired).
#define MODE_GPIO  GPIO_NUM_2

typedef enum {
    MODE_WIRELESS    = 0,
    MODE_PASSTHROUGH = 1,
} device_mode_t;

/**
 * Read the mode GPIO at boot and detect mode changes via NVS.
 * If the mode changed since the last boot, shows a restart screen on the OLED
 * and calls esp_restart() — this function does not return in that case.
 * NVS must be initialised before calling this.
 */
void mode_manager_init(void);

/**
 * Return the mode determined at boot. Valid after mode_manager_init().
 */
device_mode_t mode_manager_get(void);
