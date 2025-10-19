#include "internal/init.h"

#include "internal/wifi.h"
#include "ui.h"

void init() {
  init_wifi();

  ui_init();
}
