#include "peltier_control.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_err.h"


// IBT-2 Peltier driver
#define PELTIER_RPWM GPIO_NUM_25
#define PELTIER_LPWM GPIO_NUM_26
#define PELTIER_REN  GPIO_NUM_27
#define PELTIER_LEN  GPIO_NUM_14

//fans
#define FAN1 GPIO_NUM_32
#define FAN2 GPIO_NUM_33

#define TEMP_ON_THRESHOLD  27.0f
#define TEMP_OFF_THRESHOLD 26.5f

#define FAN_STARTUP_MS  1000
#define FAN_COOLDOWN_MS 5000

static const char *TAG = "PELTIER";

typedef enum{
    SYSTEM_OFF,
    SYSTEM_FAN_STARTUP,
    SYSTEM_COOLING,
    SYSTEM_FAN_COOLDOWN,
    SYSTEM_SENSOR_ERROR} 
    
    peltier_state_t;

static peltier_state_t system_state = SYSTEM_OFF;

static TickType_t state_start_tick = 0;

// =====================================
// FAN FUNCTIONS
// =====================================

static void fans_on(void){
    gpio_set_level(FAN1, 1);
    gpio_set_level(FAN2, 1);
}

static void fans_off(void){
    gpio_set_level(FAN1, 0);
    gpio_set_level(FAN2, 0);
}

static void peltier_on(void){
    // Forward direction only
    gpio_set_level(PELTIER_LPWM, 0);

    // Enable IBT-2 driver
    gpio_set_level(PELTIER_REN, 1);
    gpio_set_level(PELTIER_LEN, 1);

    // Full power
    gpio_set_level(PELTIER_RPWM, 1);
}

static void peltier_off(void){
    // Disable drive
    gpio_set_level(PELTIER_RPWM, 0);
    gpio_set_level(PELTIER_LPWM, 0);

    // Disable driver
    gpio_set_level(PELTIER_REN, 0);
    gpio_set_level(PELTIER_LEN, 0);
}

// =====================================
// INITIALISATION
// =====================================

void peltier_control_init(void){
    gpio_config_t config = {
        .pin_bit_mask =
            (1ULL << PELTIER_RPWM) |
            (1ULL << PELTIER_LPWM) |
            (1ULL << PELTIER_REN) |
            (1ULL << PELTIER_LEN) |
            (1ULL << FAN1) |
            (1ULL << FAN2),

        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    peltier_off();
    fans_off();

    system_state = SYSTEM_OFF;
}

// =====================================
// TEMPERATURE CONTROL
// =====================================

void peltier_control_update(bool sensor_valid, float temperature, TickType_t now){
    // Sensor failure
    if (!sensor_valid){
        peltier_off();
        fans_on();

        if (system_state != SYSTEM_SENSOR_ERROR){
            ESP_LOGE(TAG, "Sensor failure - Peltier OFF, fans ON");
        }
        system_state = SYSTEM_SENSOR_ERROR;
        return;
    }

    // Sensor recovery
    if (system_state == SYSTEM_SENSOR_ERROR){
        peltier_off();
        fans_on();
        state_start_tick = now;
        system_state = SYSTEM_FAN_COOLDOWN;
        ESP_LOGI(TAG, "Sensor recovered");
    }

    switch (system_state){
        case SYSTEM_OFF:
            if (temperature > TEMP_ON_THRESHOLD){
                fans_on();
                state_start_tick = now;
                system_state = SYSTEM_FAN_STARTUP;
                ESP_LOGI(TAG,"Temperature above 27C - Fans ON");
            }
            break;

        case SYSTEM_FAN_STARTUP:
            if (temperature <= TEMP_OFF_THRESHOLD){
                peltier_off();
                state_start_tick = now;
                system_state = SYSTEM_FAN_COOLDOWN;

                ESP_LOGI(TAG, "Cooling cancelled");
            }

            break;

        case SYSTEM_COOLING:

            if (temperature <= TEMP_OFF_THRESHOLD){
                peltier_off();

                state_start_tick = now;
                system_state = SYSTEM_FAN_COOLDOWN;

                ESP_LOGI(TAG, "Peltier OFF - Fan cooldown started");
            }

            break;

        case SYSTEM_FAN_COOLDOWN:

            if (temperature > TEMP_ON_THRESHOLD)
            {
                fans_on();

                state_start_tick = now;
                system_state = SYSTEM_FAN_STARTUP;

                ESP_LOGI(TAG,"Cooling restarting");
            }

            break;

        default:
            break;
    }
}

// =====================================
// TIMER CONTROL
// =====================================

void peltier_control_tick(TickType_t now){
    // Wait 1 second after fans turn ON
    if (system_state == SYSTEM_FAN_STARTUP){
        if ((now - state_start_tick) >= pdMS_TO_TICKS(FAN_STARTUP_MS)){
            peltier_on();
            system_state = SYSTEM_COOLING;
            ESP_LOGI(TAG, "Peltier ON");
        }
    }

    // Keep fans running 5 seconds after Peltier OFF
    else if (system_state == SYSTEM_FAN_COOLDOWN){
            if ((now - state_start_tick) >= pdMS_TO_TICKS(FAN_COOLDOWN_MS)){
            fans_off();
            system_state = SYSTEM_OFF;
            ESP_LOGI(TAG, "Fans OFF");
        }
    }
}
