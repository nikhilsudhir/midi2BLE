/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "usb/usb_host.h"

#define CLIENT_NUM_EVENT_MSG        5

typedef enum {
    ACTION_OPEN_DEV         = (1 << 0),
    ACTION_GET_DEV_INFO     = (1 << 1),
    ACTION_GET_DEV_DESC     = (1 << 2),
    ACTION_GET_CONFIG_DESC  = (1 << 3),
    ACTION_GET_STR_DESC     = (1 << 4),
    ACTION_CLOSE_DEV        = (1 << 5),
    ACTION_CLAIM_MIDI_INTF  = (1 << 6),
} action_t;

#define DEV_MAX_COUNT           128

typedef struct {
    usb_host_client_handle_t client_hdl;
    uint8_t dev_addr;
    usb_device_handle_t dev_hdl;
    action_t actions;
    struct {
        uint8_t intf_num;
        uint8_t ep_addr;
        usb_transfer_t *transfer;
        bool claimed;
        bool cancelling;
    } midi;
} usb_device_t;

typedef struct {
    struct {
        union {
            struct {
                uint8_t unhandled_devices: 1;   /**< Device has unhandled devices */
                uint8_t shutdown: 1;            /**<  */
                uint8_t reserved6: 6;           /**< Reserved */
            };
            uint8_t val;                        /**< Class drivers' flags value */
        } flags;                                /**< Class drivers' flags */
        usb_device_t device[DEV_MAX_COUNT];     /**< Class drivers' static array of devices */
    } mux_protected;                            /**< Mutex protected members. Must be protected by the Class mux_lock when accessed */

    struct {
        usb_host_client_handle_t client_hdl;
        SemaphoreHandle_t mux_lock;         /**< Mutex for protected members */
    } constant;                                 /**< Constant members. Do not change after installation thus do not require a critical section or mutex */
} class_driver_t;

static const char *TAG = "CLASS";
static class_driver_t *s_driver_obj;

static void client_event_cb(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    class_driver_t *driver_obj = (class_driver_t *)arg;
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
        // Save the device address
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        driver_obj->mux_protected.device[event_msg->new_dev.address].dev_addr = event_msg->new_dev.address;
        driver_obj->mux_protected.device[event_msg->new_dev.address].dev_hdl = NULL;
        // Open the device next
        driver_obj->mux_protected.device[event_msg->new_dev.address].actions |= ACTION_OPEN_DEV;
        // Set flag
        driver_obj->mux_protected.flags.unhandled_devices = 1;
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    case USB_HOST_CLIENT_EVENT_DEV_GONE:
        // Cancel any other actions and close the device next
        xSemaphoreTake(driver_obj->constant.mux_lock, portMAX_DELAY);
        for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
            if (driver_obj->mux_protected.device[i].dev_hdl == event_msg->dev_gone.dev_hdl) {
                driver_obj->mux_protected.device[i].actions = ACTION_CLOSE_DEV;
                // Set flag
                driver_obj->mux_protected.flags.unhandled_devices = 1;
            }
        }
        xSemaphoreGive(driver_obj->constant.mux_lock);
        break;
    default:
        ESP_LOGW(TAG, "Unsupported client event: %d (possibly suspend/resume)", event_msg->event);
        break;
    }
}

// ---- MIDI helpers -----------------------------------------------------------

#define MIDI_TRANSFER_SIZE  64  // accommodates up to 16 four-byte MIDI event packets

/**
 * Walk the active config descriptor and return the first Bulk IN endpoint inside
 * a MIDI Streaming interface (class 0x01, subclass 0x03).
 * Returns true and fills *intf_num_out / *ep_addr_out on success.
 */
static bool find_midi_in_endpoint(usb_device_handle_t dev_hdl,
                                  uint8_t *intf_num_out, uint8_t *ep_addr_out)
{
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(dev_hdl, &config_desc));

    const uint8_t *p   = (const uint8_t *)config_desc;
    const uint8_t *end = p + config_desc->wTotalLength;
    bool in_midi_intf  = false;
    uint8_t intf_num   = 0;

    while (p < end) {
        if (p[0] < 2) break;
        uint8_t desc_type = p[1];

        if (desc_type == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
            const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
            // Audio class (0x01), MIDI Streaming subclass (0x03)
            in_midi_intf = (intf->bInterfaceClass    == 0x01 &&
                            intf->bInterfaceSubClass  == 0x03);
            if (in_midi_intf) {
                intf_num = intf->bInterfaceNumber;
            }
        }

        if (in_midi_intf && desc_type == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            const usb_ep_desc_t *ep = (const usb_ep_desc_t *)p;
            bool is_bulk = (ep->bmAttributes & 0x03) == 0x02;
            bool is_in   = (ep->bEndpointAddress & 0x80) != 0;
            if (is_bulk && is_in) {
                *intf_num_out = intf_num;
                *ep_addr_out  = ep->bEndpointAddress;
                return true;
            }
        }

        p += p[0];
    }
    return false;
}

/**
 * Transfer callback – parses 4-byte USB MIDI event packets and prints them.
 * Resubmits the transfer unless the device is closing.
 */
static void midi_transfer_cb(usb_transfer_t *transfer)
{
    usb_device_t *device_obj = (usb_device_t *)transfer->context;

    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        const uint8_t *data = transfer->data_buffer;
        int len = transfer->actual_num_bytes;
        // Each USB MIDI event packet is exactly 4 bytes
        for (int i = 0; i + 3 < len; i += 4) {
            // Skip all-zero padding packets
            if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 0 && data[i+3] == 0) {
                continue;
            }
            ESP_LOGI("MIDI", "%02X %02X %02X %02X",
                     data[i], data[i+1], data[i+2], data[i+3]);
        }
    } else {
        ESP_LOGW("MIDI", "Transfer status %d", transfer->status);
    }

    // Don't resubmit if device is closing
    if (device_obj->midi.cancelling) {
        return;
    }

    esp_err_t err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
        ESP_LOGE("MIDI", "Resubmit failed: %s", esp_err_to_name(err));
    }
}

/**
 * Claim the MIDI Streaming interface, allocate a transfer, and kick off the
 * first read.  Called after the config descriptor has been printed.
 */
static void action_claim_midi_intf(usb_device_t *device_obj)
{
    uint8_t intf_num, ep_addr;
    if (!find_midi_in_endpoint(device_obj->dev_hdl, &intf_num, &ep_addr)) {
        ESP_LOGW(TAG, "No MIDI Streaming interface found – skipping");
        return;
    }

    ESP_LOGI(TAG, "Claiming MIDI interface %d, IN EP 0x%02X", intf_num, ep_addr);
    esp_err_t err = usb_host_interface_claim(device_obj->client_hdl,
                                             device_obj->dev_hdl, intf_num, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Interface claim failed: %s", esp_err_to_name(err));
        return;
    }

    device_obj->midi.intf_num   = intf_num;
    device_obj->midi.ep_addr    = ep_addr;
    device_obj->midi.claimed    = true;
    device_obj->midi.cancelling = false;

    err = usb_host_transfer_alloc(MIDI_TRANSFER_SIZE, 0, &device_obj->midi.transfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Transfer alloc failed: %s", esp_err_to_name(err));
        usb_host_interface_release(device_obj->client_hdl, device_obj->dev_hdl, intf_num);
        device_obj->midi.claimed = false;
        return;
    }

    usb_transfer_t *xfer   = device_obj->midi.transfer;
    xfer->device_handle    = device_obj->dev_hdl;
    xfer->bEndpointAddress = ep_addr;
    xfer->callback         = midi_transfer_cb;
    xfer->context          = device_obj;
    xfer->num_bytes        = MIDI_TRANSFER_SIZE;
    xfer->timeout_ms       = 0;  // no timeout for IN transfers

    err = usb_host_transfer_submit(xfer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initial transfer submit failed: %s", esp_err_to_name(err));
    }
}

// ---- end MIDI helpers -------------------------------------------------------

static void action_open_dev(usb_device_t *device_obj)
{
    assert(device_obj->dev_addr != 0);
    ESP_LOGI(TAG, "Opening device at address %d", device_obj->dev_addr);
    ESP_ERROR_CHECK(usb_host_device_open(device_obj->client_hdl, device_obj->dev_addr, &device_obj->dev_hdl));
    // Get the device's information next
    device_obj->actions |= ACTION_GET_DEV_INFO;
}

static void action_get_info(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device information");
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    ESP_LOGI(TAG, "\t%s speed", (char *[]) {
        "Low", "Full", "High"
    }[dev_info.speed]);
    ESP_LOGI(TAG, "\tParent info:");
    if (dev_info.parent.dev_hdl) {
        usb_device_info_t parent_dev_info;
        ESP_ERROR_CHECK(usb_host_device_info(dev_info.parent.dev_hdl, &parent_dev_info));
        ESP_LOGI(TAG, "\t\tBus addr: %d", parent_dev_info.dev_addr);
        ESP_LOGI(TAG, "\t\tPort: %d", dev_info.parent.port_num);

    } else {
        ESP_LOGI(TAG, "\t\tPort: ROOT");
    }
    ESP_LOGI(TAG, "\tbConfigurationValue %d", dev_info.bConfigurationValue);
    // Get the device descriptor next
    device_obj->actions |= ACTION_GET_DEV_DESC;
}

static void action_get_dev_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting device descriptor");
    const usb_device_desc_t *dev_desc;
    ESP_ERROR_CHECK(usb_host_get_device_descriptor(device_obj->dev_hdl, &dev_desc));
    usb_print_device_descriptor(dev_desc);
    // Get the device's config descriptor next
    device_obj->actions |= ACTION_GET_CONFIG_DESC;
}

static void action_get_config_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    ESP_LOGI(TAG, "Getting config descriptor");
    const usb_config_desc_t *config_desc;
    ESP_ERROR_CHECK(usb_host_get_active_config_descriptor(device_obj->dev_hdl, &config_desc));
    usb_print_config_descriptor(config_desc, NULL);
    // Attempt to claim the MIDI Streaming interface if the device exposes one
    device_obj->actions |= ACTION_CLAIM_MIDI_INTF;
    // Get the device's string descriptors next
    device_obj->actions |= ACTION_GET_STR_DESC;
}

static void action_get_str_desc(usb_device_t *device_obj)
{
    assert(device_obj->dev_hdl != NULL);
    usb_device_info_t dev_info;
    ESP_ERROR_CHECK(usb_host_device_info(device_obj->dev_hdl, &dev_info));
    if (dev_info.str_desc_manufacturer) {
        ESP_LOGI(TAG, "Getting Manufacturer string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_manufacturer);
    }
    if (dev_info.str_desc_product) {
        ESP_LOGI(TAG, "Getting Product string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_product);
    }
    if (dev_info.str_desc_serial_num) {
        ESP_LOGI(TAG, "Getting Serial Number string descriptor");
        usb_print_string_descriptor(dev_info.str_desc_serial_num);
    }
}

static void action_close_dev(usb_device_t *device_obj)
{
    if (device_obj->midi.claimed) {
        // Signal callback not to resubmit, then release interface and free transfer.
        // The USB host library completes any in-flight transfer with NO_DEVICE status
        // before delivering DEV_GONE, so freeing here is safe.
        device_obj->midi.cancelling = true;
        usb_host_interface_release(device_obj->client_hdl,
                                   device_obj->dev_hdl,
                                   device_obj->midi.intf_num);
        device_obj->midi.claimed = false;
        if (device_obj->midi.transfer) {
            usb_host_transfer_free(device_obj->midi.transfer);
            device_obj->midi.transfer = NULL;
        }
    }
    ESP_ERROR_CHECK(usb_host_device_close(device_obj->client_hdl, device_obj->dev_hdl));
    device_obj->dev_hdl = NULL;
    device_obj->dev_addr = 0;
}

static void class_driver_device_handle(usb_device_t *device_obj)
{
    uint8_t actions = device_obj->actions;
    device_obj->actions = 0;

    while (actions) {
        if (actions & ACTION_OPEN_DEV) {
            action_open_dev(device_obj);
        }
        if (actions & ACTION_GET_DEV_INFO) {
            action_get_info(device_obj);
        }
        if (actions & ACTION_GET_DEV_DESC) {
            action_get_dev_desc(device_obj);
        }
        if (actions & ACTION_GET_CONFIG_DESC) {
            action_get_config_desc(device_obj);
        }
        if (actions & ACTION_GET_STR_DESC) {
            action_get_str_desc(device_obj);
        }
        if (actions & ACTION_CLAIM_MIDI_INTF) {
            action_claim_midi_intf(device_obj);
        }
        if (actions & ACTION_CLOSE_DEV) {
            action_close_dev(device_obj);
        }

        actions = device_obj->actions;
        device_obj->actions = 0;
    }
}

void class_driver_task(void *arg)
{
    class_driver_t driver_obj = {0};
    usb_host_client_handle_t class_driver_client_hdl = NULL;

    ESP_LOGI(TAG, "Registering Client");

    SemaphoreHandle_t mux_lock = xSemaphoreCreateMutex();
    if (mux_lock == NULL) {
        ESP_LOGE(TAG, "Unable to create class driver mutex");
        vTaskSuspend(NULL);
        return;
    }

    usb_host_client_config_t client_config = {
        .is_synchronous = false,    //Synchronous clients currently not supported. Set this to false
        .max_num_event_msg = CLIENT_NUM_EVENT_MSG,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = (void *) &driver_obj,
        },
    };
    ESP_ERROR_CHECK(usb_host_client_register(&client_config, &class_driver_client_hdl));

    driver_obj.constant.mux_lock = mux_lock;
    driver_obj.constant.client_hdl = class_driver_client_hdl;

    for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
        driver_obj.mux_protected.device[i].client_hdl = class_driver_client_hdl;
    }

    s_driver_obj = &driver_obj;

    while (1) {
        // Driver has unhandled devices, handle all devices first
        if (driver_obj.mux_protected.flags.unhandled_devices) {
            xSemaphoreTake(driver_obj.constant.mux_lock, portMAX_DELAY);
            for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
                if (driver_obj.mux_protected.device[i].actions) {
                    class_driver_device_handle(&driver_obj.mux_protected.device[i]);
                }
            }
            driver_obj.mux_protected.flags.unhandled_devices = 0;
            xSemaphoreGive(driver_obj.constant.mux_lock);
        } else {
            // Driver is active, handle client events
            if (driver_obj.mux_protected.flags.shutdown == 0) {
                usb_host_client_handle_events(class_driver_client_hdl, portMAX_DELAY);
            } else {
                // Shutdown the driver
                break;
            }
        }
    }

    ESP_LOGI(TAG, "Deregistering Class Client");
    ESP_ERROR_CHECK(usb_host_client_deregister(class_driver_client_hdl));
    if (mux_lock != NULL) {
        vSemaphoreDelete(mux_lock);
    }
    vTaskSuspend(NULL);
}

void class_driver_client_deregister(void)
{
    // Mark all opened devices
    xSemaphoreTake(s_driver_obj->constant.mux_lock, portMAX_DELAY);
    for (uint8_t i = 0; i < DEV_MAX_COUNT; i++) {
        if (s_driver_obj->mux_protected.device[i].dev_hdl != NULL) {
            // Mark device to close
            s_driver_obj->mux_protected.device[i].actions |= ACTION_CLOSE_DEV;
            // Set flag
            s_driver_obj->mux_protected.flags.unhandled_devices = 1;
        }
    }
    s_driver_obj->mux_protected.flags.shutdown = 1;
    xSemaphoreGive(s_driver_obj->constant.mux_lock);

    // Unblock, exit the loop and proceed to deregister client
    ESP_ERROR_CHECK(usb_host_client_unblock(s_driver_obj->constant.client_hdl));
}
