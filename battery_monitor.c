#include "battery_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"

#define BATTERY_START 100
#define BATTERY_MIN 20
#define BATTERY_STEP_MS 10000
static portMUX_TYPE battery_lock = portMUX_INITIALIZER_UNLOCKED;
static uint8_t level = BATTERY_START;
static bool charging = false;
static bool started = false;

static void battery_task(void *arg) {
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(BATTERY_STEP_MS));
        portENTER_CRITICAL(&battery_lock);
        if (!charging) {
            if (level > BATTERY_MIN) --level;
            if (level <= BATTERY_MIN) charging = true;
        } else {
            if (level < 100) ++level;
            if (level >= 100) charging = false;
        }
        portEXIT_CRITICAL(&battery_lock);
    }
}
esp_err_t battery_monitor_init(void) {
    if (started) return ESP_OK;
    if (xTaskCreate(battery_task, "battery_demo", 2048, NULL, 2, NULL) != pdPASS)
        return ESP_ERR_NO_MEM;
    started = true;
    return ESP_OK;
}
uint8_t battery_monitor_get_percentage(void) {
    portENTER_CRITICAL(&battery_lock);
    uint8_t value = level;
    portEXIT_CRITICAL(&battery_lock);
    return value;
}
