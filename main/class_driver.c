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
#include "ble_midi.h"
#include "oled_display.h"

#define CLIENT_NUM_EVENT_MSG        5

typedef enum {
    ACTION_OPEN_DEV         = (1 << 0),
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

// Active note tracking: [channel 0-15][note 0-127]
static bool s_active_notes[16][128];

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

/*
 * USB MIDI Code Index Number → number of MIDI bytes in the packet.
 * CIN 0x0 and 0x1 are reserved/misc and skipped (len 0).
 */
static const uint8_t s_cin_to_len[16] = {
    0, 0, 2, 3,  /* 0-3 */
    3, 1, 2, 3,  /* 4-7 */
    3, 3, 3, 3,  /* 8-B: Note Off, Note On, Poly, CC */
    2, 2, 3, 1,  /* C-F: PGM, Chan Pressure, Pitch Bend, 1-byte */
};

/**
 * Transfer callback – parses 4-byte USB MIDI event packets, logs them,
 * and forwards the raw MIDI bytes over BLE MIDI.
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

            // Convert USB MIDI packet to raw MIDI bytes and send over BLE
            uint8_t cin      = data[i] & 0x0F;
            int     midi_len = s_cin_to_len[cin];
            if (midi_len > 0) {
                ble_midi_send(&data[i + 1], midi_len);
                oled_set_midi_active(true);

                // Track Note On/Off for panic on disconnect
                uint8_t status   = data[i + 1];
                uint8_t msg_type = status & 0xF0;
                uint8_t channel  = status & 0x0F;
                if (midi_len == 3) {
                    uint8_t note = data[i + 2];
                    uint8_t vel  = data[i + 3];
                    if (msg_type == 0x90 && vel > 0) {
                        s_active_notes[channel][note] = true;
                    } else if (msg_type == 0x80 || (msg_type == 0x90 && vel == 0)) {
                        s_active_notes[channel][note] = false;
                    }
                }
            }
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

    // Build full device name: "<Manufacturer> <Product>" from USB string descriptors.
    // Converts UTF-16LE → ASCII; non-ASCII characters become '?'.
    char dev_name[44] = {0};
    int out = 0;

    usb_device_info_t info;
    if (usb_host_device_info(device_obj->dev_hdl, &info) == ESP_OK) {
        // Append manufacturer string
        if (info.str_desc_manufacturer != NULL) {
            int chars = (info.str_desc_manufacturer->bLength - 2) / 2;
            for (int i = 0; i < chars && out < (int)sizeof(dev_name) - 2; i++) {
                uint16_t wc = info.str_desc_manufacturer->wData[i];
                dev_name[out++] = (wc >= 0x20 && wc < 0x80) ? (char)wc : '?';
            }
        }
        // Append a space then product string
        if (info.str_desc_product != NULL) {
            if (out > 0 && out < (int)sizeof(dev_name) - 1) {
                dev_name[out++] = ' ';
            }
            int chars = (info.str_desc_product->bLength - 2) / 2;
            for (int i = 0; i < chars && out < (int)sizeof(dev_name) - 1; i++) {
                uint16_t wc = info.str_desc_product->wData[i];
                dev_name[out++] = (wc >= 0x20 && wc < 0x80) ? (char)wc : '?';
            }
        }
    }
    oled_set_usb_connected(true, dev_name);

    device_obj->actions |= ACTION_CLAIM_MIDI_INTF;
}

// Send Note OFF for every tracked active note, then All Notes Off + All Sound Off per channel.
// Called on USB disconnect before tearing down the device.
static void midi_panic(void)
{
    ESP_LOGW("MIDI", "Panic: releasing all active notes");

    for (int ch = 0; ch < 16; ch++) {
        for (int note = 0; note < 128; note++) {
            if (s_active_notes[ch][note]) {
                uint8_t msg[3] = { 0x80 | ch, note, 0 };
                ble_midi_send(msg, 3);
                s_active_notes[ch][note] = false;
            }
        }
    }

    for (int ch = 0; ch < 16; ch++) {
        uint8_t all_notes_off[3] = { 0xB0 | ch, 123, 0 };  // CC 123: All Notes Off
        ble_midi_send(all_notes_off, 3);
        uint8_t all_sound_off[3] = { 0xB0 | ch, 120, 0 };  // CC 120: All Sound Off
        ble_midi_send(all_sound_off, 3);
    }
}

static void action_close_dev(usb_device_t *device_obj)
{
    oled_set_usb_connected(false, NULL);

    // Fire panic before teardown — all transfers are already complete at this point
    // (ESP-IDF USB host guarantees DEV_GONE is only delivered after transfers finish)
    midi_panic();

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
