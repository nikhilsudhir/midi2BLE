#include "mode_manager.h"
#include "oled_display.h"

#include "driver/gpio.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG          "MODE"
#define NVS_NS       "mode_mgr"
#define NVS_KEY      "mode"
#define RESTART_DELAY_MS  2000

static device_mode_t s_mode = MODE_WIRELESS;

void mode_manager_init(void)
{
    // Configure GPIO as input with pull-up (floating → WIRELESS by default)
    gpio_config_t io_cfg = {
        .pin_bit_mask      = (1ULL << MODE_GPIO),
        .mode              = GPIO_MODE_INPUT,
        .pull_up_en        = GPIO_PULLUP_ENABLE,
        .pull_down_en      = GPIO_PULLDOWN_DISABLE,
        .intr_type         = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_cfg);

    // HIGH = WIRELESS, LOW = PASSTHROUGH
    s_mode = gpio_get_level(MODE_GPIO) ? MODE_WIRELESS : MODE_PASSTHROUGH;
    ESP_LOGI(TAG, "Mode: %s",
             s_mode == MODE_WIRELESS ? "WIRELESS" : "PASSTHROUGH");

    // Detect mode change via NVS — restart once to reinitialise cleanly
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READWRITE, &nvs) == ESP_OK) {
        uint8_t stored = 0xFF;
        nvs_get_u8(nvs, NVS_KEY, &stored);

        bool changed = (stored != 0xFF) && (stored != (uint8_t)s_mode);

        // Always persist the current mode
        nvs_set_u8(nvs, NVS_KEY, (uint8_t)s_mode);
        nvs_commit(nvs);
        nvs_close(nvs);

        if (changed) {
            ESP_LOGW(TAG, "Mode changed (was %d, now %d) — restarting",
                     stored, (uint8_t)s_mode);
            oled_show_mode_change();          // blocks for RESTART_DELAY_MS
            esp_restart();
        }
    }
}

device_mode_t mode_manager_get(void)
{
    return s_mode;
}
