idf_component_register(
    SRCS
        "main.c"
        "temperature_sensor.c"
        "peltier_control.c"
        "segment_display.c"

    INCLUDE_DIRS
        "."

    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_spi
        fatfs
        sdmmc
)
