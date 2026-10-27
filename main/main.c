#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "temperature_sensor.h"
#include "peltier_control.h"
#include "segment_display.h"
#include "sd_logger.h"
#include "web_server.h"
#include "battery_monitor.h"
#include "time_manager.h"

#define SENSOR_INTERVAL_MS 2000
#define CONVERSION_DELAY_MS 750
#define CONTROL_INTERVAL_MS 100

static const char *TAG = "MAIN";

typedef struct {
    uint32_t uptime_seconds;

    bool cooling_valid;
    float cooling_temperature;

    bool ambient_valid;
    float ambient_temperature;
} 

temperature_log_t;
static QueueHandle_t logging_queue;
static void sd_logging_task(void *arg) {
    (void)arg;
    temperature_log_t data;
    while (true) {
        if (xQueueReceive(logging_queue, &data, portMAX_DELAY) == pdTRUE && sd_logger_is_ready() && time_manager_is_set()){
            if (!sd_logger_write(data.uptime_seconds, data.cooling_valid, data.cooling_temperature, data.ambient_valid, data.ambient_temperature)){
                ESP_LOGW(TAG, "SD write failed");
            }
        }
    }
}
static void temperature_task(void *arg) {
    (void)arg;
    TickType_t last_sensor = xTaskGetTickCount() - pdMS_TO_TICKS(SENSOR_INTERVAL_MS);
    TickType_t conversion_start = 0;
    bool pending = false, cooling_valid = false, ambient_valid = false;
    float cooling = 0.0f, ambient = 0.0f;
    while (true) {
        TickType_t now = xTaskGetTickCount();
        if (!pending && (now - last_sensor) >= pdMS_TO_TICKS(SENSOR_INTERVAL_MS)){
            last_sensor = now;
            if (temperature_sensor_start_conversion()) {
                pending = true;
                conversion_start = now;
            } else {
                cooling_valid = false;
                peltier_control_update(false, cooling, now);
                segment_display_clear();
                web_server_update_temperatures(false, cooling, ambient_valid, ambient);
                ESP_LOGW(TAG, "DS18B20 conversion failed");
            }
        }
        if (pending && (now - conversion_start) >= pdMS_TO_TICKS(CONVERSION_DELAY_MS)) {
            pending = false;
            cooling_valid = temperature_sensor_read_celsius(&cooling);
            if (cooling_valid) {
                segment_display_show_temperature(cooling);
                ESP_LOGI(TAG, "Cooling: %.2f C", cooling);
            } else {
                segment_display_clear();
                ESP_LOGW(TAG, "DS18B20 read failed");
            }
            ambient_valid = ambient_sensor_read_celsius(&ambient);
            if (ambient_valid){
                ESP_LOGI(TAG, "Ambient: %.2f C", ambient);
            } else {
                ESP_LOGW(TAG, "DHT11 read failed");
            }
            peltier_control_update(cooling_valid, cooling, xTaskGetTickCount());
            web_server_update_temperatures(cooling_valid, cooling, ambient_valid, ambient);
            if (time_manager_is_set()) {
                temperature_log_t data = {
                    .uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000ULL),
                    .cooling_valid = cooling_valid, .cooling_temperature = cooling,
                    .ambient_valid = ambient_valid, .ambient_temperature = ambient
                };
                if (xQueueSend(logging_queue, &data, 0) != pdTRUE)
                    ESP_LOGW(TAG, "Logging queue full");
            }
        }
        peltier_control_tick(xTaskGetTickCount());
        vTaskDelay(pdMS_TO_TICKS(CONTROL_INTERVAL_MS));
    }
}
void app_main(void) {
    ESP_LOGI(TAG, "Starting ESP32 cooling system");
    time_manager_init();
    temperature_sensor_init();
    ambient_sensor_init();
    peltier_control_init();
    peltier_control_update(false, 0.0f, xTaskGetTickCount());
    segment_display_init();

    if (sd_logger_init()){ 
        ESP_LOGI(TAG, "SD card mounted");
    } else{ 
        ESP_LOGW(TAG, "SD card unavailable");
    }

    logging_queue = xQueueCreate(10, sizeof(temperature_log_t));
    if (!logging_queue) { 
        ESP_LOGE(TAG, "Queue creation failed"); 
        return; 
    }
    if (xTaskCreate(sd_logging_task, "sd_logger", 4096, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Logger task creation failed"); 
        return;
    }
    if (xTaskCreate(temperature_task, "temperature", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Sensor task creation failed"); 
        return;
    }
    if (battery_monitor_init() != ESP_OK){
         ESP_LOGE(TAG, "Battery demo failed to start");
    }
    esp_err_t err = web_server_start();
    if (err != ESP_OK){
        ESP_LOGE(TAG, "Web server: %s", esp_err_to_name(err));
    } else{
        ESP_LOGI(TAG, "Website: http://192.168.4.1");
        ESP_LOGI(TAG, "Open dashboard to synchronise date/time for SD logging");
}
}
