#include "internal/init.h"

#include "esp_log.h"
#include "internal/gemini.h"
#include "internal/wifi.h"
#include "lv_port.h"
#include "ui.h"

void init() {
  init_wifi();

  char *res = call_gemini();
  if (res != NULL) {
    ESP_LOGI("test", "%s", res);
    free(res);
  }

  ui_init();
}
