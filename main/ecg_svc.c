#include "outband.h"

#define ECG_CHR_WAVE 1
#define ECG_CHR_BPM 2

static const char *TAG = "ECG_SVC";

uint16_t ecg_wave_handle;
uint16_t ecg_bpm_handle;

static uint8_t g_last_bpm = 0;
static int g_phase = 0;
static int g_heart_rate_period = 50;

static int gatt_svr_chr_access_ecg(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static float generate_ecg_point();

const struct ble_gatt_svc_def ecg_svc_definition[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &ecg_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                /* Waveform Notify */
                .uuid = &ecg_wave_uuid.u,
                .access_cb = gatt_svr_chr_access_ecg,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .arg = (void *)ECG_CHR_WAVE,
                .val_handle = &ecg_wave_handle,
            },
            {
                .uuid = &ecg_bpm_uuid.u,
                .access_cb = gatt_svr_chr_access_ecg,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .arg = (void *)ECG_CHR_BPM,
                .val_handle = &ecg_bpm_handle,
            },
            {0}},
    },
    {0}};

static int gatt_svr_chr_access_ecg(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uintptr_t chr_id = (uintptr_t)arg;

    if (chr_id == ECG_CHR_BPM)
    {
        return os_mbuf_append(ctxt->om, &g_last_bpm, 1);
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static float generate_ecg_point()
{
    g_phase++;

    if (g_phase >= g_heart_rate_period)
    {
        g_phase = 0;

        g_heart_rate_period = 48 + (rand() % 5);

        uint8_t current_bpm = (uint8_t)(60000 / (g_heart_rate_period * 20));

        if (current_bpm != g_last_bpm)
        {
            g_last_bpm = current_bpm;
            struct os_mbuf *om = ble_hs_mbuf_from_flat(&g_last_bpm, 1);

            if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE)
                ble_gatts_notify_custom(g_conn_handle, ecg_bpm_handle, om);
        }
    }

    // Simulate P-wave
    if (g_phase >= 5 && g_phase <= 8)
        return 0.15f;
    // Simulate QRS Complex
    if (g_phase == 12)
        return -0.1f;
    if (g_phase == 13)
        return 0.98f + ((rand() % 40) / 1000.0f);
    if (g_phase == 14)
        return -0.2f;
    // Simulate T-wave
    if (g_phase >= 22 && g_phase <= 26)
        return 0.22f;

    // Baseline noise
    return (float)(rand() % 10) / 1000.0f;
}

void ecg_manager_task(void *pvParameters)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t frequency = pdMS_TO_TICKS(20);

    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Task started.");

    while (1)
    {
        if (g_conn_handle == BLE_HS_CONN_HANDLE_NONE)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        last_wake_time = xTaskGetTickCount();
        vTaskDelayUntil(&last_wake_time, frequency);

        float val = generate_ecg_point();
        uint8_t byte_val = (uint8_t)(val * 255);

        struct os_mbuf *om = ble_hs_mbuf_from_flat(&byte_val, 1);

        if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE)
            ble_gatts_notify_custom(g_conn_handle, ecg_wave_handle, om);
    }
}
