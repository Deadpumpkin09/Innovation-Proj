#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

bool sd_logger_init(void);
bool sd_logger_write(const char *day_time, float cooling_temperature, float ambient_temperature, uint8_t battery_percentage);
bool sd_logger_is_ready(void);

typedef bool (*sd_logger_line_cb)(const char *line, size_t length, void *context);
bool sd_logger_read_recent(size_t max_records, sd_logger_line_cb callback, void *context);

void sd_logger_deinit(void);

#endif