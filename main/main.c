/**
 * @file Main application code
 */
 
/* system includes */
#include <esp_log.h>

/* local includes */
#include "thsensor.h"
#include "wifi.h"


/**
 * @brief Application main entry point
 */
void app_main(void)
{
    //wifi_setup(false);
    
    float temp = thsensor_read_temperature();
    ESP_LOGI("main", "Temperature = %.1f*C", temp);
}
