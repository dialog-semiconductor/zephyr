/*
 * Copyright (c) 2023 Renesas Electronics Corporation and/or its affiliates
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/linker/linker-defs.h>
#include <string.h>
#include <DA1470x-00.h>
// #include <da1469x_clock.h>
// #include <da1469x_otp.h>
// #include <da1469x_pd.h>
// #include <da1469x_pdc.h>
// #include <da1469x_trimv.h>
#include <cmsis_core.h>
//#include <zephyr/arch/arm/aarch32/cortex_m/cmsis.h>
//#include <zephyr/arch/arm/aarch32/nmi.h>





// #define REMAP_ADR0_QSPI           0x2

// #define FLASH_REGION_SIZE_128M    9
// #define FLASH_REGION_SIZE_64M     8
// #define FLASH_REGION_SIZE_32M     7
// #define FLASH_REGION_SIZE_16M     6
// #define FLASH_REGION_SIZE_8M      5
// #define FLASH_REGION_SIZE_4M      4
// #define FLASH_REGION_SIZE_2M      3
// #define FLASH_REGION_SIZE_1M      2
// #define FLASH_REGION_SIZE_05M     1
// #define FLASH_REGION_SIZE_025M    0


void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	NVIC_SystemReset();
}

void z_arm_platform_init(void)
{
// #if defined(CONFIG_BOOTLOADER_MCUBOOT)
// 	z_renesas_configure_cache();
// #endif
volatile int a = 0;
a ++;
}

static int renesas_da1470x_init(void)
{
	//ARG_UNUSED(dev);

	//NMI_INIT();

	/* Freeze watchdog until configured */
	GPREG->SET_FREEZE_REG = GPREG_SET_FREEZE_REG_FRZ_SYS_WDOG_Msk;
	/* Reset clock dividers to 0 */
	CRG_TOP->CLK_AMBA_REG &= ~(CRG_TOP_CLK_AMBA_REG_HCLK_DIV_Msk |
				   CRG_TOP_CLK_AMBA_REG_PCLK_DIV_Msk);
	/* Enable all power domains except for radio */
	CRG_TOP->PMU_CTRL_REG = 0x02;

	return 0;
}

SYS_INIT(renesas_da1470x_init, PRE_KERNEL_1, 0);
