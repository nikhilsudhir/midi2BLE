#include "battery.h"

#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define TAG "BATT"

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t         s_cali = NULL;

void battery_init(void)
{
    // Oneshot ADC unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id    = BATT_ADC_UNIT,
        .ulp_mode   = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &s_adc));

    // Channel configuration — 12 dB attenuation covers 0–3.3 V
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc, BATT_ADC_CHANNEL, &chan_cfg));

    // Calibration — try curve fitting first, fall back to line fitting
    esp_err_t ret = ESP_FAIL;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id   = BATT_ADC_UNIT,
        .atten     = ADC_ATTEN_DB_12,
        .bitwidth  = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration: curve fitting");
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (ret != ESP_OK) {
        adc_cali_line_fitting_config_t lf_cfg = {
            .unit_id   = BATT_ADC_UNIT,
            .atten     = ADC_ATTEN_DB_12,
            .bitwidth  = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&lf_cfg, &s_cali);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Calibration: line fitting");
        }
    }
#endif

    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "No calibration available — raw ADC readings will be used");
        s_cali = NULL;
    }
}

uint8_t battery_get_percent(void)
{
    if (!s_adc) return 0;

#define BATT_SAMPLES 16
    int raw = 0;
    for (int i = 0; i < BATT_SAMPLES; i++) {
        int r = 0;
        adc_oneshot_read(s_adc, BATT_ADC_CHANNEL, &r);
        raw += r;
    }
    raw /= BATT_SAMPLES;
#undef BATT_SAMPLES

    int voltage_mv = 0;
    if (s_cali) {
        adc_cali_raw_to_voltage(s_cali, raw, &voltage_mv);
    } else {
        // Rough linear mapping: 0–4095 → 0–3300 mV
        voltage_mv = (raw * 3300) / 4095;
    }

    // Scale back up through the resistor divider and apply ADC offset correction
    int batt_mv = voltage_mv * BATT_VOLTAGE_DIVIDER_RATIO + BATT_CAL_OFFSET_MV;

    if (batt_mv >= BATT_FULL_MV)  return 100;
    if (batt_mv <= BATT_EMPTY_MV) return 0;
    return (uint8_t)((batt_mv - BATT_EMPTY_MV) * 100 /
                     (BATT_FULL_MV - BATT_EMPTY_MV));
}
