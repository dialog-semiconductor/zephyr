/*
 * Copyright (c) 2022 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include <DA1469xAB.h>
#include <da1469x_clock.h>
#include <da1469x_otp.h>
#include <da1469x_pd.h>
#include <da1469x_pdc.h>
#include <da1469x_trimv.h>
#include <cmsis_core.h>

#define LOG_LEVEL CONFIG_SOC_LOG_LEVEL
LOG_MODULE_REGISTER(soc);

#define REMAP_ADR0_QSPI           0x2

#define FLASH_REGION_SIZE_32M     0
#define FLASH_REGION_SIZE_16M     1
#define FLASH_REGION_SIZE_8M      2
#define FLASH_REGION_SIZE_4M      3
#define FLASH_REGION_SIZE_2M      4
#define FLASH_REGION_SIZE_1M      5
#define FLASH_REGION_SIZE_05M     6
#define FLASH_REGION_SIZE_025M    7

#define VDD_SLEEP_LEVEL_0V750	0
#define VDD_CLAMP_LEVEL_0V706	15
#define V18_LEVEL_1V8		1
#define V14_LEVEL_1V40		4
#define V30_LEVEL_3V00		0

#define POWER_CTRL_REG_SET(_field, _val) \
	CRG_TOP->POWER_CTRL_REG = \
	(CRG_TOP->POWER_CTRL_REG & ~CRG_TOP_POWER_CTRL_REG_ ## _field ## _Msk) | \
	((_val) << CRG_TOP_POWER_CTRL_REG_ ## _field ## _Pos)

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#define MAGIC 0xaabbccdd
static uint32_t z_renesas_cache_configured;
#endif

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	NVIC_SystemReset();
}

void
z_smartbond_vdd_level_set(uint8_t vdd_level)
{
	POWER_CTRL_REG_SET(VDD_LEVEL, vdd_level);
}

static void z_smartbond_configure_power_rails(void)
{
	POWER_CTRL_REG_SET(LDO_3V0_MODE, 3);
	POWER_CTRL_REG_SET(LDO_3V0_REF, 1);
	POWER_CTRL_REG_SET(V30_LEVEL, V30_LEVEL_3V00);
	POWER_CTRL_REG_SET(LDO_3V0_RET_ENABLE_ACTIVE, 0);
	POWER_CTRL_REG_SET(LDO_3V0_RET_ENABLE_SLEEP, 1);
	POWER_CTRL_REG_SET(CLAMP_3V0_VBAT_ENABLE, 0);

	/* Enable V18, use LDO_IO in active mode, disable in sleep */
	POWER_CTRL_REG_SET(V18_LEVEL, V18_LEVEL_1V8);
	POWER_CTRL_REG_SET(LDO_1V8_RET_ENABLE_ACTIVE, 0);
	POWER_CTRL_REG_SET(LDO_1V8_RET_ENABLE_SLEEP, 0);
	POWER_CTRL_REG_SET(LDO_1V8_ENABLE, 1);

	/* Enable V18P, use LDO_IO2 in active mode and LDO_IO_RET2 in sleep */
	POWER_CTRL_REG_SET(LDO_1V8P_RET_ENABLE_ACTIVE, 0);
	POWER_CTRL_REG_SET(LDO_1V8P_RET_ENABLE_SLEEP, 1);
	POWER_CTRL_REG_SET(LDO_1V8P_ENABLE, 1);

	/* Configure VDD to 1.2V if PLL is enabled, 0.9V otherwise. */
	if (DT_NODE_HAS_STATUS(DT_NODELABEL(pll), okay)) {
		POWER_CTRL_REG_SET(VDD_LEVEL, VDD_LEVEL_1V2);
	} else {
		POWER_CTRL_REG_SET(VDD_LEVEL, VDD_LEVEL_0V9);
	}
	/* Configure VDD to 0.75V in sleep.
	 * Setting clamp to lower voltage than sleep LDO forces using LDO_CORE_RET in sleep.
	 */
	POWER_CTRL_REG_SET(VDD_SLEEP_LEVEL, VDD_SLEEP_LEVEL_0V750);
	POWER_CTRL_REG_SET(VDD_CLAMP_LEVEL, VDD_CLAMP_LEVEL_0V706);
	POWER_CTRL_REG_SET(LDO_CORE_RET_ENABLE_ACTIVE, 0);
	POWER_CTRL_REG_SET(LDO_CORE_RET_ENABLE_SLEEP, 1);
	POWER_CTRL_REG_SET(LDO_CORE_ENABLE, 1);

	POWER_CTRL_REG_SET(V14_LEVEL, V14_LEVEL_1V40);
}

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
static void z_renesas_configure_cache(void)
{
	uint32_t cache_start;
	uint32_t region_size;
	uint32_t reg_region_size;
	uint32_t reg_cache_len;

	if (z_renesas_cache_configured == MAGIC) {
		return;
	}

	cache_start = (uint32_t)&_vector_start;
	region_size = (uint32_t)&__rom_region_end - cache_start;

	/* Disable cache before configuring it */
	CACHE->CACHE_CTRL2_REG = 0;
	CRG_TOP->SYS_CTRL_REG &= ~CRG_TOP_SYS_CTRL_REG_CACHERAM_MUX_Msk;

	/* Disable MRM unit */
	CACHE->CACHE_MRM_CTRL_REG = 0;
	CACHE->CACHE_MRM_TINT_REG = 0;
	CACHE->CACHE_MRM_MISSES_THRES_REG = 0;

	if (region_size > MB(16)) {
		reg_region_size = FLASH_REGION_SIZE_32M;
	} else if (region_size > MB(8)) {
		reg_region_size = FLASH_REGION_SIZE_16M;
	} else if (region_size > MB(4)) {
		reg_region_size = FLASH_REGION_SIZE_8M;
	} else if (region_size > MB(2)) {
		reg_region_size = FLASH_REGION_SIZE_4M;
	} else if (region_size > MB(1)) {
		reg_region_size = FLASH_REGION_SIZE_2M;
	} else if (region_size > KB(512)) {
		reg_region_size = FLASH_REGION_SIZE_1M;
	} else if (region_size > KB(256)) {
		reg_region_size = FLASH_REGION_SIZE_05M;
	} else {
		reg_region_size = FLASH_REGION_SIZE_025M;
	}
	CACHE->CACHE_FLASH_REG =
		(cache_start >> 16) << CACHE_CACHE_FLASH_REG_FLASH_REGION_BASE_Pos |
		((cache_start & 0xffff) >> 2) << CACHE_CACHE_FLASH_REG_FLASH_REGION_OFFSET_Pos |
		reg_region_size << CACHE_CACHE_FLASH_REG_FLASH_REGION_SIZE_Pos;

	reg_cache_len = CLAMP(reg_region_size / KB(64), 0, 0x1ff);
	CACHE->CACHE_CTRL2_REG = (CACHE->CACHE_FLASH_REG & ~CACHE_CACHE_CTRL2_REG_CACHE_LEN_Msk) |
				 reg_cache_len;

	/* Copy IVT from flash to start of RAM.
	 * It will be remapped at 0x0 so it can be used after SW Reset
	 */
	memcpy(&_image_ram_start, &_vector_start, 0x200);

	/* Configure remapping */
	CRG_TOP->SYS_CTRL_REG = (CRG_TOP->SYS_CTRL_REG & ~CRG_TOP_SYS_CTRL_REG_REMAP_ADR0_Msk) |
				CRG_TOP_SYS_CTRL_REG_CACHERAM_MUX_Msk |
				CRG_TOP_SYS_CTRL_REG_REMAP_INTVECT_Msk |
				REMAP_ADR0_QSPI;

	z_renesas_cache_configured = MAGIC;

	/* Trigger SW Reset to apply cache configuration */
	CRG_TOP->SYS_CTRL_REG |= CRG_TOP_SYS_CTRL_REG_SW_RESET_Msk;
}
#endif /* CONFIG_HAS_FLASH_LOAD_OFFSET */

void z_arm_platform_init(void)
{
#if defined(CONFIG_PM)
	uint32_t *ivt;
#endif

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
	z_renesas_configure_cache();
#endif

#if defined(CONFIG_PM)
	/* IVT is always placed in reserved space at the start of RAM which
	 * is then remapped to 0x0 and retained. Generic reset handler is
	 * changed to custom routine since next time ARM core is reset we
	 * need to determine whether it was a regular reset or a wakeup from
	 * extended sleep and ARM core state needs to be restored.
	 */
	ivt = (uint32_t *)_image_ram_start;
	ivt[1] = (uint32_t)z_smartbond_wakeup_handler;
#endif
}

static int renesas_da1469x_init(void)
{
	/* Freeze watchdog until configured */
	GPREG->SET_FREEZE_REG = GPREG_SET_FREEZE_REG_FRZ_SYS_WDOG_Msk;
	SYS_WDOG->WATCHDOG_REG = SYS_WDOG_WATCHDOG_REG_WDOG_VAL_Msk;

	/* Reset clock dividers to 0 */
	CRG_TOP->CLK_AMBA_REG &= ~(CRG_TOP_CLK_AMBA_REG_HCLK_DIV_Msk |
				CRG_TOP_CLK_AMBA_REG_PCLK_DIV_Msk);

	CRG_TOP->PMU_CTRL_REG |= (CRG_TOP_PMU_CTRL_REG_TIM_SLEEP_Msk   |
				CRG_TOP_PMU_CTRL_REG_PERIPH_SLEEP_Msk  |
				CRG_TOP_PMU_CTRL_REG_COM_SLEEP_Msk     |
				CRG_TOP_PMU_CTRL_REG_RADIO_SLEEP_Msk);

	/* PDC should take care of PD_SYS */
	CRG_TOP->PMU_CTRL_REG &= ~CRG_TOP_PMU_CTRL_REG_SYS_SLEEP_Msk;

#if defined(CONFIG_PM)
	/* Enable cache retainability */
	CRG_TOP->PMU_CTRL_REG |= CRG_TOP_PMU_CTRL_REG_RETAIN_CACHE_Msk;
#endif

	/*
	 *	Due to crosstalk issues any power rail can potentially
	 *	issue a fake event. This is typically observed upon
	 *	switching power sources, that is DCDC <--> LDOs <--> Retention LDOs.
	 */
	CRG_TOP->BOD_CTRL_REG &= ~(CRG_TOP_BOD_CTRL_REG_BOD_V14_EN_Msk |
				CRG_TOP_BOD_CTRL_REG_BOD_V18F_EN_Msk   |
				CRG_TOP_BOD_CTRL_REG_BOD_VDD_EN_Msk    |
				CRG_TOP_BOD_CTRL_REG_BOD_V18P_EN_Msk   |
				CRG_TOP_BOD_CTRL_REG_BOD_V18_EN_Msk    |
				CRG_TOP_BOD_CTRL_REG_BOD_V30_EN_Msk    |
				CRG_TOP_BOD_CTRL_REG_BOD_VBAT_EN_Msk);

	da1469x_pdc_reset();

	da1469x_otp_init();
	da1469x_trimv_init_from_otp();

	da1469x_pd_init();
	da1469x_pd_acquire(MCU_PD_DOMAIN_SYS);
	da1469x_pd_acquire(MCU_PD_DOMAIN_TIM);

	/* Enable PD_COM for GPIO control. This reference is controlled by PM.
	 * References for other peripherals in PD_COM are controlled by their
	 * respective drivers.
	 */
	da1469x_pd_acquire(MCU_PD_DOMAIN_COM);


	z_smartbond_configure_power_rails();
	return 0;
}

SYS_INIT(renesas_da1469x_init, PRE_KERNEL_1, 0);
