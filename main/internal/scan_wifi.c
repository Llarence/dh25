#include "internal/scan_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "wifi_scan";

#define SCAN_TIMEOUT_MS 200
#define MAX_HOST_ID 255
#define MAX_WIFI_HITS 24
#define MAX_HIT_SIZE 256
#define PROMPT "SYSTEM: Here is a scan of everything in the local network:"

static esp_netif_ip_info_t ip_info;
static uint16_t ports[3] = {20, 22, 80};

char *wifi_hits[MAX_WIFI_HITS];
int wifi_hits_end = 0;
int wifi_hits_start = 0;
bool wifi_hits_empty = true;

static SemaphoreHandle_t wifi_hits_mutex = NULL;

void init_wifi_scan_mutex(void) {
  wifi_hits_mutex = xSemaphoreCreateMutex();
  if (wifi_hits_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create wifi_hits_mutex");
  }
}

void push_wifi_hit(char *hit) {
  if (xSemaphoreTake(wifi_hits_mutex, portMAX_DELAY) == pdTRUE) {
    hit = strdup(hit);

    int next_end = (wifi_hits_end + 1) % MAX_WIFI_HITS;

    if (next_end == wifi_hits_start) {
      ESP_LOGW(TAG, "Buffer full! Overwriting oldest message at index %d.",
               wifi_hits_start);
      wifi_hits_start = (wifi_hits_start + 1) % MAX_WIFI_HITS;

      free(wifi_hits[next_end]);
    }

    wifi_hits[next_end] = hit;

    wifi_hits_end = next_end;
    wifi_hits_empty = false;

    xSemaphoreGive(wifi_hits_mutex);
  } else {
    ESP_LOGE(TAG, "Failed to take mutex in push_wifi_hit");
  }
}

char *wifi_scan_to_prompt(void) {
  if (xSemaphoreTake(wifi_hits_mutex, portMAX_DELAY) == pdTRUE) {
    uint32_t count = strlen(PROMPT);

    if (!wifi_hits_empty) {
      for (int i = wifi_hits_start; i = (i + 1) % MAX_WIFI_HITS;) {
        count += strlen(wifi_hits[i]) + 1;

        if (i == wifi_hits_end) {
          break;
        }
      }
    }

    char *str = malloc(count);
    strcpy(str, PROMPT);
    int index = strlen(PROMPT);
    str[index] = '\n';
    index++;
    if (!wifi_hits_empty) {
      for (int i = wifi_hits_start; i = (i + 1) % MAX_WIFI_HITS;) {
        int len = strlen(wifi_hits[i]);
        strcpy(str + index, wifi_hits[i]);
        index += len;
        str[index] = '\n';
        index++;

        if (i == wifi_hits_end) {
          break;
        }
      }
    }

    str[index - 1] = '\0';

    xSemaphoreGive(wifi_hits_mutex);

    return str;
  } else {
    ESP_LOGE(TAG, "Failed to take mutex in to_prompt");
    return "";
  }
}

void get_ip_info(void) {
  esp_netif_t *netif = esp_netif_get_default_netif();

  if (netif == NULL) {
    ESP_LOGE(TAG, "No default network interface found!");
    return;
  }

  esp_err_t err = esp_netif_get_ip_info(netif, &ip_info);

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&ip_info.netmask));
    ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&ip_info.gw));
  } else {
    ESP_LOGE(TAG, "Failed to get IP info: %s", esp_err_to_name(err));
  }
}

void ip_scan_task(void *_) {
  get_ip_info();

  ESP_LOGI(TAG, "Starting network scan...");

  uint32_t net_id = ip_info.ip.addr & ip_info.netmask.addr;
  char *buffer[MAX_HIT_SIZE];

  for (int i = 1; i <= MAX_HOST_ID; i++) {
    for (int j = 0; j < sizeof(ports) / sizeof(ports[0]); j++) {
      uint16_t port = ports[j];

      struct in_addr target_addr;
      target_addr.s_addr = net_id | htonl(i);

      char target_ip_str[16];
      inet_ntoa_r(target_addr, target_ip_str, sizeof(target_ip_str));

      int sock = -1;

      sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
      if (sock < 0) {
        ESP_LOGE(TAG, "Error creating socket: errno %d", errno);
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }

      int flags = fcntl(sock, F_GETFL, 0);
      if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        ESP_LOGE(TAG, "Failed to set non-blocking mode: errno %d", errno);
        close(sock);
        continue;
      }

      struct sockaddr_in server_addr;
      memset(&server_addr, 0, sizeof(server_addr));
      server_addr.sin_family = AF_INET;
      server_addr.sin_port = htons(port);
      server_addr.sin_addr.s_addr = target_addr.s_addr;

      int err =
          connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

      // ESP_LOGI(TAG, "TESTING: Host: %s (Port %d)", target_ip_str, port);
      if (err == -1 && errno == EINPROGRESS) {
        fd_set write_set;
        FD_ZERO(&write_set);
        FD_SET(sock, &write_set);

        struct timeval tv;
        tv.tv_sec = SCAN_TIMEOUT_MS / 1000;
        tv.tv_usec = (SCAN_TIMEOUT_MS % 1000) * 1000;

        int res = select(sock + 1, NULL, &write_set, NULL, &tv);

        if (res > 0 && FD_ISSET(sock, &write_set)) {
          int so_error = 0;
          socklen_t len = sizeof(so_error);
          getsockopt(sock, SOL_SOCKET, SO_ERROR, &so_error, &len);

          if (so_error == 0) {
            snprintf(buffer, MAX_HIT_SIZE, "Host: %s (Port %d)", target_ip_str,
                     port);
            push_wifi_hit(buffer);
            ESP_LOGI(TAG, "Host: %s (Port %d)", target_ip_str, port);
          }
        }
      } else if (err == 0) {
        snprintf(buffer, MAX_HIT_SIZE, "Host: %s (Port %d)", target_ip_str,
                 port);
        push_wifi_hit(buffer);
        ESP_LOGI(TAG, "Host: %s (Port %d)", target_ip_str, port);
      }

      close(sock);
    }
  }

  ESP_LOGI(TAG, "Network scan finished.");
  vTaskDelete(NULL);
}
