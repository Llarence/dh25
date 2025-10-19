#include "view/view.h"

#include "esp_event.h"
#include "lv_port.h"
#include "ui.h"

void run_on_event(void *handler_arg, esp_event_base_t base, int32_t id,
                  void *event_data) {
  lv_port_sem_take();
  lv_port_sem_give();
}

void view_init() { ui_init(); }
