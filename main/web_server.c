#include "web_server.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sd_logger.h"
#include "battery_monitor.h"
#include "time_manager.h"

#define WIFI_SSID "EEE40011-PROJECT"
#define WIFI_PASSWORD "123456789"
#define HISTORY_MAX_RECORDS 900

static const char *TAG = "WEB";
static portMUX_TYPE sensor_lock = portMUX_INITIALIZER_UNLOCKED;
static float cooling_temp = 0, ambient_temp = 0;
static bool cooling_ok = false, ambient_ok = false;
static httpd_handle_t server = NULL;
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

void web_server_update_temperatures(bool cv, float c, bool av, float a) {
    portENTER_CRITICAL(&sensor_lock);
    cooling_ok = cv; ambient_ok = av;
    if (cv) cooling_temp = c;
    if (av) ambient_temp = a;
    portEXIT_CRITICAL(&sensor_lock);
}
static esp_err_t page_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
}
static esp_err_t status_handler(httpd_req_t *req) {
    float c, a; bool cv, av;
    portENTER_CRITICAL(&sensor_lock);
    c = cooling_temp; a = ambient_temp; cv = cooling_ok; av = ambient_ok;
    portEXIT_CRITICAL(&sensor_lock);
    char json[256];
    snprintf(json, sizeof(json),
        "{\"cooling_valid\":%s,\"cooling_temperature\":%.2f,"
        "\"ambient_valid\":%s,\"ambient_temperature\":%.2f,"
        "\"sd_ready\":%s,\"time_synced\":%s,\"battery_simulated\":%u}",
        cv ? "true" : "false", cv ? c : 0.0f,
        av ? "true" : "false", av ? a : 0.0f,
        sd_logger_is_ready() ? "true" : "false",
        time_manager_is_set() ? "true" : "false",
        (unsigned)battery_monitor_get_percentage());
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}
static esp_err_t sync_handler(httpd_req_t *req) {
    char query[96], epoch_str[40] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK || httpd_query_key_value(query, "epoch_ms", epoch_str, sizeof(epoch_str)) != ESP_OK){
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing epoch_ms");
        return ESP_OK;
    }
    char *end = NULL;
    int64_t ms = strtoll(epoch_str, &end, 10);
    if (!end || *end != '\0' || !time_manager_set_epoch_ms(ms)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid time");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Clock synchronised from browser");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}
static bool send_line(const char *line, size_t n, void *context) {
    httpd_req_t *req = context;
    return httpd_resp_send_chunk(req, line, n) == ESP_OK;
}
static esp_err_t history_handler(httpd_req_t *req) {
    if (!sd_logger_is_ready()) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_sendstr(req, "SD card not mounted");
    }
    httpd_resp_set_type(req, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    bool ok = sd_logger_read_recent(HISTORY_MAX_RECORDS, send_line, req);
    if (!ok) return ESP_FAIL;
    return httpd_resp_send_chunk(req, NULL, 0);
}
esp_err_t web_server_start(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) return err;
        err = nvs_flash_init();
    }
    if (err != ESP_OK){
        return err;
    }
    err = esp_netif_init(); 
    if (err != ESP_OK){
        return err;
    }
    err = esp_event_loop_create_default(); 
    if (err != ESP_OK){
        return err;
    }
    if (!esp_netif_create_default_wifi_ap()){
        return ESP_FAIL;
    }
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init); 
    if (err != ESP_OK){
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
    if (err != ESP_OK){
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_AP, &ap); 
    if (err != ESP_OK){
        return err;
    }
    err = esp_wifi_start(); 
    if (err != ESP_OK){
    return err;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    err = httpd_start(&server, &config); 
    if (err != ESP_OK){
        return err;
    }
    httpd_uri_t page = {.uri = "/", .method = HTTP_GET, .handler = page_handler};
    httpd_uri_t status = {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler};
    httpd_uri_t history = {.uri = "/api/history", .method = HTTP_GET, .handler = history_handler};
    httpd_uri_t sync = {.uri = "/api/time", .method = HTTP_GET, .handler = sync_handler};
    err = httpd_register_uri_handler(server, &page); 
    if (err != ESP_OK){
        return err;
    }
    err = httpd_register_uri_handler(server, &status); 
    if (err != ESP_OK){
        return err;
    }
    err = httpd_register_uri_handler(server, &history); 
    if (err != ESP_OK){
        return err;
    }
    err = httpd_register_uri_handler(server, &sync); 
    if (err != ESP_OK){
        return err;
    }
    ESP_LOGI(TAG, "Connect to %s and open http://192.168.4.1", WIFI_SSID);
    return ESP_OK;
}
