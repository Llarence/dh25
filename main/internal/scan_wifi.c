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
#include <string.h>

static const char *TAG = "wifi_scan";

#define WEB_SERVER_PORT 80
#define SCAN_TIMEOUT_MS 200
#define MAX_HOST_ID 255

static esp_netif_ip_info_t ip_info;

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
    ESP_LOGW(TAG, "Ensure the device has successfully connected and received "
                  "an IP address (e.g., listen for IP_EVENT_STA_GOT_IP)");
  }
}

void ip_scan_task(void *_) {
  get_ip_info();

  ESP_LOGI(TAG, "Starting network scan...");

  uint32_t net_id = ip_info.ip.addr & ip_info.netmask.addr;

  for (int i = 1; i <= MAX_HOST_ID; i++) {
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
    server_addr.sin_port = htons(WEB_SERVER_PORT);
    server_addr.sin_addr.s_addr = target_addr.s_addr;

    int err =
        connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr));

    ESP_LOGI(TAG, "Scanning: %s (Port %d)", target_ip_str, WEB_SERVER_PORT);
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
          ESP_LOGI(TAG, "Host: %s (Port %d)", target_ip_str, WEB_SERVER_PORT);
        }
      }
    } else if (err == 0) {
      ESP_LOGI(TAG, "Host: %s (Port %d)", target_ip_str, WEB_SERVER_PORT);
    }

    // 5. Close the socket
    close(sock);
  }

  ESP_LOGI(TAG, "Network scan finished.");
  vTaskDelete(NULL);
}
