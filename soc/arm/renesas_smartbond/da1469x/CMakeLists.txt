# SPDX-License-Identifier: Apache-2.0

zephyr_linker_sources(
  RAM_SECTIONS
  SORT_KEY 0
  intvect_reserved.ld
)
zephyr_library()
zephyr_library_sources(soc.c)
zephyr_library_sources_ifdef(CONFIG_PM power.c)
zephyr_library_sources_ifdef(CONFIG_PM sleep.S)

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/arm/cortex_m/scripts/linker.ld CACHE INTERNAL "")
