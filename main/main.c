#include "bsp_board.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lv_port.h"

#include "internal/init.h"

void app_main(void) {
  ESP_ERROR_CHECK(bsp_board_init());
  lv_port_init();

  lv_port_sem_take();
  init();
  lv_port_sem_give();
}
