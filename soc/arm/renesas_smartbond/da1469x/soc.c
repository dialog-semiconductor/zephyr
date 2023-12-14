/*
 * Copyright (c) 2022 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/linker/linker-defs.h>
#include <zephyr/logging/log.h>
#include <string.h>
// #include <zephyr/arch/arm/aarch32/cortex_m/cmsis.h>
// #include <zephyr/arch/arm/aarch32/nmi.h>
// #include <zephyr/arch/common/sys_io.h>
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

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#define MAGIC 0xaabbccdd
static uint32_t z_renesas_cache_configured;
#endif

#if defined(CONFIG_PM)
struct dcdc_regs {
	uint32_t v18;
	uint32_t v18p;
	uint32_t vdd;
	uint32_t v14;
	uint32_t ctrl1;
};

static struct dcdc_regs dcdc_state;
#endif

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	NVIC_SystemReset();
}

void z_smartbond_restore_dcdc_state(void)
{
	/*
	 * Enabling DCDC while VBAT is below 2.5 V renders system unstable even
	 * if VBUS is available. Enable DCDC only if VBAT is above minimal value.
	 */
	if (CRG_TOP->ANA_STATUS_REG & CRG_TOP_ANA_STATUS_REG_COMP_VBAT_HIGH_Msk) {
#if defined(CONFIG_PM)
		DCDC->DCDC_V18_REG = dcdc_state.v18;
		DCDC->DCDC_V18P_REG = dcdc_state.v18p;
		DCDC->DCDC_VDD_REG = dcdc_state.vdd;
		DCDC->DCDC_V14_REG = dcdc_state.v14;
		DCDC->DCDC_CTRL1_REG = dcdc_state.ctrl1;
#endif
	}
}

void z_smartbond_store_dcdc_state(void)
{
#if defined(CONFIG_PM)
	dcdc_state.v18 = DCDC->DCDC_V18_REG;
	dcdc_state.v18p = DCDC->DCDC_V18P_REG;
	dcdc_state.vdd = DCDC->DCDC_VDD_REG;
	dcdc_state.v14 = DCDC->DCDC_V14_REG;
	dcdc_state.ctrl1 = DCDC->DCDC_CTRL1_REG;
#endif
}

static inline void write32_mask(uint32_t mask, uint32_t data, mem_addr_t addr)
{
	uint32_t val = sys_read32(addr);

	sys_write32((val & (~mask)) | (data & mask), addr);
}

void da1469x_pd_apply_preferred(uint8_t pd)
{
	switch (pd) {
	case MCU_PD_DOMAIN_AON:
		if (sys_read32(0x500000f8) == 0x00008800) {
			sys_write32(0x00007700, 0x500000f8);
		}
		write32_mask(0x00001000, 0x00001020, 0x50000050);
		sys_write32(0x000000ca, 0x500000a4);
		write32_mask(0x0003ffff, 0x041e6ef4, 0x50000064);
		break;
	case MCU_PD_DOMAIN_SYS:
		write32_mask(0x00000c00, 0x003f6a78, 0x50040400);
		write32_mask(0x000003ff, 0x00000002, 0x50040454);
		break;
	case MCU_PD_DOMAIN_TIM:
		write32_mask(0x3ff00000, 0x000afd70, 0x50010000);
		write32_mask(0x000000c0, 0x00000562, 0x50010010);
		write32_mask(0x43c38002, 0x4801e6b6, 0x50010030);
		write32_mask(0x007fff00, 0x7500a1a4, 0x50010034);
		write32_mask(0x00000fff, 0x001e45c4, 0x50010038);
		write32_mask(0x40000000, 0x40096255, 0x5001003c);
		write32_mask(0x00c00000, 0x00c00000, 0x50010040);
		write32_mask(0x000000ff, 0x00000180, 0x50010018);
		break;
	}
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
	/* Enable all power domains except for radio */
	CRG_TOP->PMU_CTRL_REG |= CRG_TOP_PMU_CTRL_REG_TIM_SLEEP_Msk |
				 CRG_TOP_PMU_CTRL_REG_PERIPH_SLEEP_Msk |
				 CRG_TOP_PMU_CTRL_REG_COM_SLEEP_Msk |
				 CRG_TOP_PMU_CTRL_REG_RADIO_SLEEP_Msk;
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

	da1469x_clock_sys_xtal32m_init();
	da1469x_clock_sys_xtal32m_enable();
	da1469x_clock_sys_xtal32m_switch_safe();
	da1469x_clock_sys_rc32m_disable();

	return 0;
}

SYS_INIT(renesas_da1469x_init, PRE_KERNEL_1, 0);
