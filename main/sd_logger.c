#include "sd_logger.h"
#include "time_manager.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

#define LOG_PATH "/sdcard/temperature_log_dated.csv"
#define MOUNT_POINT "/sdcard"
#define MAX_LINE 128

static const char *TAG = "SD_LOGGER";
static bool ready = false;
static sdmmc_card_t *card = NULL;
static SemaphoreHandle_t file_mutex = NULL;
static const char *HEADER = "day_time,temp_blood,temp_internal,battery\n";

bool sd_logger_is_ready(void)
{
    return ready;
}

bool sd_logger_init(void)
{
    if (ready)
    {
        return true;
    }
    if (!file_mutex)
    {
        file_mutex = xSemaphoreCreateMutex();
    }
    if (!file_mutex)
    {
        return false;
    }
    spi_bus_config_t bus = {
        .mosi_io_num = GPIO_NUM_23,
        .miso_io_num = GPIO_NUM_19,
        .sclk_io_num = GPIO_NUM_16,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096};

    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SDSPI_DEFAULT_DMA);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SPI bus: %s", esp_err_to_name(err));
        return false;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SPI2_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = GPIO_NUM_17;
    slot.host_id = SPI2_HOST;
    esp_vfs_fat_sdmmc_mount_config_t cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024};
    err = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot, &cfg, &card);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "SD mount failed: %s", esp_err_to_name(err));
        spi_bus_free(SPI2_HOST);
        return false;
    }
    ready = true;
    FILE *f = fopen(LOG_PATH, "r");
    if (f)
    {
        fclose(f);
    }
    else
    {
        f = fopen(LOG_PATH, "w");
        if (!f)
        {
            ESP_LOGE(TAG, "Cannot create CSV");
            sd_logger_deinit();
            return false;
        }
        fputs(HEADER, f);
        fclose(f);
    }
    ESP_LOGI(TAG, "Logging to %s", LOG_PATH);
    return true;
}

bool sd_logger_write(const char *day_time, float cooling_temperature, float ambient_temperature, uint8_t battery_percentage)
{
    if (!ready || !file_mutex || !day_time)
    {
        return false;
    }
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE)
        return false;

    FILE *f = fopen(LOG_PATH, "a");
    bool ok = false;
    if (f)
    {
        int n = fprintf(f, "%s,%.2f,%.2f,%u\n", day_time, cooling_temperature, ambient_temperature, battery_percentage);
        ok = (n > 0 && fclose(f) == 0);
    }
    xSemaphoreGive(file_mutex);
    return ok;
}

bool sd_logger_read_recent(size_t max_records, sd_logger_line_cb cb, void *context)
{
    if (!ready || !cb || !file_mutex)
        return false;
    if (xSemaphoreTake(file_mutex, pdMS_TO_TICKS(3000)) != pdTRUE)
        return false;

    FILE *f = fopen(LOG_PATH, "r");
    if (!f)
    {
        xSemaphoreGive(file_mutex);
        return false;
    }
    char line[MAX_LINE];
    size_t count = 0;
    while (fgets(line, sizeof(line), f))
        ++count;
    rewind(f);
    size_t data_count = count > 0 ? count - 1 : 0;
    size_t skip = data_count > max_records ? data_count - max_records : 0;
    bool ok = cb(HEADER, strlen(HEADER), context);
    if (fgets(line, sizeof(line), f))
    {
        while (skip > 0 && fgets(line, sizeof(line), f))
            --skip;
        while (ok && fgets(line, sizeof(line), f))
            ok = cb(line, strlen(line), context);
    }
    fclose(f);
    xSemaphoreGive(file_mutex);
    return ok;
}

void sd_logger_deinit(void)
{
    if (!ready)
        return;
    ready = false;
    esp_vfs_fat_sdcard_unmount(MOUNT_POINT, card);
    card = NULL;
    spi_bus_free(SPI2_HOST);
}