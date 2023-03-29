/**
 * @file Constants and functions for communicating with TP-Link Kasa IoT smart devices
 */

/* system includes */
#include <esp_log.h>

/* local includes */
#include "am2302.h"
#include "thsensor.h"


#define AM2302_SDA_PIN GPIO_NUM_37

static const char *log_tag = "thsensor";


float thsensor_read_humidity(void)
{
    am2302_data_t data = am2302_read_data(AM2302_SDA_PIN);
    if (data.error != ESP_OK) {
        ESP_LOGE(log_tag, "Error reading AM2302 T&H sensor");
        return 0;
    }
    return data.humidity / 10;
}

float thsensor_read_temperature(void)
{
    am2302_data_t data = am2302_read_data(AM2302_SDA_PIN);
    if (data.error != ESP_OK) {
        ESP_LOGE(log_tag, "Error reading AM2302 T&H sensor");
        return 0;
    }
    return data.temperature / 10;
}
