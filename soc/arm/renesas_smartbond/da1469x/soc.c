/*
 * Copyright (c) 2022 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/arch/arm/aarch32/cortex_m/cmsis.h>
#include <zephyr/arch/arm/aarch32/nmi.h>
#include <zephyr/linker/linker-defs.h>
#include <string.h>
#include <DA1469xAB.h>
#include <da1469x_clock.h>
#include <da1469x_otp.h>
#include <da1469x_pd.h>
#include <da1469x_pdc.h>
#include <da1469x_trimv.h>

#define REMAP_ADR0_QSPI           0x2

#define FLASH_REGION_SIZE_32M     0
#define FLASH_REGION_SIZE_16M     1
#define FLASH_REGION_SIZE_8M      2
#define FLASH_REGION_SIZE_4M      3
#define FLASH_REGION_SIZE_2M      4
#define FLASH_REGION_SIZE_1M      5
#define FLASH_REGION_SIZE_05M     6
#define FLASH_REGION_SIZE_025M    7

enum REMAP_ADDR0 {
	REMAP_ADDR0_ROM = 0x0,
	REMAP_ADDR0_OTP,
	REMAP_ADDR0_QSPIF_CACHED,
	REMAP_ADDR0_SYSRAM,
	REMAP_ADDR0_MAX,
};

#if defined(CONFIG_BOOTLOADER_MCUBOOT)
#define MAGIC 0xaabbccdd
static uint32_t z_renesas_cache_configured;
#endif

void sys_arch_reboot(int type)
{
	ARG_UNUSED(type);

	NVIC_SystemReset();
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
#if defined(CONFIG_BOOTLOADER_MCUBOOT)
	z_renesas_configure_cache();
#endif
}

uint32_t black_orca_phy_addr(uint32_t addr)
{
	uint32_t remap_addr0;
	uint32_t phy_addr;

	static const uint32_t remap_bases[] = {
		[REMAP_ADDR0_ROM] = MCU_ROM_BASE,
		[REMAP_ADDR0_OTP] = MCU_OTP_M_BASE,
		[REMAP_ADDR0_QSPIF_CACHED] = MCU_QSPIF_M_CACHED_BASE,
		[REMAP_ADDR0_SYSRAM] = MCU_SYSRAM_M_BASE
	};

	static const uint32_t flash_region_sizes[] = {
		MB(32),
		MB(16),
		MB(8),
		MB(4),
		MB(2),
		MB(1),
		KB(512),
		KB(256)
	};

	remap_addr0 = CRG_TOP->SYS_CTRL_REG & CRG_TOP_SYS_CTRL_REG_REMAP_ADR0_Msk;
	assert(remap_addr0 < REMAP_ADDR0_MAX);

	if (remap_addr0 != REMAP_ADDR0_QSPIF_CACHED) {
		if (IS_REMAPPED_ADDRESS(addr)) {
			phy_addr = addr + remap_bases[remap_addr0];
		} else {
			phy_addr = addr;
		}
	} else {
		/* FLASH_REGION_BASE corresponds to Flash region base address bits [31:16] */
		uint32_t flash_region_base = ((CACHE->CACHE_FLASH_REG & CACHE_CACHE_FLASH_REG_FLASH_REGION_BASE_Msk) >>
																CACHE_CACHE_FLASH_REG_FLASH_REGION_BASE_Pos) << 16;
		/* FLASH_REGION_OFFSET corresponds to Flash region base address bits [13:2]. Offset is expressed in words. */
		flash_region_base += ((CACHE->CACHE_FLASH_REG & CACHE_CACHE_FLASH_REG_FLASH_REGION_OFFSET_Msk) >>
														CACHE_CACHE_FLASH_REG_FLASH_REGION_OFFSET_Pos) << 2;
		__unused uint32_t flash_region_size = flash_region_sizes[CACHE->CACHE_FLASH_REG & CACHE_CACHE_FLASH_REG_FLASH_REGION_SIZE_Msk];

		if (IS_REMAPPED_ADDRESS(addr)) {
			assert(addr < flash_region_size);

			phy_addr = addr + flash_region_base;
		} else if (IS_QSPIF_CACHED_ADDRESS(addr)) {
			assert(addr >= flash_region_base && addr < (flash_region_base + flash_region_size));

			phy_addr = addr;
		} else {
			phy_addr = addr;
		}
	}

	return phy_addr;
}

static int renesas_da1469x_init(void)
{
	NMI_INIT();

	/* Freeze watchdog until configured */
	GPREG->SET_FREEZE_REG = GPREG_SET_FREEZE_REG_FRZ_SYS_WDOG_Msk;

	/* Reset clock dividers to 0 */
	CRG_TOP->CLK_AMBA_REG &= ~(CRG_TOP_CLK_AMBA_REG_HCLK_DIV_Msk |
				CRG_TOP_CLK_AMBA_REG_PCLK_DIV_Msk);

	CRG_TOP->PMU_CTRL_REG |= (CRG_TOP_PMU_CTRL_REG_TIM_SLEEP_Msk   |
				CRG_TOP_PMU_CTRL_REG_PERIPH_SLEEP_Msk  |
				CRG_TOP_PMU_CTRL_REG_COM_SLEEP_Msk     |
				CRG_TOP_PMU_CTRL_REG_RADIO_SLEEP_Msk);

	/* PDC should take care of PD_SYS */
	CRG_TOP->PMU_CTRL_REG &= ~CRG_TOP_PMU_CTRL_REG_SYS_SLEEP_Msk;

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
	da1469x_pd_acquire(MCU_PD_DOMAIN_COM);

	/* Configure XTAL32M once */
	da1469x_clock_sys_xtal32m_configure();

	return 0;
}

SYS_INIT(renesas_da1469x_init, PRE_KERNEL_1, 0);
