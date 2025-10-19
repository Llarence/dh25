#include "internal/scan_bluetooth.h"

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "services/gap/ble_svc_gap.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "blue_scan";

#define AD_TYPE_NAME_CMPL 0x09
#define AD_TYPE_NAME_SHORT 0x08
#define AD_TYPE_SVC_UUIDS_16_CMPL 0x03
#define AD_TYPE_SVC_UUIDS_16_INCOMP 0x02

char *addr_str(const uint8_t *addr) {
  static char buf[18];
  sprintf(buf, "%02x:%02x:%02x:%02x:%02x:%02x", addr[5], addr[4], addr[3],
          addr[2], addr[1], addr[0]);
  return buf;
}

static int ble_adv_data_find_field(uint8_t type, const uint8_t *data,
                                   uint8_t length, const uint8_t **out_val,
                                   uint8_t *out_len) {
  const uint8_t *ptr = data;
  uint8_t current_len = length;

  while (current_len > 1) {
    uint8_t field_len = ptr[0];
    uint8_t total_field_size = field_len + 1;

    if (total_field_size > current_len) {
      return -1;
    }

    uint8_t ad_type = ptr[1];

    if (ad_type == type) {
      *out_val = ptr + 2;
      *out_len = field_len - 1;
      return 0;
    }

    ptr += total_field_size;
    current_len -= total_field_size;
  }

  return -2;
}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_DISC: {
    const uint8_t *name = NULL;
    uint8_t name_len = 0;
    const uint8_t *uuids16_val = NULL;
    uint8_t uuids16_len = 0;

    ESP_LOGI(TAG, "Device found: addr_type=%d, addr=%s, RSSI=%d",
             event->disc.addr.type, addr_str(event->disc.addr.val),
             event->disc.rssi);

    if (ble_adv_data_find_field(AD_TYPE_NAME_CMPL, event->disc.data,
                                event->disc.length_data, &name,
                                &name_len) != 0) {
      ble_adv_data_find_field(AD_TYPE_NAME_SHORT, event->disc.data,
                              event->disc.length_data, &name, &name_len);
    }

    if (name_len > 0) {
      ESP_LOGI(TAG, "  Name: %.*s", name_len, (char *)name);
    }

    if (ble_adv_data_find_field(AD_TYPE_SVC_UUIDS_16_CMPL, event->disc.data,
                                event->disc.length_data, &uuids16_val,
                                &uuids16_len) != 0) {
      ble_adv_data_find_field(AD_TYPE_SVC_UUIDS_16_INCOMP, event->disc.data,
                              event->disc.length_data, &uuids16_val,
                              &uuids16_len);
    }

    if (uuids16_len > 0) {
      int num_uuids = uuids16_len / 2;
      ESP_LOGI(TAG, "  Service UUIDs (16-bit): Found %d", num_uuids);
    }
    break;
  }

  case BLE_GAP_EVENT_DISC_COMPLETE:
    ESP_LOGI(TAG, "Scan complete; reason=%d", event->disc_complete.reason);
    break;

  default:
    break;
  }

  return 0;
}

esp_err_t start_ble_scan(int duration_ms) {
  int rc;
  struct ble_gap_disc_params disc_params;

  memset(&disc_params, 0, sizeof(disc_params));
  disc_params.itvl = 0;
  disc_params.window = 0;
  disc_params.filter_duplicates = 1;
  disc_params.passive = 1;
  disc_params.limited = 0;

  uint8_t own_addr_type;
  rc = ble_hs_id_infer_auto(0, &own_addr_type);
  if (rc != 0) {
    ESP_LOGE(TAG, "error determining address type: %d", rc);
    return ESP_FAIL;
  }

  int timeout_ticks = duration_ms / 10;
  if (timeout_ticks <= 0) {
    timeout_ticks = BLE_HS_FOREVER;
  }

  ESP_LOGI(TAG, "Starting BLE scan for %d ms...", duration_ms);

  rc = ble_gap_disc(own_addr_type, timeout_ticks, &disc_params,
                    ble_gap_event_cb, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "Error starting scan: %d", rc);
    return ESP_FAIL;
  }

  return ESP_OK;
}

static void ble_host_task(void *param) {
  ESP_LOGI(TAG, "BLE Host Task Started");

  nimble_port_run();

  nimble_port_freertos_deinit();
}

static void start_ble_scan_on_sync(void) {
  if (ble_hs_synced()) {
    start_ble_scan(10000);
  }
}

void init_blue(void) {
  nimble_port_init();

  ble_svc_gap_device_name_set("Scanner");

  ble_hs_cfg.sync_cb = start_ble_scan_on_sync;

  nimble_port_freertos_init(ble_host_task);
}
