#include "time_manager.h"
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
static portMUX_TYPE time_lock = portMUX_INITIALIZER_UNLOCKED;
static bool synced = false;
void time_manager_init(void) {
    // Melbourne: AEST UTC+10, AEDT UTC+11 (Australian DST rules).
    setenv("TZ", "AEST-10AEDT,M10.1.0,M4.1.0/3", 1);
    tzset();
}
bool time_manager_set_epoch_ms(int64_t epoch_ms) {
    // Reject obviously incorrect timestamps (before 2024).
    if (epoch_ms < INT64_C(1704067200000)) return false;
    struct timeval tv = {
        .tv_sec = (time_t)(epoch_ms / 1000),
        .tv_usec = (suseconds_t)((epoch_ms % 1000) * 1000)
    };
    if (settimeofday(&tv, NULL) != 0) return false;
    portENTER_CRITICAL(&time_lock);
    synced = true;
    portEXIT_CRITICAL(&time_lock);
    return true;
}
bool time_manager_is_set(void) {
    portENTER_CRITICAL(&time_lock);
    bool result = synced;
    portEXIT_CRITICAL(&time_lock);
    return result;
}
bool time_manager_get_local(char date[11], char clock_time[9]) {
    if (!time_manager_is_set()) return false;
    time_t now = time(NULL);
    struct tm local;
    if (localtime_r(&now, &local) == NULL) return false;
    if (strftime(date, 11, "%Y-%m-%d", &local) == 0) return false;
    if (strftime(clock_time, 9, "%H:%M:%S", &local) == 0) return false;
    return true;
}
