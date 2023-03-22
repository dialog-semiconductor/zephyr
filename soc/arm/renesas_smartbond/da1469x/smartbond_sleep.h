/*
 * Copyright (c) 2024 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _SLEEP__H_
#define _SLEEP__H_

#ifdef __cplusplus
extern "C" {
#endif

int z_smartbond_sleep(void);
void z_smartbond_wakeup_handler(void);
void z_smartbond_wakeup(void);
void z_smartbond_restore_state(void);

#ifdef __cplusplus
}
#endif

#endif /* _SLEEP__H_ */
