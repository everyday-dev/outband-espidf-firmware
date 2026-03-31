#include "outband.h"

static int _add_service(const struct ble_gatt_svc_def *defs)
{
    int rc = 0;

    rc |= ble_gatts_count_cfg(defs);
    rc |= ble_gatts_add_svcs(defs);

    return rc;
}

int gatt_svr_init(void)
{
    int rc = 0;

    ble_svc_gatt_init();

    rc |= _add_service(wifi_svc_definition);
    rc |= _add_service(thermo_svc_definition);
    rc |= _add_service(ecg_svc_definition);

    return rc;
}