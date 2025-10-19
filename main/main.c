#include "bsp_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "internal/scan_wifi.h"
#include "lv_port.h"
#include "screens/ui_Chat.h"

void updater(lv_timer_t *timer) {
  lv_label_set_text_fmt(ui_Count, "Scanned: %d", wifis_seen);
}

void app_main(void) {
  ESP_ERROR_CHECK(bsp_board_init());
  lv_port_init();

  lv_port_sem_take();
  ui_init();
  lv_port_sem_give();

  lv_timer_create(updater, 10, NULL);
}
