/**
 * @file Constants and functions for communicating with temperature and humidity sensor
 */

#ifndef INTELLILIGHT_THSENSOR_H
#define INTELLILIGHT_THSENSOR_H

/* system includes */
#include <unistd.h>


/**
 * @brief Read humidity from sensor
 * @return Humidity value
 */ 
extern float thsensor_read_humidity(void);

/**
 * @brief Read temperature from sensor
 * @return Temperature value
 */ 
extern float thsensor_read_temperature(void);

#endif
