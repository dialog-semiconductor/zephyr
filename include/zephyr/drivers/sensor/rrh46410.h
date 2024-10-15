/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_RRH46410_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_RRH46410_H_

#include <zephyr/drivers/sensor.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>

enum sensor_channel_rrh46410 {
    /**
	* 1 byte
	*/
    SENSOR_CHAN_IAQ,
    /**
	* 2 bytes
	*/
    SENSOR_CHAN_TVOC,
    /**
	* 2 bytes
	*/
    SENSOR_CHAN_ETOH,
    /**
	* 2 bytes
	*/
    SENSOR_CHAN_ECO2,
    /**
	* 1 byte
	*/
    SENSOR_CHAN_RELIAQ,
};

enum sensor_attribute_rrh46410 {
    SENSOR_ATTR_RRH46410_HUMIDITY = SENSOR_ATTR_PRIV_START,
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_RRH46410_H_ */