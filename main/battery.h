#pragma once

#include <stdint.h>
#include "esp_adc/adc_oneshot.h"

// ADC channel connected to the battery voltage divider.
// Adjust to match the PCB net once schematic is finalised.
// Default: ADC1 channel 3 = GPIO4 (ESP32-S3).
#define BATT_ADC_UNIT    ADC_UNIT_1
#define BATT_ADC_CHANNEL ADC_CHANNEL_3

// Assumed resistor divider ratio (Vsense = Vbatt / RATIO).
// A 100 kΩ / 100 kΩ divider gives ratio 2.
#define BATT_VOLTAGE_DIVIDER_RATIO  2

// LiPo voltage range (mV)
#define BATT_FULL_MV   4200
#define BATT_EMPTY_MV  3000

/**
 * Initialise the ADC peripheral and calibration scheme.
 * Call once from app_main before battery_get_percent().
 */
void battery_init(void);

/**
 * Return the estimated battery charge as a percentage (0–100).
 * Based on the LiPo discharge curve linearised between BATT_EMPTY_MV
 * and BATT_FULL_MV.
 */
uint8_t battery_get_percent(void);
