#include "internal/init.h"

#include "internal/wifi.h"
#include "lv_port.h"
#include "ui.h"

void init() {
  init_wifi();

  ui_init();
}
