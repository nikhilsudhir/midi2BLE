/*
 * BLE MIDI peripheral (Apple BLE MIDI spec / MIDI over Bluetooth LE).
 *
 * Service UUID  : 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
 * Char UUID     : 7772E5DB-3868-4112-A1A9-F2669D106BF3
 *
 * macOS connects via Audio MIDI Setup → Bluetooth, then the device
 * appears as a standard MIDI port in any DAW (e.g. FL Studio via IAC).
 */

#include "ble_midi.h"
#include "oled_display.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define TAG         "BLE_MIDI"
#define DEVICE_NAME "MIDI2BLE"

/* ---- UUIDs (stored little-endian as NimBLE expects) ---- */

// Service: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700
static const ble_uuid128_t midi_svc_uuid = BLE_UUID128_INIT(
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7,
    0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03
);

// Characteristic: 7772E5DB-3868-4112-A1A9-F2669D106BF3
static const ble_uuid128_t midi_chr_uuid = BLE_UUID128_INIT(
    0xF3, 0x6B, 0x10, 0x9D, 0x66, 0xF2, 0xA9, 0xA1,
    0x12, 0x41, 0x68, 0x38, 0xDB, 0xE5, 0x72, 0x77
);

static uint16_t s_midi_chr_handle = 0;
static uint16_t s_conn_handle     = BLE_HS_CONN_HANDLE_NONE;

/* ---- GATT ---- */

static int midi_chr_access(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    // The host (Mac) may write to this characteristic; we don't need the data.
    return 0;
}

static const struct ble_gatt_svc_def s_midi_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &midi_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid       = &midi_chr_uuid.u,
                .access_cb  = midi_chr_access,
                .val_handle = &s_midi_chr_handle,
                .flags      = BLE_GATT_CHR_F_READ       |
                              BLE_GATT_CHR_F_WRITE       |
                              BLE_GATT_CHR_F_WRITE_NO_RSP|
                              BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }, /* end characteristics */
        },
    },
    { 0 }, /* end services */
};

/* ---- GAP / advertising ---- */

static void start_advertising(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            ESP_LOGI(TAG, "Host connected (handle %d)", event->connect.conn_handle);
            s_conn_handle = event->connect.conn_handle;
            oled_set_ble_connected(true);
        } else {
            ESP_LOGW(TAG, "Connection failed (%d), restarting adv",
                     event->connect.status);
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            oled_set_ble_connected(false);
            start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Host disconnected (reason %d)", event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        oled_set_ble_connected(false);
        start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        // Mac lost its bond info and wants to re-pair. Delete the stale bond
        // and accept the new pairing so reconnect works without manual steps.
        {
            struct ble_gap_conn_desc desc;
            ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
            ble_gap_unpair(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    default:
        break;
    }
    return 0;
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name             = (uint8_t *)DEVICE_NAME;
    fields.name_len         = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.uuids128         = &midi_svc_uuid;
    fields.num_uuids128     = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields error: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, gap_event_cb, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_start error: %d", rc);
    } else {
        ESP_LOGI(TAG, "Advertising as \"%s\"", DEVICE_NAME);
    }
}

/* ---- NimBLE host callbacks ---- */

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);
    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE stack reset, reason=%d", reason);
}

static void nimble_host_task(void *param)
{
    nimble_port_run(); /* blocks until nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* ---- Public API ---- */

void ble_midi_init(void)
{
    // NVS is initialised in app_main before this function is called.
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return;
    }

    ble_hs_cfg.sync_cb  = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_midi_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(s_midi_svcs);
    assert(rc == 0);

    rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    assert(rc == 0);

    nimble_port_freertos_init(nimble_host_task);
    ESP_LOGI(TAG, "BLE MIDI initialized");
}

void ble_midi_send(const uint8_t *midi_bytes, int len)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || len <= 0 || len > 3) {
        return;
    }

    /* BLE MIDI packet: [header][timestamp_low][midi byte 0..n] */
    uint16_t ts     = (uint16_t)((esp_timer_get_time() / 1000) & 0x1FFF);
    uint8_t ts_high = (ts >> 7) & 0x3F;
    uint8_t ts_low  = ts & 0x7F;

    uint8_t pkt[2 + 3];
    pkt[0] = 0x80 | ts_high;
    pkt[1] = 0x80 | ts_low;
    memcpy(&pkt[2], midi_bytes, len);

    struct os_mbuf *om = ble_hs_mbuf_from_flat(pkt, 2 + len);
    if (!om) {
        ESP_LOGE(TAG, "mbuf alloc failed");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_midi_chr_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed: %d", rc);
    }
}
