/*
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_RRH62000_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_RRH62000_H_

enum sensor_channel_rrh62000 {
    /**
	* 2 bytes Number concentration of particle size 0.3μm-10μm
	*/
    SENSOR_CHAN_NC_0_3 = SENSOR_ATTR_PRIV_START,
    /**
	* 2 bytes Number concentration of particle size 0.5μm-10μm
	*/
    SENSOR_CHAN_NC_0_5,
    /**
	* 2 bytes Number concentration of particle size 1μm-10μm
	*/
    SENSOR_CHAN_NC_1,
    /**
	* 2 bytes Number concentration of particle size 2.5μm-10μm
	*/
    SENSOR_CHAN_NC_2_5,
    /**
	* 2 bytes Number concentration of particle size 4μm-10μm
	*/
    SENSOR_CHAN_NC_4,
	/**
	* 2 bytes Mass concentration of particle size 0.3 μm - 1 μm with reference to KCl particle
	*/
    SENSOR_CHAN_PM1_1,
    /**
	* 2 bytes Mass concentration of particle size 0.3 μm - 2.5 μm with reference to KCl particle
	*/
    SENSOR_CHAN_PM2_5_1,
    /**
	* 2 bytes Mass concentration of particle size 0.3 μm - 10 μm with reference to KCl particle
	*/
    SENSOR_CHAN_PM10_1,
	/**
	* 2 bytes Mass concentration of particle size 0.3 μm - 1 μm with reference to cigarette smoke
	*/
    SENSOR_CHAN_PM1_2,
    /**
	* 2 bytes Mass concentration of particle size 0.3 μm - 2.5 μm with reference to cigarette smoke
	*/
    SENSOR_CHAN_PM2_5_2,
    /**
	* 2 bytes Mass concentration of particle size 0.3 μm - 10 μm with reference to cigarette smoke
	*/
    SENSOR_CHAN_PM10_2,
	/**
	* 2 bytes Total volatile organic compounds (TVOC) concentrations 
	*/
    SENSOR_CHAN_TVOC,
	/**
	* 2 byte Estimated carbon dioxide (eCO2) level 
	*/
    SENSOR_CHAN_ECO2,
    /**
	* 2 bytes Indoor Air Quality level according to UBA
	*/
    SENSOR_CHAN_IAQ,
    /**
	* 2 bytes
	*/
    SENSOR_CHAN_RELIAQ,
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_RRH62000_H_ */