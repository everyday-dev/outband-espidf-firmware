#include "outband.h"

#define CONSTRAIN(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))
#define THERMO_CHR_SETPOINT 1
#define THERMO_CHR_TEMP 2
#define THERMO_CHR_STATE 3

static const char *TAG = "THERMO_SVC";

uint16_t thermo_setpoint_handle;
uint16_t thermo_temp_handle;
uint16_t thermo_state_handle;

typedef enum
{
    THERMOSTAT_OFF = 0,
    THERMOSTAT_AUTO = 1,
    THERMOSTAT_HEATING = 2,
    THERMOSTAT_COOLING = 3,
    THERMOSTAT__MAX__ = 4
} thermo_state_t;

static uint8_t g_setpoint = 68;
static uint8_t g_current_temp = 70;
static thermo_state_t g_state = THERMOSTAT_AUTO;

static int gatt_svr_chr_access_thermo(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

const struct ble_gatt_svc_def thermo_svc_definition[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &thermo_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &thermo_setpoint_uuid.u,
                .access_cb = gatt_svr_chr_access_thermo,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .arg = (void *)THERMO_CHR_SETPOINT,
                .val_handle = &thermo_setpoint_handle,
            },
            {
                .uuid = &thermo_temp_uuid.u,
                .access_cb = gatt_svr_chr_access_thermo,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .arg = (void *)THERMO_CHR_TEMP,
                .val_handle = &thermo_temp_handle,
            },
            {
                .uuid = &thermo_state_uuid.u,
                .access_cb = gatt_svr_chr_access_thermo,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .arg = (void *)THERMO_CHR_STATE,
                .val_handle = &thermo_state_handle,
            },
            {0}},
    },
    {0}};

static int gatt_svr_chr_access_thermo(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uintptr_t chr_id = (uintptr_t)arg;

    switch (chr_id)
    {
    case THERMO_CHR_SETPOINT:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
        {
            uint8_t val;
            ble_hs_mbuf_to_flat(ctxt->om, &val, 1, NULL);
            g_setpoint = CONSTRAIN(val, 60, 80);

            struct os_mbuf *om = ble_hs_mbuf_from_flat(&g_setpoint, 1);

            return ble_gatts_notify_custom(conn_handle, thermo_setpoint_handle, om);
        }
        return os_mbuf_append(ctxt->om, &g_setpoint, 1);

    case THERMO_CHR_TEMP:
        return os_mbuf_append(ctxt->om, &g_current_temp, 1);

    case THERMO_CHR_STATE:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
        {
            uint8_t val;
            ble_hs_mbuf_to_flat(ctxt->om, &val, 1, NULL);
            g_state = (val >= THERMOSTAT__MAX__) ? THERMOSTAT_OFF : (thermo_state_t)val;

            struct os_mbuf *om = ble_hs_mbuf_from_flat(&g_state, 1);

            return ble_gatts_notify_custom(conn_handle, thermo_state_handle, om);
        }
        return os_mbuf_append(ctxt->om, &g_state, 1);
    }
    return BLE_ATT_ERR_UNLIKELY;
}

void thermostat_manager_task(void *pvParameters)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    ESP_LOGI(TAG, "Task started.");

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(3000));

        bool changed = false;
        switch (g_state)
        {
        case THERMOSTAT_HEATING:
            if (g_current_temp < g_setpoint)
            {
                g_current_temp++;
                changed = true;
            }
            break;
        case THERMOSTAT_COOLING:
            if (g_current_temp > g_setpoint)
            {
                g_current_temp--;
                changed = true;
            }
            break;
        case THERMOSTAT_AUTO:
            if (g_current_temp < g_setpoint)
            {
                g_current_temp++;
                changed = true;
            }
            else if (g_current_temp > g_setpoint)
            {
                g_current_temp--;
                changed = true;
            }
            break;
        default:
            break;
        }

        if (changed)
        {
            ESP_LOGI(TAG, "Temp updated: %d (Setpoint: %d)", g_current_temp, g_setpoint);
            struct os_mbuf *om = ble_hs_mbuf_from_flat(&g_current_temp, 1);

            if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE)
                ble_gatts_notify_custom(g_conn_handle, thermo_temp_handle, om);
        }
    }
}
