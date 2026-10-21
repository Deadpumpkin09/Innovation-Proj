
#include <stdio.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "temperature_sensor.h"
#include "peltier_control.h"
#include "segment_display.h"

// =====================================
// SETTINGS
// =====================================

#define TEMP_INTERVAL_MS 2000
#define TEMP_CONVERSION_MS 750
#define CONTROL_INTERVAL_MS 100

static const char *TAG = "MAIN";

// =====================================
// SYSTEM TASK
// =====================================

static void system_task(void *pvParameters)
{
    TickType_t last_temp_tick = xTaskGetTickCount();

    TickType_t conversion_start_tick = 0;

    bool conversion_in_progress = false;

    while (1)
    {
        TickType_t now = xTaskGetTickCount();

        // =================================
        // START TEMPERATURE CONVERSION
        // =================================

        if (!conversion_in_progress &&
            (now - last_temp_tick) >=
                pdMS_TO_TICKS(TEMP_INTERVAL_MS))
        {
            last_temp_tick = now;

            if (temperature_sensor_start_conversion())
            {
                conversion_start_tick = now;
                conversion_in_progress = true;
            }
            else
            {
                ESP_LOGE(TAG,
                    "Temperature sensor not detected");

                peltier_control_update(
                    false,
                    0.0f,
                    now
                );

                // Show error on display
                segment_display_show_temperature(
                    -999.0f
                );
            }
        }

        // =================================
        // READ TEMPERATURE
        // =================================

        if (conversion_in_progress &&
            (now - conversion_start_tick) >=
                pdMS_TO_TICKS(TEMP_CONVERSION_MS))
        {
            conversion_in_progress = false;

            float temperature = 0.0f;

            bool valid =
                temperature_sensor_read_celsius(
                    &temperature
                );

            if (valid)
            {
                ESP_LOGI(TAG,
                    "Temperature: %.2f C",
                    temperature);

                // Update 14-segment display
                esp_err_t display_err =
                    segment_display_show_temperature(
                        temperature
                    );

                if (display_err != ESP_OK)
                {
                    ESP_LOGW(TAG,
                        "Display update failed");
                }
            }
            else
            {
                ESP_LOGE(TAG,
                    "Temperature reading failed");

                // Display error indication
                segment_display_show_temperature(
                    -999.0f
                );
            }

            // Update Peltier control
            peltier_control_update(
                valid,
                temperature,
                xTaskGetTickCount()
            );
        }

        // =================================
        // UPDATE PELTIER/FAN TIMERS
        // =================================

        peltier_control_tick(xTaskGetTickCount());

        vTaskDelay(
            pdMS_TO_TICKS(CONTROL_INTERVAL_MS)
        );
    }
}

// =====================================
// MAIN
// =====================================

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP32 system");

    // Initialise temperature sensor
    temperature_sensor_init();

    // Initialise Peltier and fans
    peltier_control_init();

    // Initialise 14-segment display
    esp_err_t display_err = segment_display_init();

    if (display_err != ESP_OK)
    {
        ESP_LOGE(TAG,
            "14-segment display initialization failed");
    }

    // Create FreeRTOS system task
    BaseType_t result = xTaskCreate(
        system_task,
        "system_task",
        4096,
        NULL,
        4,
        NULL
    );

    if (result != pdPASS)
    {
        ESP_LOGE(TAG,
            "Failed to create system task");
    }
}