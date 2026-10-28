idf_component_register(
    SRCS
        "main.c"
        "temperature_sensor.c"
        "peltier_control.c"
        "segment_display.c"
        "sd_logger.c"
        "web_server.c"
        "time_manager.c"
        "battery_monitor.c"

    INCLUDE_DIRS
        "."

    EMBED_TXTFILES
        "index.html"

    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        esp_wifi
        esp_event
        esp_netif
        esp_http_server
        esp_timer
        nvs_flash
        fatfs
        sdmmc
        dht
)
