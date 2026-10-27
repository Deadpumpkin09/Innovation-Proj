#include "segment_display.h"

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"

// =====================================
// I2C SETTINGS
// =====================================

#define I2C_SDA GPIO_NUM_21
#define I2C_SCL GPIO_NUM_22

#define DISPLAY_ADDRESS 0x70
#define I2C_FREQUENCY 100000

static const char *TAG = "DISPLAY";

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t display_handle;

static bool display_ready = false;

// =====================================
// HT16K33 COMMANDS
// =====================================

#define CMD_OSCILLATOR_ON 0x21
#define CMD_DISPLAY_ON   0x81
#define CMD_BRIGHTNESS   0xEF

// =====================================
// CHARACTER MAPPING
// =====================================

// Standard HT16K33 alphanumeric segment patterns.
// Exact appearance depends on display wiring.

static uint16_t char_pattern(char character){
    switch (character){
        case '0': return 0x0C3F;
        case '1': return 0x0006;
        case '2': return 0x00DB;
        case '3': return 0x008F;
        case '4': return 0x00E6;
        case '5': return 0x00ED;
        case '6': return 0x00FD;
        case '7': return 0x0007;
        case '8': return 0x00FF;
        case '9': return 0x00EF;

        case 'C': return 0x0039;
        case '-': return 0x00C0;
        case ' ': return 0x0000;

        // Degree symbol approximation
        case 'o': return 0x00C3;

        default: return 0x0000;
    }
}

// =====================================
// I2C WRITE FUNCTION
// =====================================

static esp_err_t display_write(const uint8_t *data, size_t length){
    if (!display_ready){
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit(display_handle, data, length, 1000);
}

// =====================================
// DISPLAY INITIALISATION
// =====================================

esp_err_t segment_display_init(void){
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = I2C_SDA,
        .scl_io_num = I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true};

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_handle);

    if (err != ESP_OK){
        ESP_LOGE(TAG, "I2C bus initialization failed");
        return err;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DISPLAY_ADDRESS,
        .scl_speed_hz = I2C_FREQUENCY};

    err = i2c_master_bus_add_device(bus_handle, &device_config, &display_handle);

    if (err != ESP_OK){
        ESP_LOGE(TAG, "Display device setup failed");
        return err;
    }

    display_ready = true;

    // Turn on internal oscillator
    uint8_t command = CMD_OSCILLATOR_ON;

    err = display_write(&command, 1);
    if (err != ESP_OK) return err;

    // Turn on display
    command = CMD_DISPLAY_ON;

    err = display_write(&command, 1);
    if (err != ESP_OK) return err;

    // Set maximum brightness
    command = CMD_BRIGHTNESS;

    err = display_write(&command, 1);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "14-segment display initialized");

    return segment_display_clear();
}

// =====================================
// WRITE FOUR CHARACTERS
// =====================================

static esp_err_t display_four_characters(
    char c1,
    char c2,
    char c3,
    char c4)
{
    uint16_t patterns[4] = {
        char_pattern(c1),
        char_pattern(c2),
        char_pattern(c3),
        char_pattern(c4)
    };

    // HT16K33 display RAM starts at address 0x00.
    // Each character uses two bytes.

    uint8_t buffer[9];

    buffer[0] = 0x00;

    for (int i = 0; i < 4; i++){
        buffer[1 + i * 2] = patterns[i] & 0xFF;

        buffer[2 + i * 2] = (patterns[i] >> 8) & 0xFF;
    }
    return display_write(buffer, sizeof(buffer));
}

// =====================================
// CLEAR DISPLAY
// =====================================

esp_err_t segment_display_clear(void){
    return display_four_characters(' ', ' ', ' ', ' ');
}

// =====================================
// DISPLAY TEMPERATURE
// =====================================

esp_err_t segment_display_show_temperature(float temperature){
    if (!isfinite(temperature)){
        return display_four_characters('-', '-', '-', '-');
    }

    // Round to nearest whole degree
    int temp = (int)roundf(temperature);

    if (temp >= 0 && temp <= 99){
        char tens = ' ';
        char ones = '0' + (temp % 10);

        if (temp >= 10){
            tens = '0' + (temp / 10);
        }

        return display_four_characters(tens, ones, 'o', 'C');
    }

    // Temperature outside displayable range
    return display_four_characters('-', '-', 'o', 'C');
}
