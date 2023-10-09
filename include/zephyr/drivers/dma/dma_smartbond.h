/*
 * Copyright (c) 2023 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef DMA_SMARTBOND_H_
#define DMA_SMARTBOND_H_

/*
 * DMA channel priority level. The smaller the value the lower the priority granted to a channel
 * when two or more channels request the bus at the same time. For channels of same priority an
 * inherent mechanism is applied in which the lower the channel number the higher the priority.
 */
enum dma_smartbond_channel_prio {
	DMA_SMARTBOND_CHANNEL_PRIO_0 = 0x0,  /* Lowest channel priority */
	DMA_SMARTBOND_CHANNEL_PRIO_1,
	DMA_SMARTBOND_CHANNEL_PRIO_2,
	DMA_SMARTBOND_CHANNEL_PRIO_3,
	DMA_SMARTBOND_CHANNEL_PRIO_4,
	DMA_SMARTBOND_CHANNEL_PRIO_5,
	DMA_SMARTBOND_CHANNEL_PRIO_6,
	DMA_SMARTBOND_CHANNEL_PRIO_7,         /* Highest channel priority */
	DMA_SMARTBOND_CHANNEL_PRIO_MAX
};

enum dma_smartbond_channel {
	DMA_SMARTBOND_CHANNEL_0 = 0x0,
	DMA_SMARTBOND_CHANNEL_1,
	DMA_SMARTBOND_CHANNEL_2,
	DMA_SMARTBOND_CHANNEL_3,
	DMA_SMARTBOND_CHANNEL_4,
	DMA_SMARTBOND_CHANNEL_5,
	DMA_SMARTBOND_CHANNEL_6,
	DMA_SMARTBOND_CHANNEL_7,
	DMA_SMARTBOND_CHANNEL_MAX
};

enum dma_smartbond_burst_len {
	DMA_SMARTBOND_BURST_LEN_1B  = 0x1, /* Burst mode is disabled */
	DMA_SMARTBOND_BURST_LEN_4B  = 0x4, /* Perform bursts of 4 beats (INCR4) */
	DMA_SMARTBOND_BURST_LEN_8B  = 0x8  /* Perform bursts of 8 beats (INCR8) */
};

/*
 * DMA bus width indicating how many bytes are retrived/written per transfer.
 * Note that the bus width is the same for the source and destination.
 */
enum dma_smartbond_bus_width {
	DMA_SMARTBOND_BUS_WIDTH_1B = 0x1,
	DMA_SMARTBOND_BUS_WIDTH_2B = 0x2,
	DMA_SMARTBOND_BUS_WIDTH_4B = 0x4
};

enum dma_smartbond_trig_mux {
	DMA_SMARTBOND_TRIG_MUX_SPI   = 0x0,
	DMA_SMARTBOND_TRIG_MUX_SPI2  = 0x1,
	DMA_SMARTBOND_TRIG_MUX_UART  = 0x2,
	DMA_SMARTBOND_TRIG_MUX_UART2 = 0x3,
	DMA_SMARTBOND_TRIG_MUX_I2C   = 0x4,
	DMA_SMARTBOND_TRIG_MUX_I2C2  = 0x5,
	DMA_SMARTBOND_TRIG_MUX_USB   = 0x6,
	DMA_SMARTBOND_TRIG_MUX_UART3 = 0x7,
	DMA_SMARTBOND_TRIG_MUX_PCM   = 0x8,
	DMA_SMARTBOND_TRIG_MUX_SRC   = 0x9,
	DMA_SMARTBOND_TRIG_MUX_GPADC = 0xC,
	DMA_SMARTBOND_TRIG_MUX_SDADC = 0xD,
	DMA_SMARTBOND_TRIG_MUX_NONE  = 0xF
};

#endif /* DMA_SMARTBOND_H_ */
