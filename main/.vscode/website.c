#include "website.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sd_logger.h"
#include "battery_monitor.h"
#include "time_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "WEBSITE";
#define WIFI_SSID "EEE40011-PROJECT"
#define WIFI_PASSWORD "123456789"

static portMUX_TYPE sensor_lock = portMUX_INITIALIZER_UNLOCKED;
static float cooling_temp = 0.0f, ambient_temp = 0.0f;
static bool cooling_ok = false, ambient_ok = false;
static char reading_time[20] = "";

extern const char page1_html_start[] asm("_binary_page1_html_start");
extern const char page1_html_end[] asm("_binary_page1_html_end");

void web_server_update_temperatures(bool cooling_valid, float cooling_temperature,
                                    bool ambient_valid, float ambient_temperature)
{
    char date[11], clock_time[9], timestamp[20] = "";
    if (time_manager_get_local(date, clock_time))
        snprintf(timestamp, sizeof(timestamp), "%s %s", date, clock_time);

    portENTER_CRITICAL(&sensor_lock);
    cooling_ok = cooling_valid && isfinite(cooling_temperature);
    ambient_ok = ambient_valid && isfinite(ambient_temperature);
    cooling_temp = cooling_ok ? cooling_temperature : 0.0f;
    ambient_temp = ambient_ok ? ambient_temperature : 0.0f;
    memcpy(reading_time, timestamp, sizeof(reading_time));
    portEXIT_CRITICAL(&sensor_lock);
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, page1_html_start, page1_html_end - page1_html_start);
}

static esp_err_t status_get_handler(httpd_req_t *req)
{
    float cooling, ambient;
    bool cooling_valid, ambient_valid;
    char timestamp[sizeof(reading_time)];
    portENTER_CRITICAL(&sensor_lock);
    cooling = cooling_temp;
    ambient = ambient_temp;
    cooling_valid = cooling_ok;
    ambient_valid = ambient_ok;
    memcpy(timestamp, reading_time, sizeof(timestamp));
    portEXIT_CRITICAL(&sensor_lock);

    char json[384];
    snprintf(json, sizeof(json),
             "{\"cooling_valid\":%s,\"cooling_temperature\":%.2f,"
             "\"ambient_valid\":%s,\"ambient_temperature\":%.2f,"
             "\"sd_ready\":%s,\"time_synced\":%s,\"battery_simulated\":%u,"
             "\"day_time\":\"%s\"}",
             cooling_valid ? "true" : "false", cooling,
             ambient_valid ? "true" : "false", ambient,
             sd_logger_is_ready() ? "true" : "false",
             time_manager_is_set() ? "true" : "false",
             (unsigned)battery_monitor_get_percentage(), timestamp);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t time_get_handler(httpd_req_t *req)
{
    char query[96], epoch_str[40];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "epoch_ms", epoch_str, sizeof(epoch_str)) != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing epoch_ms");

    char *end;
    errno = 0;
    int64_t epoch_ms = strtoll(epoch_str, &end, 10);
    if (errno == ERANGE || end == epoch_str || *end != '\0' ||
        !time_manager_set_epoch_ms(epoch_ms))
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid time");

    ESP_LOGI(TAG, "Clock synchronised from browser");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

typedef struct
{
    httpd_req_t *req;
    bool error;
} csv_stream_ctx_t;

static bool csv_line_callback(const char *line, size_t length, void *context)
{
    csv_stream_ctx_t *ctx = (csv_stream_ctx_t *)context;
    if (httpd_resp_send_chunk(ctx->req, line, length) != ESP_OK)
    {
        ctx->error = true;
        return false;
    }
    return true;
}

static esp_err_t download_get_handler(httpd_req_t *req)
{
    if (!sd_logger_is_ready())
    {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "SD card not mounted");
    }
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    csv_stream_ctx_t ctx = {.req = req, .error = false};

    if (!sd_logger_read_recent(900, csv_line_callback, &ctx))
    {
        if (ctx.error)
            return ESP_FAIL;
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Cannot read SD history");
    }
    return httpd_resp_send_chunk(req, NULL, 0);
}

static esp_err_t delete_post_handler(httpd_req_t *req)
{
    FILE *f = fopen("/sdcard/temperature_log_dated.csv", "w");
    if (f)
    {
        fprintf(f, "day_time,temp_blood,temp_internal,battery\n");
        fclose(f);
        httpd_resp_send(req, "Data deleted", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    httpd_resp_send_500(req);
    return ESP_FAIL;
}


esp_err_t web_server_start(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        err = nvs_flash_erase();
        if (err != ESP_OK)
            return err;
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        return err;
    }
    err = esp_netif_init();
    if (err != ESP_OK)
    {
        return err;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK)
    {
        return err;
    }
    if (!esp_netif_create_default_wifi_ap())
    {
        return ESP_FAIL;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init);
    if (err != ESP_OK)
    {
        return err;
    }
    wifi_config_t ap = {0};
    memcpy(ap.ap.ssid, WIFI_SSID, strlen(WIFI_SSID));
    memcpy(ap.ap.password, WIFI_PASSWORD, strlen(WIFI_PASSWORD));
    ap.ap.ssid_len = strlen(WIFI_SSID);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    if (err != ESP_OK)
    {
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        return err;
    }
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        return err;
    }

    httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get_handler};
    httpd_uri_t download = {.uri = "/download", .method = HTTP_GET, .handler = download_get_handler};
    httpd_uri_t delete_data = {.uri = "/delete", .method = HTTP_POST, .handler = delete_post_handler};
    httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler};
    httpd_uri_t time_sync = {.uri = "/api/time", .method = HTTP_GET, .handler = time_get_handler};

    const httpd_uri_t *routes[] = {&root, &download, &delete_data, &status, &time_sync};
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i)
    {
        err = httpd_register_uri_handler(server, routes[i]);
        if (err != ESP_OK)
        {
            httpd_stop(server);
            return err;
        }
    }

    ESP_LOGI(TAG, "Web server started");
    ESP_LOGI(TAG, "Connect to %s and open http://192.168.4.1", WIFI_SSID);
    return ESP_OK;

}
