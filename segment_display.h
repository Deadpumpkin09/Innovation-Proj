#ifndef SEGMENT_DISPLAY_H
#define SEGMENT_DISPLAY_H

#include "esp_err.h"

// Initialise I2C and 14-segment display
esp_err_t segment_display_init(void);

// Display temperature in Celsius
esp_err_t segment_display_show_temperature(float temperature);

// Clear all display characters
esp_err_t segment_display_clear(void);

#endif
