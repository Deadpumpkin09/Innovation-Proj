#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H
#include <stdint.h>
#include "esp_err.h"
esp_err_t battery_monitor_init(void);
uint8_t battery_monitor_get_percentage(void);
#endif
