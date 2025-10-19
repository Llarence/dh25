#include "internal/init.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "internal/scan_bluetooth.h"
#include "internal/wifi.h"
#include "scan_bluetooth.h"
#include "scan_wifi.h"
#include "ui.h"

void init() {
  init_wifi();
  init_wifi_scan_mutex();

  xTaskCreate(ip_scan_task, "ip_scan_task", 4096, NULL, 5, NULL);
  // init_blue();

  ui_init();
}
