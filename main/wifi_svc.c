#include "outband.h"
#include "esp_wifi.h"

#define WIFI_CHR_SSID 1
#define WIFI_CHR_PSK 2
#define WIFI_CHR_STATE 3

static const char *TAG = "WIFI_SVC";

uint16_t wifi_ssid_handle;
uint16_t wifi_state_handle;

typedef enum
{
    WIFI_IDLE = 0,
    WIFI_CONNECTING = 1,
    WIFI_CONNECTED = 2,
    WIFI_ERROR = 3
} wifi_state_t;

static wifi_state_t g_wifi_state = WIFI_IDLE;
static char g_ssid[33] = {0};
static char g_psk[64] = {0};
static bool g_connect_requested = false;

static int gatt_svr_chr_access_wifi(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static void notify_state_change(wifi_state_t new_state);

const struct ble_gatt_svc_def wifi_svc_definition[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &wifi_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid = &wifi_ssid_uuid.u,
                .access_cb = gatt_svr_chr_access_wifi,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .arg = (void *)WIFI_CHR_SSID,
                .val_handle = &wifi_ssid_handle,
            },
            {
                .uuid = &wifi_psk_uuid.u,
                .access_cb = gatt_svr_chr_access_wifi,
                .flags = BLE_GATT_CHR_F_WRITE,
                .arg = (void *)WIFI_CHR_PSK,
            },
            {
                .uuid = &wifi_state_uuid.u,
                .access_cb = gatt_svr_chr_access_wifi,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .arg = (void *)WIFI_CHR_STATE,
                .val_handle = &wifi_state_handle,
            },
            {0}},
    },
    {0}};

static void notify_state_change(wifi_state_t new_state)
{
    if (g_wifi_state == new_state)
        return;
    g_wifi_state = new_state;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&g_wifi_state, 1);

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE)
        ble_gatts_notify_custom(g_conn_handle, wifi_state_handle, om);

    ESP_LOGI(TAG, "WiFi State changed to: %d", g_wifi_state);
}

static void notify_ssid_change(const char *ssid)
{
    if (ssid == NULL)
        return;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&g_ssid, strlen(g_ssid));

    if (g_conn_handle != BLE_HS_CONN_HANDLE_NONE)
        ble_gatts_notify_custom(g_conn_handle, wifi_ssid_handle, om);
}

static int gatt_svr_chr_access_wifi(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uintptr_t chr_id = (uintptr_t)arg;
    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);

    switch (chr_id)
    {
    case WIFI_CHR_SSID:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
        {
            ble_hs_mbuf_to_flat(ctxt->om, g_ssid, (len < sizeof(g_ssid) ? len : sizeof(g_ssid)), NULL);
            g_ssid[len] = '\0';
            return 0;
        }
        return os_mbuf_append(ctxt->om, g_ssid, strlen(g_ssid));

    case WIFI_CHR_PSK:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
        {
            ble_hs_mbuf_to_flat(ctxt->om, g_psk, (len < sizeof(g_psk) ? len : sizeof(g_psk)), NULL);
            g_psk[len] = '\0';
            return 0;
        }
        return os_mbuf_append(ctxt->om, g_psk, strlen(g_psk));

    case WIFI_CHR_STATE:
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
        {
            uint8_t cmd;
            ble_hs_mbuf_to_flat(ctxt->om, &cmd, 1, NULL);
            if (cmd == WIFI_CONNECTING)
                g_connect_requested = true;
            return 0;
        }
        return os_mbuf_append(ctxt->om, &g_wifi_state, 1);

    default:
        break;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

void wifi_manager_task(void *pvParameters)
{
    uint32_t connect_start_time = 0;

    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_ERROR_CHECK(esp_netif_init());
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Task started.");

    while (1)
    {
        if (g_connect_requested)
        {
            g_connect_requested = false;
            ESP_LOGI(TAG, "Starting WiFi Connection to: %s", g_ssid);

            wifi_config_t wifi_config = {0};
            strncpy((char *)wifi_config.sta.ssid, g_ssid, sizeof(wifi_config.sta.ssid));
            strncpy((char *)wifi_config.sta.password, g_psk, sizeof(wifi_config.sta.password));

            esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
            esp_wifi_connect();

            connect_start_time = xTaskGetTickCount();
            notify_state_change(WIFI_CONNECTING);
        }

        if (g_wifi_state == WIFI_CONNECTING)
        {
            wifi_ap_record_t ap_info;
            if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
            {
                notify_ssid_change(g_ssid);
                notify_state_change(WIFI_CONNECTED);
            }
            else if ((xTaskGetTickCount() - connect_start_time) > pdMS_TO_TICKS(7500))
            {
                notify_state_change(WIFI_ERROR);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}