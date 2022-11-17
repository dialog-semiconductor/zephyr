/*
 * Copyright (c) 2022 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <soc.h>
#include <zephyr/sys/onoff.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/smartbond_clock_control.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(clock_control, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

#define DT_DRV_COMPAT smartbond_clock


static atomic_t pll_lock_count;

static void pll_start(void)
{
	/* TODO: use PDC instead */
	CRG_XTAL->XTAL32M_CTRL1_REG |= CRG_XTAL_XTAL32M_CTRL1_REG_XTAL32M_XTAL_ENABLE_Msk;

	/* Start PLL LDO if not done yet */
	if ((CRG_XTAL->PLL_SYS_STATUS_REG &
	     CRG_XTAL_PLL_SYS_STATUS_REG_LDO_PLL_OK_Msk) == 0) {
		CRG_XTAL->PLL_SYS_CTRL1_REG |=
			CRG_XTAL_PLL_SYS_CTRL1_REG_LDO_PLL_ENABLE_Msk;
		/* Wait for XTAL LDO to settle */
		k_busy_wait(20);
	}
	if ((CRG_XTAL->PLL_SYS_STATUS_REG &
	     CRG_XTAL_PLL_SYS_STATUS_REG_PLL_LOCK_FINE_Msk) == 0) {
		/* Enables DXTAL for the system PLL */
		CRG_XTAL->XTAL32M_CTRL0_REG |=
			CRG_XTAL_XTAL32M_CTRL0_REG_XTAL32M_DXTAL_SYSPLL_ENABLE_Msk;
		/* Use internal VCO current setting to enable precharge */
		CRG_XTAL->PLL_SYS_CTRL1_REG |=
			CRG_XTAL_PLL_SYS_CTRL1_REG_PLL_SEL_MIN_CUR_INT_Msk;
		/* Enable precharge */
		CRG_XTAL->PLL_SYS_CTRL2_REG |=
			CRG_XTAL_PLL_SYS_CTRL2_REG_PLL_RECALIB_Msk;
		/* Start the SYSPLL */
		CRG_XTAL->PLL_SYS_CTRL1_REG |=
			CRG_XTAL_PLL_SYS_CTRL1_REG_PLL_EN_Msk;
		/* Precharge loopfilter (Vtune) */
		k_busy_wait(10);
		/* Disable precharge */
		CRG_XTAL->PLL_SYS_CTRL2_REG &=
			~CRG_XTAL_PLL_SYS_CTRL2_REG_PLL_RECALIB_Msk;
		/* Extra wait time */
		k_busy_wait(5);
		/* Take external VCO current setting */
		CRG_XTAL->PLL_SYS_CTRL1_REG &=
			~CRG_XTAL_PLL_SYS_CTRL1_REG_PLL_SEL_MIN_CUR_INT_Msk;
	}
}

static void pll_stop(void)
{
	CRG_XTAL->PLL_SYS_CTRL1_REG &=
		~CRG_XTAL_PLL_SYS_CTRL1_REG_PLL_EN_Msk;
	CRG_XTAL->PLL_SYS_CTRL1_REG &=
		~CRG_XTAL_PLL_SYS_CTRL1_REG_LDO_PLL_ENABLE_Msk;
}

void z_smartbond_clock_pll_request(void)
{
	if (atomic_inc(&pll_lock_count)) {
		/* PLL start requested before. */
		return;
	}

	pll_start();
}

void z_smartbond_clock_pll_release(void)
{
	if (atomic_dec(&pll_lock_count) > 1) {
		return;
	}

	pll_stop();
}


