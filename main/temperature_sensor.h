#ifndef TEMPERATURE_SENSOR_H
#define TEMPERATURE_SENSOR_H

#include <stdbool.h>



void temperature_sensor_init(void);  //Initialise sensors

bool temperature_sensor_start_conversion(void);        // Peltier temp sensor 
bool temperature_sensor_read_celsius(float *temperature);

void ambient_sensor_init(void);          //Ambient temp sensor
bool ambient_sensor_read_celsius(float *temperature);

#endif
