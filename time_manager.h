#ifndef TIME_MANAGER_H
#define TIME_MANAGER_H
#include <stdbool.h>
#include <stdint.h>
void time_manager_init(void);
bool time_manager_set_epoch_ms(int64_t epoch_ms);
bool time_manager_get_local(char date[11], char clock_time[9]);
bool time_manager_is_set(void);
#endif
