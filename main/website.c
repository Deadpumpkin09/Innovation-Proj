#include "esp_http_server.h"
#include "esp_log.h"
#include "sd_logger.h"
#include "battery_monitor.h"
#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "WEBSITE";
#define WIFI_SSID "EEE40011-PROJECT"
#define WIFI_PASSWORD "123456789"

extern const char page1_html_start[] asm("_binary_page1_html_start");
extern const char page1_html_end[] asm("_binary_page1_html_end");

static esp_err_t root_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, page1_html_start, page1_html_end - page1_html_start);
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
    httpd_resp_set_type(req, "text/csv");
    csv_stream_ctx_t ctx = {.req = req, .error = false};

    if (!sd_logger_read_recent(900, csv_line_callback, &ctx))
    {
        httpd_resp_send_chunk(req, "day_time,temp_blood,temp_internal,battery\n", 41);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
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

    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &download);
    httpd_register_uri_handler(server, &delete_data);

    ESP_LOGI(TAG, "Web server started");
    ESP_LOGI(TAG, "Connect to %s and open http://192.168.4.1", WIFI_SSID);
    return ESP_OK;

    return ESP_FAIL;
}