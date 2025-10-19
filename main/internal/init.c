#include "internal/init.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "internal/scan_bluetooth.h"
#include "internal/wifi.h"
#include "scan_bluetooth.h"
#include "ui.h"

void init() {
  init_wifi();

  init_blue();

  ui_init();
}
