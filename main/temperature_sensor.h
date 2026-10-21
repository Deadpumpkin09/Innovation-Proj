#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

#include <stdbool.h>

// Initialise DS18B20
void temperature_sensor_init(void);

// Start temperature measurement
bool temperature_sensor_start_conversion(void);

// Read measured temperature
bool temperature_sensor_read_celsius(float *temperature);

#endif
