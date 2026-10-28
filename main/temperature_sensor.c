#include "temperature_sensor.h"

#include <stdint.h>
#include <stddef.h>

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_err.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "dht.h"

// =====================================
// SENSOR PIN CONFIGURATIONS
// =====================================

#define DS18B20_PIN GPIO_NUM_18
#define DHT11_PIN   GPIO_NUM_4

static const char *TAG = "TEMPERATURE";

// Protect timing-sensitive 1-Wire operations
static portMUX_TYPE onewire_mux =
    portMUX_INITIALIZER_UNLOCKED;


// =====================================
// DS18B20 - 1-WIRE COMMUNICATION
// =====================================

// Pull the 1-Wire data line LOW
static void onewire_low(void){
    gpio_set_level(DS18B20_PIN, 0);
}

// Release the data line HIGH
static void onewire_release(void){
    gpio_set_level(DS18B20_PIN, 1);
}

// Reset the 1-Wire bus and detect sensor
static bool onewire_reset(void){
    bool present;
    taskENTER_CRITICAL(&onewire_mux);

    onewire_low();
    esp_rom_delay_us(480);

    onewire_release();
    esp_rom_delay_us(70);

    present = (gpio_get_level(DS18B20_PIN) == 0);

    esp_rom_delay_us(410);

    taskEXIT_CRITICAL(&onewire_mux);

    return present;
}

// Write one bit to DS18B20
static void onewire_write_bit(uint8_t bit){
    taskENTER_CRITICAL(&onewire_mux);

    onewire_low();

    if (bit){
        esp_rom_delay_us(6);
        onewire_release();
        esp_rom_delay_us(64);
    }
    else{
        esp_rom_delay_us(60);
        onewire_release();
        esp_rom_delay_us(10);
    }

    taskEXIT_CRITICAL(&onewire_mux);
}

// Read one bit from DS18B20
static uint8_t onewire_read_bit(void){
    uint8_t bit;

    taskENTER_CRITICAL(&onewire_mux);

    onewire_low();
    esp_rom_delay_us(3);

    onewire_release();
    esp_rom_delay_us(10);

    bit = gpio_get_level(DS18B20_PIN);

    esp_rom_delay_us(53);

    taskEXIT_CRITICAL(&onewire_mux);

    return bit;
}

// Write one byte, least significant bit first
static void onewire_write_byte(uint8_t data){
    for (int i = 0; i < 8; i++){
        onewire_write_bit(data & 0x01);
        data >>= 1;
    }
}

// Read one byte, least significant bit first
static uint8_t onewire_read_byte(void){
    uint8_t data = 0;

    for (int i = 0; i < 8; i++){
        data |= (onewire_read_bit() << i);
    }

    return data;
}

// =====================================
// DS18B20 CRC8 VERIFICATION
// =====================================

static uint8_t onewire_crc8(const uint8_t *data, uint8_t length){
    uint8_t crc = 0;

    for (uint8_t i = 0; i < length; i++){
        uint8_t value = data[i];

        for (uint8_t j = 0; j < 8; j++){
            uint8_t mix = (crc ^ value) & 0x01;
            crc >>= 1;
            if (mix){
                crc ^= 0x8C;
            }
            value >>= 1;
        }
    }

    return crc;
}

// =====================================
// INITIALISE TEMPERATURE SENSOR
// =====================================

void temperature_sensor_init(void){
    gpio_config_t io_config = {
        .pin_bit_mask = (1ULL << DS18B20_PIN),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};

    ESP_ERROR_CHECK(gpio_config(&io_config));

    onewire_release();

    ESP_LOGI(TAG, "DS18B20 initialised on GPIO18");
}

// =====================================
// START DS18B20 CONVERSION
// =====================================

bool temperature_sensor_start_conversion(void){
    if (!onewire_reset()){
        ESP_LOGW(TAG, "DS18B20 not detected");
        return false;
    }

    // Skip ROM (single sensor)
    onewire_write_byte(0xCC);

    // Convert temperature
    onewire_write_byte(0x44);

    return true;
}

// =====================================
// READ DS18B20 TEMPERATURE
// =====================================

bool temperature_sensor_read_celsius(float *temperature){
    if (temperature == NULL){
        return false;
    }

    if (!onewire_reset()){
        ESP_LOGW(TAG, "DS18B20 not responding");
        return false;
    }

    // Skip ROM
    onewire_write_byte(0xCC);

    // Read scratchpad
    onewire_write_byte(0xBE);

    uint8_t scratchpad[9];

    for (int i = 0; i < 9; i++){
        scratchpad[i] = onewire_read_byte();
    }

    // Verify received data
    uint8_t calculated_crc = onewire_crc8(scratchpad, 8);

    if (calculated_crc != scratchpad[8]){
        ESP_LOGW(TAG, "DS18B20 CRC error");
        return false;
    }

    // Combine temperature bytes
    int16_t raw_temperature =(int16_t)(((uint16_t)scratchpad[1] << 8) |scratchpad[0]);

    float celsius = raw_temperature / 16.0f;

    // Validate DS18B20 operating range
    if (celsius < -55.0f || celsius > 125.0f){
        ESP_LOGW(TAG, "Invalid DS18B20 temperature");
        return false;
    }
    *temperature = celsius;
    return true;
}


// =====================================
// DHT11 - AMBIENT TEMPERATURE SENSOR
// =====================================

void ambient_sensor_init(void){
    ESP_LOGI(TAG, "DHT11 ambient sensor on GPIO4");
}

// =====================================
// READ DHT11 AMBIENT TEMPERATURE
// =====================================

bool ambient_sensor_read_celsius(float *temperature){
    if (temperature == NULL){
        return false;
    }

    float humidity = 0.0f;
    float ambient_temperature = 0.0f;

    esp_err_t result = dht_read_float_data(DHT_TYPE_DHT11, DHT11_PIN, &humidity, &ambient_temperature);
   
    if (result != ESP_OK){
        ESP_LOGW(TAG, "DHT11 reading failed: %s", esp_err_to_name(result));
        return false;
    }
    *temperature = ambient_temperature;
    return true;
}

