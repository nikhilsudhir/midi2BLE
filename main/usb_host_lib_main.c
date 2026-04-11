#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "usb/usb_host.h"

#include "mode_manager.h"
#include "oled_display.h"
#include "battery.h"
#include "ble_midi.h"

#define HOST_LIB_TASK_PRIORITY  2
#define CLASS_TASK_PRIORITY     3
#define OLED_TASK_PRIORITY      1

extern void class_driver_task(void *arg);
extern void class_driver_client_deregister(void);

static const char *TAG = "MAIN";

// ---- USB host task (WIRELESS mode only) ------------------------------------

static void usb_host_lib_task(void *arg)
{
    ESP_LOGI(TAG, "Installing USB Host Library");
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LOWMED,
        .peripheral_map = BIT0,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));

    xTaskNotifyGive(arg);  // signal app_main that USB host is ready

    bool has_clients = true;
    bool has_devices = false;
    while (has_clients) {
        uint32_t event_flags;
        ESP_ERROR_CHECK(usb_host_lib_handle_events(portMAX_DELAY, &event_flags));
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            if (usb_host_device_free_all() == ESP_OK) {
                has_clients = false;
            } else {
                has_devices = true;
            }
        }
        if (has_devices && (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)) {
            has_clients = false;
        }
    }
    ESP_ERROR_CHECK(usb_host_uninstall());
    vTaskSuspend(NULL);
}

// ---- OLED update task (always running) -------------------------------------

static void oled_update_task(void *arg)
{
    while (1) {
        oled_set_battery(battery_get_percent());
        oled_update();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ---- Entry point -----------------------------------------------------------

void app_main(void)
{
    // NVS must be first — BLE, mode_manager, and battery all depend on it
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // OLED before mode_manager so the "MODE CHANGED" screen can be shown
    oled_init();

    // Read mode GPIO and check for mode change (may restart here)
    mode_manager_init();
    device_mode_t mode = mode_manager_get();
    oled_set_mode(mode);

    battery_init();

    // Start the 1 Hz OLED update task
    xTaskCreate(oled_update_task, "oled", 3072, NULL, OLED_TASK_PRIORITY, NULL);

    if (mode == MODE_WIRELESS) {
        ESP_LOGI(TAG, "Starting in WIRELESS mode");

        // BLE MIDI peripheral (NVS already initialised above)
        ble_midi_init();

        // USB host library task
        BaseType_t ok;
        ok = xTaskCreatePinnedToCore(usb_host_lib_task,
                                     "usb_host",
                                     4096,
                                     xTaskGetCurrentTaskHandle(),
                                     HOST_LIB_TASK_PRIORITY,
                                     NULL, 0);
        assert(ok == pdTRUE);

        // Wait for USB host to be installed before spawning the class driver
        ulTaskNotifyTake(false, pdMS_TO_TICKS(1000));

        // Class driver task (large stack — BLE notify runs in this context)
        ok = xTaskCreatePinnedToCore(class_driver_task,
                                     "class",
                                     10 * 1024,
                                     NULL,
                                     CLASS_TASK_PRIORITY,
                                     NULL, 0);
        assert(ok == pdTRUE);

    } else {
        // PASSTHROUGH — neither BLE nor USB host is started.
        // The hardware routes the MIDI signal directly; the ESP32-S3 only
        // manages the display and battery monitor.
        ESP_LOGI(TAG, "Starting in PASSTHROUGH (wired) mode");
    }
}
