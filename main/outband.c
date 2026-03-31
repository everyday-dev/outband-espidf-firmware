#include <stdio.h>
#include "outband.h"

static const char *TAG = "OUTBAND";
static const char *device_name = "Outband_espidf_demo";

static uint8_t ble_addr_type;

uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;

static int ble_gap_event(struct ble_gap_event *event, void *arg);
static void on_sync(void);
static void start_advertising(void);
static void ble_host_task(void *param);

static int ble_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            ESP_LOGI(TAG, "Connection established; status=%d", event->connect.status);
            g_conn_handle = event->connect.conn_handle;
        }
        else
        {
            ESP_LOGE(TAG, "Error: Connection failed; status=%d", event->connect.status);
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "Disconnect; reason=%d", event->disconnect.reason);
        g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "Advertise complete; reason=%d", event->adv_complete.reason);
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "MTU update event; conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        return 0;
    }

    return 0;
}

static void on_sync(void)
{
    int rc;

    rc = ble_hs_id_infer_auto(0, &ble_addr_type);
    assert(rc == 0);

    uint8_t addr_val[6] = {0};
    rc = ble_hs_id_copy_addr(ble_addr_type, addr_val, NULL);

    start_advertising();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting state; reason=%d\n", reason);
}

static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    memset(&fields, 0, sizeof(fields));
    memset(&rsp_fields, 0, sizeof(rsp_fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error setting adv fields; rc=%d", rc);
        return;
    }

    rsp_fields.uuids128 = (ble_uuid128_t[]){wifi_svc_uuid};
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error setting rsp adv fields; rc=%d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "Error enabling advertising; rc=%d", rc);
        return;
    }
    ESP_LOGI(TAG, "Advertising Started...");
}

static void ble_host_task(void *param)
{
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();

    nimble_port_freertos_deinit();
}

void app_main(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    nimble_port_init();

    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;

    gatt_svr_init();
    ble_svc_gap_device_name_set("Outband Demo espidf");

    nimble_port_freertos_init(ble_host_task);

    xTaskCreate(ecg_manager_task, "ecg_task", 2048, NULL, 5, NULL);
    xTaskCreate(wifi_manager_task, "wifi_task", 2048, NULL, 3, NULL);
    xTaskCreate(thermostat_manager_task, "thermo_task", 2048, NULL, 3, NULL);
}