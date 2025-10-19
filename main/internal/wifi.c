#include "internal/wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "wifi_scan_connect";

static EventGroupHandle_t s_wifi_event_group;
const int CONNECTED_BIT = BIT0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    ESP_LOGI(TAG, "Scan done. APs found: %d", ap_count);
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "STA start");
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "Disconnected, trying to reconnect...");
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
  }
}

void init_wifi(char *name, char *password) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  s_wifi_event_group = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

  wifi_scan_config_t scanConf = {
      .ssid = 0, .bssid = 0, .channel = 0, .show_hidden = true};

  ESP_LOGI(TAG, "Starting WiFi scan...");
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_ERROR_CHECK(esp_wifi_scan_start(&scanConf, true));

  uint16_t ap_num = 0;
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_num));
  wifi_ap_record_t *ap_records =
      (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_num);
  ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));

  bool found = false;
  for (int i = 0; i < ap_num; ++i) {
    ESP_LOGI(TAG, "Found SSID: %s (RSSI: %d)", ap_records[i].ssid,
             ap_records[i].rssi);
    if (strcmp((const char *)ap_records[i].ssid, name) == 0) {
      found = true;
      break;
    }
  }

  free(ap_records);

  if (!found) {
    ESP_LOGW(TAG, "Target SSID '%s' not found. Aborting.", name);
    esp_restart();
    return;
  }

  ESP_LOGI(TAG, "Target found, connecting...");
  wifi_config_t wifi_config = {0};
  strncpy((char *)wifi_config.sta.ssid, name, sizeof(wifi_config.sta.ssid));
  strncpy((char *)wifi_config.sta.password, password,
          sizeof(wifi_config.sta.password));
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_wifi_disconnect());
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_connect());

  EventBits_t bits =
      xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT, pdFALSE, pdFALSE,
                          pdMS_TO_TICKS(20000));
  if (bits & CONNECTED_BIT) {
    ESP_LOGI(TAG, "Connected to %s", name);
  } else {
    esp_restart();
    ESP_LOGW(TAG, "Failed to connect within timeout");
  }
}
