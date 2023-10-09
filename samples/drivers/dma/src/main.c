/*
 * Copyright (c) 2023 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Sample to illustrate the usage of DMA APIs. */
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_smartbond.h>

#define LOG_LEVEL CONFIG_DMA_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#if DT_HAS_COMPAT_STATUS_OKAY(renesas_smartbond_dma)
#define DMA_DEV_COMPAT renesas_smartbond_dma
#else
#error "You need to enable one dma device"
#endif

/* in millisecond */
#define SLEEPTIME 250

#define TRANSFER_LOOPS (4)

#define CONFIG_DMA_LOOP_TRANSFER_SIZE   8192
#define CONFIG_DMA_LOOP_TRANSFER_CHANNEL_NR 0

#if CONFIG_NOCACHE_MEMORY
static __aligned(32) uint8_t tx_data[CONFIG_DMA_LOOP_TRANSFER_SIZE] __used
	__attribute__((__section__(CONFIG_DMA_LOOP_TRANSFER_SRAM_SECTION)));
static __aligned(32) uint8_t rx_data[TRANSFER_LOOPS][CONFIG_DMA_LOOP_TRANSFER_SIZE] __used
	__attribute__((__section__(CONFIG_DMA_LOOP_TRANSFER_SRAM_SECTION".dma")));
#else
/* this src memory shall be in RAM to support usingas a DMA source pointer.*/
static uint8_t tx_data[CONFIG_DMA_LOOP_TRANSFER_SIZE];
static __aligned(16) uint8_t rx_data[TRANSFER_LOOPS][CONFIG_DMA_LOOP_TRANSFER_SIZE] = { { 0 } };
#endif

volatile uint32_t transfer_count, transfer_count_circular;
volatile uint32_t done;
static struct dma_config dma_cfg = {0};
static struct dma_block_config dma_block_cfg = {0};
static int test_case_id;

static void test_transfer(const struct device *dev, uint32_t id)
{
	transfer_count++;
	if (transfer_count < TRANSFER_LOOPS) {
		dma_block_cfg.block_size = sizeof(tx_data);
#ifdef CONFIG_DMA_64BIT
		dma_block_cfg.source_address = (uint64_t)tx_data;
		dma_block_cfg.dest_address = (uint64_t)rx_data[transfer_count];
#else
		dma_block_cfg.source_address = (uint32_t)tx_data;
		dma_block_cfg.dest_address = (uint32_t)rx_data[transfer_count];
#endif

		if (dma_config(dev, id, &dma_cfg) < 0) {
			LOG_ERR("Not able to config transfer\n");
			return;
		}
		if (dma_start(dev, id) < 0) {
			LOG_ERR("Not able to start next transfer\n");
			return;
		}
	}
}

static void dma_user_callback(const struct device *dma_dev, void *arg,
			      uint32_t id, int status)
{
	/* test case is done so ignore the interrupt */
	if (done) {
		return;
	}

	if (status < 0) {
		LOG_ERR("DMA could not proceed, an error occurred\n");
	}
	test_transfer(dma_dev, id);
}

static void dma_cyclic_user_callback(const struct device *dma_dev, void *arg,
			      uint32_t id, int status)
{
	transfer_count_circular++;
	/* test case is done so ignore the interrupt */
	if (done) {
		return;
	}

	if (status < 0) {
		LOG_ERR("DMA could not proceed, an error occurred\n");
	}
}


static void test_loop(const struct device *dma)
{
	static int chan_id;
	uint32_t attribute = 0;

	test_case_id = 0;
	LOG_INF("DMA memory to memory transfer started\n");

	memset(tx_data, 0, sizeof(tx_data));

	for (int i = 0; i < CONFIG_DMA_LOOP_TRANSFER_SIZE; i++) {
		tx_data[i] = i;
	}

	memset(rx_data, 0, sizeof(rx_data));

	if (!device_is_ready(dma)) {
		LOG_ERR("dma controller device is not ready\n");
		return;
	}

	LOG_INF("Preparing DMA Controller: %s\n", dma->name);
	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = dma_cfg.dest_data_size = DMA_SMARTBOND_BUS_WIDTH_1B;
	dma_cfg.source_burst_length = dma_cfg.dest_burst_length = DMA_SMARTBOND_BURST_LEN_1B;
	dma_cfg.user_data = NULL;
	dma_cfg.dma_callback = dma_user_callback;

	if (dma_get_attribute(dma, DMA_ATTR_MAX_BLOCK_COUNT, &attribute)) {
		LOG_ERR("Requested attribute is not supported\n");
		return;
	} else {
		LOG_INF("Max block count is %d\n", attribute);
	}

	dma_cfg.block_count = attribute;
	dma_cfg.head_block = &dma_block_cfg;
	dma_cfg.source_handshake = dma_cfg.dest_handshake = 1; // SW

	chan_id = dma_request_channel(dma, NULL);
	if (chan_id < 0) {
		LOG_WRN("this platform do not support the dma channel\n");
		chan_id = CONFIG_DMA_LOOP_TRANSFER_CHANNEL_NR;
	}
	transfer_count = 0;
	done = 0;
	LOG_INF("Starting the transfer on channel %d and waiting for 1 second\n", chan_id);
	dma_block_cfg.block_size = sizeof(tx_data) - 1;
#ifdef CONFIG_DMA_64BIT
	dma_block_cfg.source_address = (uint64_t)tx_data;
	dma_block_cfg.dest_address = (uint64_t)rx_data[transfer_count];
#else
	dma_block_cfg.source_address = (uint32_t)tx_data;
	dma_block_cfg.dest_address = (uint32_t)rx_data[transfer_count];
#endif
	dma_block_cfg.source_addr_adj = dma_block_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	bool is_buffer_misalignment_valid = true;

	/* Define a burst that is not multiple of data size to trigger buffer size misalignment */
	if (dma_block_cfg.block_size % (dma_cfg.source_data_size * DMA_SMARTBOND_BURST_LEN_8B)) {
		dma_cfg.source_burst_length = dma_cfg.dest_burst_length = DMA_SMARTBOND_BURST_LEN_8B;
	} else if (dma_block_cfg.block_size % (dma_cfg.source_data_size * DMA_SMARTBOND_BURST_LEN_4B)) {
		dma_cfg.source_burst_length = dma_cfg.dest_burst_length = DMA_SMARTBOND_BURST_LEN_4B;
	} else {
		LOG_WRN("Triggering bugger size misalignment is not possible!\n");
		is_buffer_misalignment_valid = false;
	}

	if (dma_config(dma, chan_id, &dma_cfg)) {
		if (!is_buffer_misalignment_valid) {
			LOG_ERR("ERROR: transfer config (%d)\n", chan_id);
			return;
		} else {
			LOG_INF("Configuring channel (%d) failed successfully\n", chan_id);
			dma_cfg.source_burst_length = dma_cfg.dest_burst_length = DMA_SMARTBOND_BURST_LEN_1B;
		}
	} else if (is_buffer_misalignment_valid) {
		LOG_ERR("Configuring channel (%d) succeeded though it should not\n", chan_id);
		return;
	}

	/* Burst length should have been fixed. Reconfigure the selected channel. */
	if (is_buffer_misalignment_valid) {
		if (dma_config(dma, chan_id, &dma_cfg)) {
			LOG_ERR("ERROR: transfer config (%d)\n", chan_id);
			return;
		}
	}

	if (dma_start(dma, chan_id)) {
		LOG_ERR("ERROR: transfer start (%d)\n", chan_id);
		return;
	}

	k_sleep(K_MSEC(SLEEPTIME));

	if (transfer_count < TRANSFER_LOOPS) {
		transfer_count = TRANSFER_LOOPS;
		LOG_ERR("ERROR: unfinished transfer\n");
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}

	LOG_INF("Each RX buffer should contain the full TX buffer string.\n");

	for (int i = 0; i < TRANSFER_LOOPS; i++) {
		LOG_INF("RX data Loop %d\n", i);
		if (memcmp(tx_data, rx_data[i], CONFIG_DMA_LOOP_TRANSFER_SIZE - 1)) {
			LOG_ERR("TX RX buffer mismatch\n");
			return;
		}
	}

	LOG_INF("Finished DMA: %s\n", dma->name);
	dma_stop(dma, chan_id);
	return;
}

static void test_loop_suspend_resume(const struct device *dma)
{
	static int chan_id;
	int res = 0;
	uint32_t attribute = 0;

	test_case_id = 1;
	LOG_INF("DMA memory to memory transfer started\n");

	memset(tx_data, 0, sizeof(tx_data));

	for (int i = 0; i < CONFIG_DMA_LOOP_TRANSFER_SIZE; i++) {
		tx_data[i] = i;
	}

	memset(rx_data, 0, sizeof(rx_data));

	if (!device_is_ready(dma)) {
		LOG_ERR("dma controller device is not ready\n");
		return;
	}

	LOG_INF("Preparing DMA Controller: %s\n", dma->name);
	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = dma_cfg.dest_data_size = DMA_SMARTBOND_BUS_WIDTH_1B;
	dma_cfg.source_burst_length = dma_cfg.dest_burst_length = DMA_SMARTBOND_BURST_LEN_1B;
	dma_cfg.user_data = NULL;
	dma_cfg.dma_callback = dma_user_callback;

	if (dma_get_attribute(dma, DMA_ATTR_MAX_BLOCK_COUNT, &attribute)) {
		LOG_ERR("Requested attribute is not supported\n");
		return;
	} else {
		LOG_INF("Max block count is %d\n", attribute);
	}

	dma_cfg.block_count = attribute;
	dma_cfg.head_block = &dma_block_cfg;
	dma_cfg.source_handshake = dma_cfg.dest_handshake = 1; // SW

	chan_id = dma_request_channel(dma, NULL);
	if (chan_id < 0) {
		LOG_WRN("this platform do not support the dma channel\n");
		chan_id = CONFIG_DMA_LOOP_TRANSFER_CHANNEL_NR;
	}
	transfer_count = 0;
	done = 0;
	LOG_INF("Starting the transfer on channel %d and waiting for 1 second\n", chan_id);
	dma_block_cfg.block_size = sizeof(tx_data);
#ifdef CONFIG_DMA_64BIT
	dma_block_cfg.source_address = (uint64_t)tx_data;
	dma_block_cfg.dest_address = (uint64_t)rx_data[transfer_count];
#else
	dma_block_cfg.source_address = (uint32_t)tx_data;
	dma_block_cfg.dest_address = (uint32_t)rx_data[transfer_count];
#endif
	dma_block_cfg.source_addr_adj = dma_block_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	unsigned int irq_key;

	if (dma_config(dma, chan_id, &dma_cfg)) {
		LOG_ERR("ERROR: transfer config (%d)\n", chan_id);
		return;
	}

	if (dma_start(dma, chan_id)) {
		LOG_ERR("ERROR: transfer start (%d)\n", chan_id);
		return;
	}

	/* Try multiple times to suspend the transfers */
	uint32_t tc = transfer_count;

	do {
		irq_key = irq_lock();
		res = dma_suspend(dma, chan_id);
		if (res == -ENOSYS) {
			done = 1;
			LOG_INF("suspend not supported\n");
			dma_stop(dma, chan_id);
			return;
		}
		tc = transfer_count;
		irq_unlock(irq_key);
		k_busy_wait(100);
	} while (tc != transfer_count);

	/* If we failed to suspend we failed */
	if (transfer_count == TRANSFER_LOOPS) {
		LOG_ERR("ERROR: failed to suspend transfers\n");
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}
	LOG_INF("suspended after %d transfers occurred\n", transfer_count);

	/* Now sleep */
	k_sleep(K_MSEC(SLEEPTIME));

	/* If we failed to suspend we failed */
	if (transfer_count == TRANSFER_LOOPS) {
		LOG_ERR("ERROR: failed to suspend transfers\n");
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}
	LOG_INF("resuming after %d transfers occurred\n", transfer_count);

	struct dma_status status;
	if (dma_get_status(dma, chan_id, &status)) {
		LOG_WRN("Cannot get DMA channel status\n");
	} else {
		LOG_INF("Busy status: %d", status.busy);
		LOG_INF("Direction: %d", status.dir);
		LOG_INF("Total copied: %d", (uint32_t)status.total_copied);
		LOG_INF("Pending length: %d\n", status.pending_length);
	}

	res = dma_resume(dma, chan_id);
	LOG_INF("Resumed transfers\n");
	if (res != 0) {
		LOG_ERR("ERROR: resume failed, channel %d, result %d\n", chan_id, res);
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}

	k_sleep(K_MSEC(SLEEPTIME));

	LOG_INF("Transfer count %d\n", transfer_count);
	if (transfer_count < TRANSFER_LOOPS) {
		transfer_count = TRANSFER_LOOPS;
		LOG_ERR("ERROR: unfinished transfer\n");
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}

	LOG_INF("Each RX buffer should contain the full TX buffer string.\n");

	for (int i = 0; i < TRANSFER_LOOPS; i++) {
		LOG_INF("RX data Loop %d\n", i);
		if (memcmp(tx_data, rx_data[i], CONFIG_DMA_LOOP_TRANSFER_SIZE)) {
			LOG_ERR("TX RX buffer mismatch");
			return;
		}
	}

	LOG_INF("Finished DMA: %s\n", dma->name);
	dma_stop(dma, chan_id); // TODO: Should check that NVIC is disabled for DMA
	dma_release_channel(dma, chan_id);
	return;
}

static void test_loop_repeated_start_stop(const struct device *dma)
{
	static int chan_id;
	uint32_t attribute;

	test_case_id = 0;
	LOG_INF("DMA memory to memory transfer started\n");
	LOG_INF("Preparing DMA Controller\n");

	memset(tx_data, 0, sizeof(tx_data));

	memset(rx_data, 0, sizeof(rx_data));

	if (!device_is_ready(dma)) {
		LOG_ERR("dma controller device is not ready\n");
		return;
	}

	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = dma_cfg.dest_data_size = DMA_SMARTBOND_BUS_WIDTH_1B;
	dma_cfg.source_burst_length = dma_cfg.dest_burst_length = DMA_SMARTBOND_BURST_LEN_1B;
	dma_cfg.user_data = NULL;
	dma_cfg.dma_callback = dma_user_callback;

	if (dma_get_attribute(dma, DMA_ATTR_MAX_BLOCK_COUNT, &attribute)) {
		LOG_ERR("Requested attribute is not supported\n");
		return;
	} else {
		LOG_INF("Max block count is %d\n", attribute);
	}

	dma_cfg.block_count = attribute;
	dma_cfg.head_block = &dma_block_cfg;
	dma_cfg.source_handshake = dma_cfg.dest_handshake = 1; // SW

	/* Request a specific channel index */
	uint32_t requested_idx = DMA_SMARTBOND_CHANNEL_5;
	chan_id = dma_request_channel(dma, &requested_idx);
	if (chan_id < 0) {
		LOG_WRN("this platform do not support the dma channel\n");
		chan_id = CONFIG_DMA_LOOP_TRANSFER_CHANNEL_NR;
	}
	transfer_count = 0;
	done = 0;
	LOG_INF("Starting the transfer on channel %d and waiting for 1 second\n", chan_id);
	dma_block_cfg.block_size = sizeof(tx_data);
#ifdef CONFIG_DMA_64BIT
	dma_block_cfg.source_address = (uint64_t)tx_data;
	dma_block_cfg.dest_address = (uint64_t)rx_data[transfer_count];
#else
	dma_block_cfg.source_address = (uint32_t)tx_data;
	dma_block_cfg.dest_address = (uint32_t)rx_data[transfer_count];
#endif
	dma_block_cfg.source_addr_adj = dma_block_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	/* Start the channel before configuration takes place. We should expect that this will fail */
	if (dma_start(dma, chan_id)) {
		LOG_INF("Starting channel (%d) failed correctly as it should first be configured\n", chan_id);
	} else {
		LOG_ERR("Channel (%d) started though it should not!\n", chan_id);
		return;
	}

	if (dma_reload(dma, chan_id, (uint32_t)tx_data, (uint32_t)rx_data[transfer_count], sizeof(tx_data))) {
		LOG_INF("Reloading channel (%d) failed correctly as it should first be configured\n", chan_id);
	} else {
		LOG_ERR("Channel (%d) reloaded though it should not!\n", chan_id);
		return;
	}

	if (dma_config(dma, chan_id, &dma_cfg)) {
		LOG_ERR("ERROR: transfer config (%d)\n", chan_id);
		return;
	}

	if (dma_reload(dma, chan_id, (uint32_t)tx_data, (uint32_t)rx_data[transfer_count], sizeof(tx_data))) {
		LOG_ERR("Channel (%d) failred to be reloaded\n", chan_id);
		return;
	}

	if (dma_stop(dma, chan_id)) {
		LOG_ERR("ERROR: transfer stop on stopped channel (%d)\n", chan_id);
		return;
	}

	if (dma_start(dma, chan_id)) {
		LOG_ERR("ERROR: transfer start (%d)\n", chan_id);
		return;
	}

	k_sleep(K_MSEC(SLEEPTIME));

	if (transfer_count < TRANSFER_LOOPS) {
		transfer_count = TRANSFER_LOOPS;
		LOG_ERR("ERROR: unfinished transfer\n");
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}

	LOG_INF("Each RX buffer should contain the full TX buffer string.\n");

	for (int i = 0; i < TRANSFER_LOOPS; i++) {
		LOG_INF("RX data Loop %d\n", i);
		if (memcmp(tx_data, rx_data[i], CONFIG_DMA_LOOP_TRANSFER_SIZE)) {
			LOG_ERR("RX/TX data mismatch\n");
			return;
		}
	}

	LOG_INF("Finished: DMA\n");

	if (dma_stop(dma, chan_id)) {
		LOG_ERR("ERROR: transfer stop (%d)\n", chan_id);
		return;
	}

	if (dma_stop(dma, chan_id)) {
		LOG_ERR("ERROR: repeated transfer stop (%d)\n", chan_id);
		return;
	}

	return;
}

#define DEFAULT_BUF_VAL  0xA0

static void test_memory_init(const struct device *dma)
{
	static int chan_id;
	uint32_t attribute = 0;
	uint8_t init_value = DEFAULT_BUF_VAL;

	test_case_id = 0;
	LOG_INF("DMA memory initialization transfer started\n");

	memset(tx_data, 0, sizeof(tx_data));

	for (int i = 0; i < CONFIG_DMA_LOOP_TRANSFER_SIZE; i++) {
		tx_data[i] = DEFAULT_BUF_VAL;
	}

	memset(rx_data, 0, sizeof(rx_data));

	if (!device_is_ready(dma)) {
		LOG_ERR("dma controller device is not ready\n");
		return;
	}

	LOG_INF("Preparing DMA Controller: %s\n", dma->name);
	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = dma_cfg.dest_data_size = DMA_SMARTBOND_BUS_WIDTH_1B;
	dma_cfg.source_burst_length = dma_cfg.dest_burst_length = DMA_SMARTBOND_BURST_LEN_1B;
	dma_cfg.user_data = NULL;
	dma_cfg.dma_callback = dma_user_callback;

	if (dma_get_attribute(dma, DMA_ATTR_MAX_BLOCK_COUNT, &attribute)) {
		LOG_ERR("Requested attribute is not supported\n");
		return;
	} else {
		LOG_INF("Max block count is %d\n", attribute);
	}

	dma_cfg.block_count = attribute;
	dma_cfg.head_block = &dma_block_cfg;
	dma_cfg.source_handshake = dma_cfg.dest_handshake = 1; // SW

	uint32_t requested_idx = DMA_SMARTBOND_CHANNEL_5;
	chan_id = dma_request_channel(dma, &requested_idx);
	if (chan_id < 0) {
		chan_id = CONFIG_DMA_LOOP_TRANSFER_CHANNEL_NR;
		LOG_INF("Channel (%d) was not granted correctly. Channel (%d) should be used instead.\n",
																		requested_idx, chan_id);
	} else {
		LOG_ERR("Requested channel was granted though it should already be reserved\n");
		return;
	}

	transfer_count = 0;
	done = 0;
	LOG_INF("Starting the transfer on channel %d and waiting for 1 second\n", chan_id);
	dma_block_cfg.block_size = sizeof(tx_data);
#ifdef CONFIG_DMA_64BIT
	dma_block_cfg.source_address = (uint64_t)tx_data;
	dma_block_cfg.dest_address = (uint64_t)rx_data[transfer_count];
#else
	dma_block_cfg.source_address = (uint32_t)&init_value;
	dma_block_cfg.dest_address = (uint32_t)rx_data[transfer_count];
#endif
	dma_block_cfg.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE;
	dma_block_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	if (dma_config(dma, chan_id, &dma_cfg)) {
		LOG_ERR("ERROR: transfer config (%d)\n", chan_id);
		return;
	}

	if (dma_start(dma, chan_id)) {
		LOG_ERR("ERROR: transfer start (%d)\n", chan_id);
		return;
	}

	k_sleep(K_MSEC(SLEEPTIME));

	if (transfer_count < TRANSFER_LOOPS) {
		transfer_count = TRANSFER_LOOPS;
		LOG_ERR("ERROR: unfinished transfer\n");
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}

	LOG_INF("Each RX buffer should contain the full TX buffer string.\n");

	for (int i = 0; i < TRANSFER_LOOPS; i++) {
		LOG_INF("RX data Loop %d\n", i);
		if (memcmp(tx_data, rx_data[i], CONFIG_DMA_LOOP_TRANSFER_SIZE)) {
			LOG_ERR("TX RX buffer mismatch");
			return;
		}
	}

	LOG_INF("Finished DMA: %s\n", dma->name);
	dma_stop(dma, chan_id);
	return;
}

static void test_circular_mode(const struct device *dma)
{
	static int chan_id;
	uint32_t attribute = 0;

	test_case_id = 0;
	LOG_INF("DMA circular transfer started\n");

	memset(tx_data, 0, sizeof(tx_data));

	for (int i = 0; i < CONFIG_DMA_LOOP_TRANSFER_SIZE; i++) {
		tx_data[i] = i;
	}

	memset(rx_data, 0, sizeof(rx_data));

	if (!device_is_ready(dma)) {
		LOG_ERR("dma controller device is not ready\n");
		return;
	}

	LOG_INF("Preparing DMA Controller: %s\n", dma->name);
	dma_cfg.channel_direction = MEMORY_TO_MEMORY;
	dma_cfg.source_data_size = dma_cfg.dest_data_size = DMA_SMARTBOND_BUS_WIDTH_1B;
	dma_cfg.source_burst_length = dma_cfg.dest_burst_length = DMA_SMARTBOND_BURST_LEN_1B;
	dma_cfg.user_data = NULL;
	dma_cfg.dma_callback = dma_cyclic_user_callback;

	if (dma_get_attribute(dma, DMA_ATTR_MAX_BLOCK_COUNT, &attribute)) {
		LOG_ERR("Requested attribute is not supported\n");
		return;
	} else {
		LOG_INF("Max block count is %d\n", attribute);
	}

	dma_cfg.block_count = attribute;
	dma_cfg.head_block = &dma_block_cfg;
	dma_cfg.source_handshake = dma_cfg.dest_handshake = 1; // SW
	dma_cfg.cyclic = true;

	chan_id = dma_request_channel(dma, NULL);
	if (chan_id < 0) {
		LOG_WRN("this platform do not support the dma channel\n");
		chan_id = CONFIG_DMA_LOOP_TRANSFER_CHANNEL_NR;
	}
	transfer_count = transfer_count_circular = 0;
	done = 0;
	LOG_INF("Starting the transfer on channel %d and waiting for 1 second\n", chan_id);
	dma_block_cfg.block_size = sizeof(tx_data) - 1;
#ifdef CONFIG_DMA_64BIT
	dma_block_cfg.source_address = (uint64_t)tx_data;
	dma_block_cfg.dest_address = (uint64_t)rx_data[transfer_count];
#else
	dma_block_cfg.source_address = (uint32_t)tx_data;
	dma_block_cfg.dest_address = (uint32_t)rx_data[transfer_count];
#endif
	dma_block_cfg.source_addr_adj = dma_block_cfg.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT;

	if (dma_config(dma, chan_id, &dma_cfg)) {
		LOG_ERR("ERROR: transfer config (%d)\n", chan_id);
		return;
	}

	if (dma_start(dma, chan_id)) {
		LOG_ERR("ERROR: transfer start (%d)\n", chan_id);
		return;
	}

	k_sleep(K_MSEC(SLEEPTIME));

	if (transfer_count_circular < TRANSFER_LOOPS) {
		transfer_count_circular = TRANSFER_LOOPS;
		LOG_ERR("ERROR: unfinished transfer\n");
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}

	if(dma_suspend(dma, chan_id)) {
		LOG_ERR("Failed to suspend channel (%d)\n", chan_id);
		return;
	}

	uint32_t old_transfer_count_circular = transfer_count_circular;
	LOG_INF("Current cyclic index value (%d)", transfer_count_circular);

	k_sleep(K_MSEC(SLEEPTIME));

	if (old_transfer_count_circular != transfer_count_circular) {
		LOG_ERR("DMA cyclic did not suspend though suspended\n");
		return;
	}

	if (dma_resume(dma, chan_id)) {
		LOG_ERR("Failed to resume channel (%d)\n", chan_id);
		return;
	}

	k_sleep(K_MSEC(SLEEPTIME));

	if (old_transfer_count_circular == transfer_count_circular) {
		LOG_ERR("DMA cyclic did not resumed while instrcuted to do so\n");
		return;
	}
	LOG_INF("Current cyclic index value (%d)", transfer_count_circular);

	if (dma_stop(dma, chan_id)) {
		LOG_ERR("Channel (%d) failed to be terminated\n", chan_id);
		return;
	}

	/* By stopping a DMA channel all of its indexes are being reset. Switch back to normal mode. */
	dma_cfg.cyclic = false;
	dma_cfg.dma_callback = dma_user_callback;

	if (dma_config(dma, chan_id, &dma_cfg)) {
		LOG_ERR("ERROR: transfer config (%d)\n", chan_id);
		return;
	}
	if (dma_start(dma, chan_id)) {
		LOG_ERR("ERROR: transfer start (%d)\n", chan_id);
		return;
	}

	k_sleep(K_MSEC(SLEEPTIME));

	if (transfer_count < TRANSFER_LOOPS) {
		transfer_count = TRANSFER_LOOPS;
		LOG_ERR("ERROR: unfinished transfer\n");
		if (dma_stop(dma, chan_id)) {
			LOG_ERR("ERROR: transfer stop\n");
		}
		return;
	}

	LOG_INF("Each RX buffer should contain the full TX buffer string.\n");

	for (int i = 0; i < TRANSFER_LOOPS; i++) {
		LOG_INF("RX data Loop %d\n", i);
		if (memcmp(tx_data, rx_data[i], CONFIG_DMA_LOOP_TRANSFER_SIZE - 1)) {
			LOG_ERR("TX RX buffer mismatch\n");
			return;
		}
	}

	LOG_INF("Finished DMA: %s\n", dma->name);
	dma_stop(dma, chan_id);
	return;
}

struct mode_test {
	const char *mode;
	void (*mode_func)(const struct device *dev);
};

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(DMA_DEV_COMPAT);

	if (!device_is_ready(dev)) {
		LOG_ERR("\nDMA device is not ready\n");
		return 0;
	}

	const struct mode_test modes[] = {
		{ .mode = "------ MEMORY-MEMORY LOOP TEST -----", .mode_func = test_loop                     },
		{ .mode = "------ SUSPEND/RESUME TEST -----",     .mode_func = test_loop_suspend_resume      },
		{ .mode = "------ START/STOP TEST -----",         .mode_func = test_loop_repeated_start_stop },
		{ .mode = "------ MEMORY INIT TEST -----",        .mode_func = test_memory_init              },
		{ .mode = "------ CIRCULAR MODE TEST -----",      .mode_func = test_circular_mode            },
		{ },
	};
	int i;

	LOG_INF("###### DMA Test Vectors ######\n");

	for (i = 0; modes[i].mode; i++) {
		LOG_INF("%s", modes[i].mode);
		modes[i].mode_func(dev);
	}
	return 0;
}
