#ifndef SD_LOGGER_H
#define SD_LOGGER_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
bool sd_logger_init(void);
bool sd_logger_write(uint32_t uptime_seconds, bool cooling_valid, float cooling_temperature,
                     bool ambient_valid, float ambient_temperature);
bool sd_logger_is_ready(void);
// Send recent CSV history over HTTP from the logger module, under its file mutex.
// The callback receives a line and its byte count, returns false to stop.
typedef bool (*sd_logger_line_cb)(const char *line, size_t length, void *context);
bool sd_logger_read_recent(size_t max_records, sd_logger_line_cb callback, void *context);
void sd_logger_deinit(void);
#endif
