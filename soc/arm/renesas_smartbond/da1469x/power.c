/*
 * Copyright (c) 2023 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/pm/pm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/barrier.h>
#include <soc.h>
#include <DA1469xAB.h>
#include <da1469x_clock.h>
#include <da1469x_otp.h>
#include <da1469x_pd.h>
#include <da1469x_pdc.h>

LOG_MODULE_DECLARE(soc, CONFIG_SOC_LOG_LEVEL);

static int pdc_idx_combo;
static int pdc_idx_sw_trigger;
static bool wait_for_jtag;

#define GPIO0_NGPIOS DT_PROP(DT_NODELABEL(gpio0), ngpios)
#define GPIO1_NGPIOS DT_PROP(DT_NODELABEL(gpio1), ngpios)

struct gpio_state_data {
	uint32_t data0;
	uint32_t mode0[GPIO0_NGPIOS];
	uint32_t data1;
	uint32_t mode1[GPIO1_NGPIOS];
};

static struct gpio_state_data gpio_state;

static bool z_smartbond_is_sleep_allowed(void)
{
	if (wait_for_jtag) {
		if (CRG_TOP->SYS_STAT_REG & CRG_TOP_SYS_STAT_REG_DBG_IS_ACTIVE_Msk) {
			wait_for_jtag = false;
		}
		return false;
	}

	/* We can enter extended sleep only if running from RCX or XTAL32K, debugger is
	 * not attached and there are no interrupts pending.
	 */
	return (CRG_TOP->CLK_CTRL_REG & CRG_TOP_CLK_CTRL_REG_LP_CLK_SEL_Msk) &&
	       !(CRG_TOP->SYS_STAT_REG & CRG_TOP_SYS_STAT_REG_DBG_IS_ACTIVE_Msk) &&
	       !((NVIC->ISPR[0] & NVIC->ISER[0]) | (NVIC->ISPR[1] & NVIC->ISER[1]));
}

static bool z_smartbond_is_wakeup_by_jtag(void)
{
	return (da1469x_pdc_is_pending(pdc_idx_combo) &&
		!(NVIC->ISPR[0] & ((1 << CMAC2SYS_IRQn) | (1 << KEY_WKUP_GPIO_IRQn) |
				   (1 << VBUS_IRQn))));
}

static void gpio_latch_inst(mem_addr_t data_reg, mem_addr_t mode_reg, mem_addr_t latch_reg,
			    uint8_t ngpios, uint32_t *data, uint32_t *mode)
{
	uint8_t idx;

	*data = sys_read32(data_reg);
	for (idx = 0; idx < ngpios; idx++, mode_reg += 4) {
		mode[idx] = sys_read32(mode_reg);
	}
	sys_write32(BIT_MASK(ngpios), latch_reg);

}

static void gpio_unlatch_inst(mem_addr_t data_reg, mem_addr_t mode_reg, mem_addr_t latch_reg,
			      uint8_t ngpios, uint32_t data, uint32_t *mode)
{
	uint8_t idx;

	sys_write32(data, data_reg);
	for (idx = 0; idx < ngpios; idx++, mode_reg += 4) {
		sys_write32(mode[idx], mode_reg);
	}
	sys_write32(BIT_MASK(ngpios), latch_reg);
}

static void gpio_latch(void)
{
	gpio_latch_inst(DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio0), data),
			DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio0), mode),
			DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio0), latch) + 8,
			GPIO0_NGPIOS, &gpio_state.data0, gpio_state.mode0);
	gpio_latch_inst(DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio1), data),
			DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio1), mode),
			DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio1), latch) + 8,
			GPIO1_NGPIOS, &gpio_state.data1, gpio_state.mode1);

	da1469x_pd_release(MCU_PD_DOMAIN_COM);
}

static void gpio_unlatch(void)
{
	da1469x_pd_acquire(MCU_PD_DOMAIN_COM);

	gpio_unlatch_inst(DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio0), data),
			  DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio0), mode),
			  DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio0), latch) + 4,
			  GPIO0_NGPIOS, gpio_state.data0, gpio_state.mode0);
	gpio_unlatch_inst(DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio1), data),
			  DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio1), mode),
			  DT_REG_ADDR_BY_NAME(DT_NODELABEL(gpio1), latch) + 4,
			  GPIO1_NGPIOS, gpio_state.data1, gpio_state.mode1);
}

__weak void pm_state_set(enum pm_state state, uint8_t substate_id)
{
	int slept;

	ARG_UNUSED(substate_id);

	switch (state) {
	case PM_STATE_STANDBY:
		/* We enter here with interrupts disabled by BASEPRI which prevents wfi from
		 * waking up on interrupts. Need to disable interrupts by PRIMASK instead and
		 * reset BASEPRI to 0.
		 */
		__disable_irq();
		arch_irq_unlock(0);

		if (!z_smartbond_is_sleep_allowed()) {
			barrier_dsync_fence_full();
			__WFI();
			break;
		}

		gpio_latch();

		da1469x_pdc_set(pdc_idx_sw_trigger);

		/* PD_SYS will not be disabled here until we enter deep sleep - don't wait */
		if (!da1469x_pd_release_nowait(MCU_PD_DOMAIN_SYS)) {
			barrier_dsync_fence_full();
			__WFI();
			slept = 0;
		} else {
			z_smartbond_store_dcdc_state();
			da1469x_pdc_ack_all_m33();
			slept = z_smartbond_sleep();
		}

		gpio_unlatch();

		if (slept) {
			if (!IS_ENABLED(CONFIG_WDT_SMARTBOND)) {
				/* Watchdog is always resumed when PD_SYS is turned off, need to
				 * freeze it again if there's no one to feed it.
				 */
				GPREG->SET_FREEZE_REG = GPREG_SET_FREEZE_REG_FRZ_SYS_WDOG_Msk;
				SYS_WDOG->WATCHDOG_REG = SYS_WDOG_WATCHDOG_REG_WDOG_VAL_Msk;
			}

			z_smartbond_restore_dcdc_state();

			da1469x_pd_acquire(MCU_PD_DOMAIN_SYS);

			if (z_smartbond_is_wakeup_by_jtag()) {
				wait_for_jtag = 1;
			}

			if (DT_SAME_NODE(DT_PROP(DT_NODELABEL(sys_clk), clock_src),
					 DT_NODELABEL(xtal32m))) {
				da1469x_clock_sys_xtal32m_switch_safe();
			}
		} else {
			da1469x_pd_acquire_noconf(MCU_PD_DOMAIN_SYS);
		}

		break;
	default:
		LOG_DBG("Unsupported power state %u", state);
		break;
	}
}

__weak void pm_state_exit_post_ops(enum pm_state state, uint8_t substate_id)
{
	ARG_UNUSED(state);
	ARG_UNUSED(substate_id);

	__enable_irq();
}

static int renesas_da1469x_pm_init(void)
{
	int pdc_idx;
	uint8_t en_xtal;

	en_xtal = DT_NODE_HAS_STATUS(DT_NODELABEL(xtal32m), okay) ? MCU_PDC_EN_XTAL : 0;

	pdc_idx = da1469x_pdc_add(MCU_PDC_TRIGGER_COMBO, MCU_PDC_MASTER_M33, en_xtal);
	__ASSERT_NO_MSG(pdc_idx >= 0);
	da1469x_pdc_set(pdc_idx);
	da1469x_pdc_ack(pdc_idx);
	pdc_idx_combo = pdc_idx;

	pdc_idx = da1469x_pdc_add(MCU_PDC_TRIGGER_SW_TRIGGER, MCU_PDC_MASTER_M33, en_xtal);
	__ASSERT_NO_MSG(pdc_idx >= 0);
	da1469x_pdc_set(pdc_idx);
	da1469x_pdc_ack(pdc_idx);
	pdc_idx_sw_trigger = pdc_idx;

	return 0;
}

SYS_INIT(renesas_da1469x_pm_init, PRE_KERNEL_2, 2);
