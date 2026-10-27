#ifndef PELTIER_CONTROL_H
#define PELTIER_CONTROL_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Initialise Peltier and fan GPIOs
void peltier_control_init(void);

// Update cooling based on temperature
void peltier_control_update(bool sensor_valid, float temperature, TickType_t now);

// Update fan startup and cooldown timers
void peltier_control_tick(TickType_t now);

#endif
