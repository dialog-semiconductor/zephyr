/*
 * Copyright (c) 2022 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/arch/arm/aarch32/cortex_m/cmsis.h>
#include <zephyr/arch/arm/aarch32/nmi.h>
#include <da1469x_pd.h>

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	NVIC_SystemReset();
}

static inline uint32_t
get_reg32(uint32_t addr)
{
	volatile uint32_t *reg = (volatile uint32_t *)addr;

	return *reg;
}

static inline void
set_reg32(uint32_t addr, uint32_t val)
{
	volatile uint32_t *reg = (volatile uint32_t *)addr;

	*reg = val;
}

static inline void
set_reg32_mask(uint32_t addr, uint32_t mask, uint32_t val)
{
	volatile uint32_t *reg = (volatile uint32_t *)addr;

	*reg = (*reg & (~mask)) | (val & mask);
}

void
da1469x_pd_apply_preferred(uint8_t pd)
{
	switch (pd) {
	case MCU_PD_DOMAIN_AON:
		if (get_reg32(0x500000f8) == 0x00008800) {
			set_reg32(0x500000f8, 0x00007700);
		}
		set_reg32_mask(0x50000050, 0x00001000, 0x00001020);
		set_reg32(0x500000a4, 0x000000ca);
		set_reg32_mask(0x50000064, 0x0003ffff, 0x041e6ef4);
		break;
	case MCU_PD_DOMAIN_SYS:
		set_reg32_mask(0x50040400, 0x00000c00, 0x003f6a78);
		set_reg32_mask(0x50040454, 0x000003ff, 0x00000002);
		break;
	case MCU_PD_DOMAIN_TIM:
		set_reg32_mask(0x50010000, 0x3ff00000, 0x000afd70);
		set_reg32_mask(0x50010010, 0x000000c0, 0x00000562);
		set_reg32_mask(0x50010030, 0x43c38002, 0x4801e6b6);
		set_reg32_mask(0x50010034, 0x007fff00, 0x7500a1a4);
		set_reg32_mask(0x50010038, 0x00000fff, 0x001e45c4);
		set_reg32_mask(0x5001003c, 0x40000000, 0x40096255);
		set_reg32_mask(0x50010040, 0x00c00000, 0x00c00000);
		set_reg32_mask(0x50010018, 0x000000ff, 0x00000180);
		break;
	}
}

static int renesas_da14699_init(void)
{

	NMI_INIT();

	/* Freeze watchdog until configured */
	GPREG->SET_FREEZE_REG = GPREG_SET_FREEZE_REG_FRZ_SYS_WDOG_Msk;
	/* Reset clock dividers to 0 */
	CRG_TOP->CLK_AMBA_REG &= ~(CRG_TOP_CLK_AMBA_REG_HCLK_DIV_Msk |
				   CRG_TOP_CLK_AMBA_REG_PCLK_DIV_Msk);
	/* Enable all power domains except for radio */
	CRG_TOP->PMU_CTRL_REG = 0x02;

	return 0;
}

SYS_INIT(renesas_da14699_init, PRE_KERNEL_1, 0);
