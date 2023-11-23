/*
 * Copyright (c) 2023 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_SMARTBOND_MIPI_DBI_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_SMARTBOND_MIPI_DBI_H_

/* Pixel (output) color formats supported by the MIPI DBI host */

/** 0 0 R G B R' G' B' */
#define SMARTBOND_MIPI_DBI_P_8RGB111_1    0x06
/** R G B 0 R' G' B' 0 */
#define SMARTBOND_MIPI_DBI_P_8RGB111_2    0x07
/** R G B  R' G' B' ... */
#define SMARTBOND_MIPI_DBI_P_RGB111       0x08
/** D D' D'' ... */
#define SMARTBOND_MIPI_DBI_P_L1           0x09
/** R[2-0]G[2-0]B[1-0] */
#define SMARTBOND_MIPI_DBI_P_RGB332       0x10
/** R[3-0]G[3-0] - B[3-0]R'[3-0] - G'[3-0]B'[3-0] */
#define SMARTBOND_MIPI_DBI_P_RGB444       0x11
/** R[4-0]G[5-3] - G[2-0]B[4-0] */
#define SMARTBOND_MIPI_DBI_P_RGB565       0x12
/** R[5-0]00 - G[5-0]00 - B[5-0]00 */
#define SMARTBOND_MIPI_DBI_P_RGB666       0x13
/** R[7-0] - G[7-0] - B[7-0] */
#define SMARTBOND_MIPI_DBI_P_RGB888       0x14

/* Layer (input) color formats supported by the MIPI DBI host */

/** R[4-0]G[4-0]B[4-0]A0 */
#define SMARTBOND_MIPI_DBI_L0_RGBA5551  0x01
/** R[7-0]G[7-0]B[7-0]A[7-0] */
#define SMARTBOND_MIPI_DBI_L0_RGBA8888  0x02
/** R[2-0]G[2-0]B[1-0] */
#define SMARTBOND_MIPI_DBI_L0_RGB332    0x04
/** R[4-0]G[5-0]B[4-0] */
#define SMARTBOND_MIPI_DBI_L0_RGB565    0x05
/** A[7-0]R[7-0]G[7-0]B[7-0] */
#define SMARTBOND_MIPI_DBI_L0_ARGB8888  0x06
/** L[7-0] - Grayscale */
#define SMARTBOND_MIPI_DBI_L0_L8        0x07
/** L - Black and white, 1 = black, 0 = white */
#define SMARTBOND_MIPI_DBI_L0_L1        0x08
/** L[3-0] - Grayscale */
#define SMARTBOND_MIPI_DBI_L0_L4        0x09
/** A[7-0]B[7-0]G[7-0]R[7-0] */
#define SMARTBOND_MIPI_DBI_L0_ABGR8888  0x0D
/** B[7-0]G[7-0]R[7-0]A[7-0] */
#define SMARTBOND_MIPI_DBI_L0_BGRA8888  0x0E

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_SMARTBOND_MIPI_DBI_H_ */
