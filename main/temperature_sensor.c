#include "temperature_sensor.h"
#include <stdint.h>
#include <stddef.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include "esp_err.h"

// =====================================
// DS18B20 SETTINGS
// =====================================

#define TEMP_SENSOR GPIO_NUM_18

static portMUX_TYPE onewire_mux =
    portMUX_INITIALIZER_UNLOCKED;

// =====================================
// GPIO INITIALISATION
// =====================================

void temperature_sensor_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = (1ULL << TEMP_SENSOR),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    ESP_ERROR_CHECK(gpio_config(&config));

    gpio_set_level(TEMP_SENSOR, 1);
}

// =====================================
// 1-WIRE LOW LEVEL FUNCTIONS
// =====================================

static inline void onewire_low(void)
{
    gpio_set_level(TEMP_SENSOR, 0);
}

static inline void onewire_release(void)
{
    gpio_set_level(TEMP_SENSOR, 1);
}

static inline int onewire_read_pin(void)
{
    return gpio_get_level(TEMP_SENSOR);
}

// Reset bus and detect sensor
static bool onewire_reset(void)
{
    bool present;

    taskENTER_CRITICAL(&onewire_mux);

    onewire_low();
    esp_rom_delay_us(480);

    onewire_release();
    esp_rom_delay_us(70);

    present = (onewire_read_pin() == 0);

    esp_rom_delay_us(410);

    taskEXIT_CRITICAL(&onewire_mux);

    return present;
}

// Write one bit
static void onewire_write_bit(uint8_t bit)
{
    taskENTER_CRITICAL(&onewire_mux);

    onewire_low();

    if (bit)
    {
        esp_rom_delay_us(6);
        onewire_release();
        esp_rom_delay_us(64);
    }
    else
    {
        esp_rom_delay_us(60);
        onewire_release();
        esp_rom_delay_us(10);
    }

    taskEXIT_CRITICAL(&onewire_mux);
}

// Read one bit
static uint8_t onewire_read_bit(void)
{
    uint8_t bit;

    taskENTER_CRITICAL(&onewire_mux);

    onewire_low();
    esp_rom_delay_us(6);

    onewire_release();
    esp_rom_delay_us(9);

    bit = (uint8_t)onewire_read_pin();

    esp_rom_delay_us(55);

    taskEXIT_CRITICAL(&onewire_mux);

    return bit;
}

// Write one byte
static void onewire_write_byte(uint8_t data)
{
    for (int i = 0; i < 8; i++)
    {
        onewire_write_bit(data & 0x01);
        data >>= 1;
    }
}

// Read one byte
static uint8_t onewire_read_byte(void)
{
    uint8_t data = 0;

    for (int i = 0; i < 8; i++)
    {
        data |= (onewire_read_bit() << i);
    }

    return data;
}

// =====================================
// CRC8 CHECK
// =====================================

static uint8_t onewire_crc8(
    const uint8_t *data,
    uint8_t length)
{
    uint8_t crc = 0;

    while (length--)
    {
        uint8_t inbyte = *data++;

        for (int i = 0; i < 8; i++)
        {
            uint8_t mix = (crc ^ inbyte) & 0x01;

            crc >>= 1;

            if (mix)
            {
                crc ^= 0x8C;
            }

            inbyte >>= 1;
        }
    }

    return crc;
}

// =====================================
// DS18B20 FUNCTIONS
// =====================================

// Start temperature conversion
bool temperature_sensor_start_conversion(void)
{
    if (!onewire_reset())
    {
        return false;
    }

    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0x44); // Convert temperature

    return true;
}

// Read temperature in Celsius
bool temperature_sensor_read_celsius(float *temperature)
{
    if (temperature == NULL)
    {
        return false;
    }

    if (!onewire_reset())
    {
        return false;
    }

    onewire_write_byte(0xCC); // Skip ROM
    onewire_write_byte(0xBE); // Read scratchpad

    uint8_t scratchpad[9];

    for (int i = 0; i < 9; i++)
    {
        scratchpad[i] = onewire_read_byte();
    }

    // Check data integrity
    if (onewire_crc8(scratchpad, 8) != scratchpad[8])
    {
        return false;
    }

    int16_t raw = (int16_t)(
        ((uint16_t)scratchpad[1] << 8) |
        scratchpad[0]
    );

    *temperature = raw / 16.0f;

    if (!isfinite(*temperature))
    {
        return false;
    }

    if (*temperature < -55.0f ||
        *temperature > 125.0f)
    {
        return false;
    }

    return true;
}
