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

#define MAX_BLUE_HITS 256
#define MAX_BLUE_HIT_SIZE 256

#define PROMPT "SYSTEM: Here is a scan of all the named bluetooth things:"

char *blue_hits[MAX_BLUE_HITS];
int blue_hits_end = 0;
int blue_hits_start = 0;
bool blue_hits_empty = true;

volatile int searched = 0;

static SemaphoreHandle_t blue_hits_mutex = NULL;

void init_blue_scan_mutex(void) {
  blue_hits_mutex = xSemaphoreCreateMutex();
  if (blue_hits_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create blue_hits_mutex");
  }
}

void push_blue_hit(char *hit) {
  if (xSemaphoreTake(blue_hits_mutex, portMAX_DELAY) == pdTRUE) {
    hit = strdup(hit);

    int next_end = (blue_hits_end + 1) % MAX_BLUE_HITS;

    if (next_end == blue_hits_start) {
      ESP_LOGW(TAG, "Buffer full! Overwriting oldest message at index %d.",
               blue_hits_start);
      blue_hits_start = (blue_hits_start + 1) % MAX_BLUE_HITS;

      free(blue_hits[next_end]);
    }

    blue_hits[next_end] = hit;

    blue_hits_end = next_end;
    blue_hits_empty = false;

    xSemaphoreGive(blue_hits_mutex);
  } else {
    ESP_LOGE(TAG, "Failed to take mutex in push_blue_hit");
  }
}

char *blue_scan_to_prompt(void) {
  if (xSemaphoreTake(blue_hits_mutex, portMAX_DELAY) == pdTRUE) {
    uint32_t count = strlen(PROMPT);

    if (!blue_hits_empty) {
      for (int i = blue_hits_start; i = (i + 1) % MAX_BLUE_HITS;) {
        count += strlen(blue_hits[i]) + 1;

        if (i == blue_hits_end) {
          break;
        }
      }
    }

    char *str = malloc(count);
    strcpy(str, PROMPT);
    int index = strlen(PROMPT);
    str[index] = '\n';
    index++;
    if (!blue_hits_empty) {
      for (int i = blue_hits_start; i = (i + 1) % MAX_BLUE_HITS;) {
        int len = strlen(blue_hits[i]);
        strcpy(str + index, blue_hits[i]);
        index += len;
        str[index] = '\n';
        index++;

        if (i == blue_hits_end) {
          break;
        }
      }
    }

    str[index - 1] = '\0';

    xSemaphoreGive(blue_hits_mutex);

    return str;
  } else {
    ESP_LOGE(TAG, "Failed to take mutex in to_prompt");
    return "";
  }
}

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

static void format_uuids16(const uint8_t *data, uint8_t len, char *out_buf,
                           size_t max_len) {
  size_t current_len = 0;
  out_buf[0] = '\0';

  for (uint8_t i = 0; i < len; i += 2) {
    if (i + 2 > len)
      break;

    uint16_t uuid = data[i] | (data[i + 1] << 8);

    size_t needed = (i > 0 ? 1 : 0) + 4 + 1;

    if (current_len + needed > max_len) {
      break;
    }

    if (i > 0) {
      current_len +=
          snprintf(out_buf + current_len, max_len - current_len, ",");
    }

    current_len +=
        snprintf(out_buf + current_len, max_len - current_len, "%04X", uuid);
  }
}

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg) {
  char *buffer[MAX_BLUE_HIT_SIZE];

  switch (event->type) {
  case BLE_GAP_EVENT_DISC: {
    const uint8_t *name = NULL;
    uint8_t name_len = 0;
    const uint8_t *uuids16_val = NULL;
    uint8_t uuids16_len = 0;
    char uuids_str[64] = "";

    if (ble_adv_data_find_field(AD_TYPE_NAME_CMPL, event->disc.data,
                                event->disc.length_data, &name,
                                &name_len) != 0) {
      ble_adv_data_find_field(AD_TYPE_NAME_SHORT, event->disc.data,
                              event->disc.length_data, &name, &name_len);
    }

    if (ble_adv_data_find_field(AD_TYPE_SVC_UUIDS_16_CMPL, event->disc.data,
                                event->disc.length_data, &uuids16_val,
                                &uuids16_len) != 0) {
      ble_adv_data_find_field(AD_TYPE_SVC_UUIDS_16_INCOMP, event->disc.data,
                              event->disc.length_data, &uuids16_val,
                              &uuids16_len);
    }

    if (uuids16_len > 0) {
      format_uuids16(uuids16_val, uuids16_len, uuids_str, sizeof(uuids_str));
    }

    if (name_len > 0 || uuids16_len > 0) {
      if (uuids16_len > 0) {
        if (name_len > 0) {
          snprintf(buffer, MAX_BLUE_HIT_SIZE,
                   "Device: %s (Type: %d), RSSI: %d, Name: %.*s, UUIDs: %s",
                   addr_str(event->disc.addr.val), event->disc.addr.type,
                   event->disc.rssi, name_len, (char *)name, uuids_str);
        } else {
          snprintf(buffer, MAX_BLUE_HIT_SIZE,
                   "Device: %s (Type: %d), RSSI: %d, UUIDs: %s",
                   addr_str(event->disc.addr.val), event->disc.addr.type,
                   event->disc.rssi, uuids_str);
        }
      } else {
        snprintf(buffer, MAX_BLUE_HIT_SIZE,
                 "Device: %s (Type: %d), RSSI: %d, Name: %.*s",
                 addr_str(event->disc.addr.val), event->disc.addr.type,
                 event->disc.rssi, name_len, (char *)name);
      }

      ESP_LOGI(TAG, "%s", buffer);
      push_blue_hit(buffer);
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
  init_blue_scan_mutex();

  nimble_port_init();

  ble_svc_gap_device_name_set("Scanner");

  ble_hs_cfg.sync_cb = start_ble_scan_on_sync;

  nimble_port_freertos_init(ble_host_task);
}
