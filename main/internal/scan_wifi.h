#ifndef SCAN_WIFI_H
#define SCAN_WIFI_H

void ip_scan_task(void *_);
void init_wifi_scan_mutex(void);
char *wifi_scan_to_prompt(void);

#endif
