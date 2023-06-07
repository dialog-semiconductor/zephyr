/*
 * Copyright (c) 2022 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SOC__H_
#define _SOC__H_

#ifdef __cplusplus
extern "C" {
#endif

#include <DA1469xAB.h>

int z_smartbond_sleep(void);
void z_smartbond_wakeup_handler(void);
void z_smartbond_wakeup(void);
void z_smartbond_restore_state(void);

#define VDD_LEVEL_0V9		0
#define VDD_LEVEL_1V2		3

void z_smartbond_vdd_level_set(uint8_t vdd_level);

#ifdef __cplusplus
}
#endif

#endif /* _SOC__H_ */
