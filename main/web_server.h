#ifndef WEB_SERVER_H
#define WEB_SERVER_H
#include <stdbool.h>
#include "esp_err.h"
esp_err_t web_server_start(void);
void web_server_update_temperatures(bool cooling_valid, float cooling_temperature,
                                    bool ambient_valid, float ambient_temperature);
#endif
