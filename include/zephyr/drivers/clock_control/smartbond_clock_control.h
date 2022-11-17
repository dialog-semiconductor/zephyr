/*
 * Copyright (c) 2022 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_SMARTBOND_CLOCK_CONTROL_H_
#define ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_SMARTBOND_CLOCK_CONTROL_H_

#include <zephyr/device.h>
#include <zephyr/sys/onoff.h>
#include <zephyr/drivers/clock_control.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Request PLL.
 *
 * Function starts PLL if not already running.
 * USB requires PLL to operate. PLL can also be turned on for some other
 * peripherals or CPU to increase performance.
 *
 * Function does not perform any validation. It is the caller responsibility to
 * ensure that every z_smartbond_clock_pll_request matches
 * z_smartbond_clock_pll_release call.
 */
void z_smartbond_clock_pll_request(void);

/** @brief Release PLL.
 *
 * See z_smartbond_clock_pll_request for details.
 */
void z_smartbond_clock_pll_release(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_SMARTBOND_CLOCK_CONTROL_H_ */
