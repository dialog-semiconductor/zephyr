/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_rrh62000

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/sensor/rrh62000.h>

LOG_MODULE_REGISTER(RRH62000, CONFIG_SENSOR_LOG_LEVEL);

#define RRH62000_START_BYTE1 0xA1
#define RRH62000_START_BYTE2 0x4D
#define RRH62000_COMMAND_LENGTH 7
#define RRH62000_BUFFER_LENGTH 38

#define RRH62000_MAX_RESPONSE_DELAY 150 /* Add margin to the specified 50 in datasheet */
#define RRH62000_CO2_VALID_DELAY    1200

struct rrh62000_data {
	struct k_mutex uart_mutex;
	struct k_sem uart_rx_sem;
	uint8_t read_index;
	uint8_t xfer_bytes;
	uint8_t read_buffer[RRH62000_BUFFER_LENGTH];
	uint16_t NC_0_3_sample;
	uint16_t NC_0_5_sample;
	uint16_t NC_1_sample;
	uint16_t NC_2_5_sample;
	uint16_t NC_4_sample;
	uint16_t PM1_1_sample;
	uint16_t PM2_5_1_sample;
	uint16_t PM10_1_sample;
	uint16_t PM1_2_sample;
	uint16_t PM2_5_2_sample;
	uint16_t PM10_2_sample;
	int16_t AMBIENT_TEMP_sample;
	uint16_t HUMIDITY_sample;
	uint16_t TVOC_sample;
	uint16_t ECO2_sample;
	uint16_t IAQ_sample;
	uint16_t RELIAQ_sample;
};

struct rrh62000_cfg {
	const struct device *uart_dev;
	uart_irq_callback_user_data_t cb;
};

static void rrh62000_uart_flush(const struct device *uart_dev)
{
	uint8_t tmp;

	while (uart_fifo_read(uart_dev, &tmp, 1) > 0) {
		LOG_ERR("flush: %d", tmp);
		continue;
	}
}

static void rrh62000_buffer_reset(struct rrh62000_data *data)
{
	memset(data->read_buffer, 0, data->read_index);
	data->read_index = 0;
}

static void rrh62000_uart_isr(const struct device *uart_dev, void *user_data)
{
	const struct device *dev = user_data;
	struct rrh62000_data *data = dev->data;
	int rc, read_len;

	if (!device_is_ready(uart_dev)) {
		LOG_DBG("UART device is not ready");
		return;
	}

	if (!uart_irq_update(uart_dev)) {
		LOG_DBG("Unable to process interrupts");
		return;
	}

	if (!uart_irq_rx_ready(uart_dev)) {
		LOG_DBG("No RX data");
		return;
	}

	read_len = RRH62000_BUFFER_LENGTH;
	rc = uart_fifo_read(uart_dev, &data->read_buffer[data->read_index], read_len);

	if (rc < 0 || rc == read_len) {
		LOG_ERR("UART read failed: %d", rc < 0 ? rc : -ERANGE);
		rrh62000_uart_flush(uart_dev);
		LOG_HEXDUMP_WRN(data->read_buffer, data->read_index, "Discarding");
		rrh62000_buffer_reset(data);
	} else {
		data->read_index += rc;
		if(data->read_index == RRH62000_BUFFER_LENGTH){
			k_sem_give(&data->uart_rx_sem);
		}
	}

}

static int rrh62000_await_receive(struct rrh62000_data *data)
{
	int rc = k_sem_take(&data->uart_rx_sem, K_MSEC(RRH62000_MAX_RESPONSE_DELAY));
	
	/* Reset semaphore if sensor did not respond within maximum specified response time */
	if (rc == -EAGAIN) {
		k_sem_reset(&data->uart_rx_sem);
	}

	return rc;
}

static int rrh62000_uart_transceive(const struct device *dev, uint8_t *command_data, size_t data_size)
{
	const struct rrh62000_cfg *cfg = dev->config;
	struct rrh62000_data *data = dev->data;
	int rc;
	uint8_t UART_packet[RRH62000_COMMAND_LENGTH];
	uint16_t checksum = 0;
	UART_packet[0] = RRH62000_START_BYTE1;
	UART_packet[1] = RRH62000_START_BYTE2;
	UART_packet[2] = command_data[0];
	UART_packet[3] = command_data[1];
	UART_packet[4] = command_data[2];
	for(int i = 0; i < RRH62000_COMMAND_LENGTH-2; i++){
		checksum += UART_packet[i];
	}

	UART_packet[5] = (checksum & 0xFF00) >>8;
	UART_packet[6] = checksum & 0xFF;

	k_mutex_lock(&data->uart_mutex, K_FOREVER);

	rrh62000_buffer_reset(data);

	for (int i = 0; i != RRH62000_COMMAND_LENGTH; i++) {
		uart_poll_out(cfg->uart_dev, UART_packet[i]);
	}

	rc = rrh62000_await_receive(data);
	if (rc != 0) {
		LOG_WRN("UART did not receive a response: %d", rc);
	}

	k_mutex_unlock(&data->uart_mutex);

	return rc;
}

static int rrh62000_read_sample(const struct device *dev, uint16_t *NC_0_3_sample, uint16_t *NC_0_5_sample, uint16_t *NC_1_sample, uint16_t *NC_2_5_sample, uint16_t *NC_4_sample, uint16_t *PM1_1_sample, uint16_t *PM2_5_1_sample, uint16_t *PM10_1_sample, uint16_t *PM1_2_sample, uint16_t *PM2_5_2_sample, uint16_t *PM10_2_sample, int16_t *AMBIENT_TEMP_sample, uint16_t *HUMIDITY_sample, uint16_t *TVOC_sample, uint16_t *ECO2_sample, uint16_t *IAQ_sample, uint16_t *RELIAQ_sample)
{
	struct rrh62000_data *data = dev->data;
	uint8_t status;
	
	status = sys_get_be16(&data->read_buffer[2]);
	*NC_0_3_sample = sys_get_be16(&data->read_buffer[4]);
	*NC_0_5_sample = sys_get_be16(&data->read_buffer[6]);
	*NC_1_sample = sys_get_be16(&data->read_buffer[8]);
	*NC_2_5_sample = sys_get_be16(&data->read_buffer[10]);
	*NC_4_sample = sys_get_be16(&data->read_buffer[12]);
	*PM1_1_sample = sys_get_be16(&data->read_buffer[14]);
	*PM2_5_1_sample = sys_get_be16(&data->read_buffer[16]);
	*PM10_1_sample = sys_get_be16(&data->read_buffer[18]);
	*PM1_2_sample = sys_get_be16(&data->read_buffer[20]);
	*PM2_5_2_sample = sys_get_be16(&data->read_buffer[22]);
	*PM10_2_sample = sys_get_be16(&data->read_buffer[24]);
	*AMBIENT_TEMP_sample = sys_get_be16(&data->read_buffer[26]);
	*HUMIDITY_sample = sys_get_be16(&data->read_buffer[28]);
	*TVOC_sample = sys_get_be16(&data->read_buffer[30]);
	*ECO2_sample = sys_get_be16(&data->read_buffer[32]);
	*IAQ_sample = sys_get_be16(&data->read_buffer[34]);
	*RELIAQ_sample = sys_get_be16(&data->read_buffer[36]);

	if( status != 0x00 ){
		LOG_ERR("Status error.");
	}

	return 0;
}

static int rrh62000_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	struct rrh62000_data *data = dev->data;
	enum sensor_channel_rrh62000 rrh62000_chan = (enum sensor_channel_rrh62000)chan;
	int rc;
	uint8_t get_measurement_results = 0xE2;
	uint8_t DATAH = 0x00;
	uint8_t DATAL = 0x00;
	uint8_t fetch_sample[3] = {0};

	if (chan != SENSOR_CHAN_ALL && rrh62000_chan != SENSOR_CHAN_NC_0_3 &&
		rrh62000_chan != SENSOR_CHAN_NC_0_5 &&
		rrh62000_chan != SENSOR_CHAN_NC_1 && rrh62000_chan != SENSOR_CHAN_NC_2_5 &&
		rrh62000_chan != SENSOR_CHAN_NC_4 && rrh62000_chan != SENSOR_CHAN_PM1_1 &&
		rrh62000_chan != SENSOR_CHAN_PM2_5_1 && rrh62000_chan != SENSOR_CHAN_PM10_1 &&
		rrh62000_chan != SENSOR_CHAN_PM1_2 && rrh62000_chan != SENSOR_CHAN_PM2_5_2 &&
		rrh62000_chan != SENSOR_CHAN_PM10_2 && chan != SENSOR_CHAN_AMBIENT_TEMP &&
		chan != SENSOR_CHAN_HUMIDITY && rrh62000_chan != SENSOR_CHAN_TVOC &&
		rrh62000_chan != SENSOR_CHAN_ECO2 && rrh62000_chan != SENSOR_CHAN_IAQ &&
		rrh62000_chan != SENSOR_CHAN_RELIAQ) {
		return -ENOTSUP;
	} 

	fetch_sample[0] = get_measurement_results;
	fetch_sample[1] = DATAH;
	fetch_sample[2] = DATAL;

	rc = rrh62000_uart_transceive(dev, fetch_sample, sizeof(fetch_sample));
	if (rc < 0) {
		LOG_ERR("Failed to send fetch.");
		return rc;
	}
	
	rc = rrh62000_read_sample(dev, &data->NC_0_3_sample, &data->NC_0_5_sample, &data->NC_1_sample, &data->NC_2_5_sample, &data->NC_4_sample, &data->PM1_1_sample, &data->PM2_5_1_sample, &data->PM10_1_sample, &data->PM1_2_sample, &data->PM2_5_2_sample, &data->PM10_2_sample, &data->AMBIENT_TEMP_sample, &data->HUMIDITY_sample, &data->TVOC_sample, &data->ECO2_sample, &data->IAQ_sample, &data->RELIAQ_sample);
	if (rc < 0) {
		LOG_ERR("Failed to fetch data.");
		return rc;
	}

	return 0;
}

static int rrh62000_channel_get(const struct device *dev, enum sensor_channel chan,
			      struct sensor_value *val)
{
	const struct rrh62000_data *data = dev->data;
	int32_t convert_val;

	switch(chan){
	case SENSOR_CHAN_NC_0_3:
		convert_val = ((int32_t)data->NC_0_3_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_NC_0_5:
		convert_val = ((int32_t)data->NC_0_5_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_NC_1:
		convert_val = ((int32_t)data->NC_1_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_NC_2_5:
		convert_val = ((int32_t)data->NC_2_5_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_NC_4:
		convert_val = ((int32_t)data->NC_4_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_PM1_1:
		convert_val = ((int32_t)data->PM1_1_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_PM2_5_1:
		convert_val = ((int32_t)data->PM2_5_1_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_PM10_1:
		convert_val = ((int32_t)data->PM10_1_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_PM1_2:
		convert_val = ((int32_t)data->PM1_2_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_PM2_5_2:
		convert_val = ((int32_t)data->PM2_5_2_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_PM10_2:
		convert_val = ((int32_t)data->PM10_2_sample) * 100000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_AMBIENT_TEMP:
		convert_val = ((int32_t)data->AMBIENT_TEMP_sample) * 10000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_HUMIDITY:
		convert_val = ((int32_t)data->HUMIDITY_sample) * 10000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_TVOC:
		convert_val = ((int32_t)data->TVOC_sample) * 10000000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_ECO2:
		convert_val = ((int32_t)data->ECO2_sample) * 1000000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_IAQ:
		convert_val = ((int32_t)data->IAQ_sample) * 10000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	case SENSOR_CHAN_RELIAQ:
		convert_val = ((int32_t)data->RELIAQ_sample) * 10000;
 		val->val1 = convert_val / 1000000;
 		val->val2 = convert_val % 1000000;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int rrh62000_init(const struct device *dev)
{
	const struct rrh62000_cfg *cfg = dev->config;
	struct rrh62000_data *data = dev->data;
	int rc;

	LOG_DBG("Initializing %s", dev->name);

	if (!device_is_ready(cfg->uart_dev)) {
		return -ENODEV;
	}

	k_mutex_init(&data->uart_mutex);
	k_sem_init(&data->uart_rx_sem, 0, 1);

	uart_irq_rx_disable(cfg->uart_dev);
	uart_irq_tx_disable(cfg->uart_dev);

	rc = uart_irq_callback_user_data_set(cfg->uart_dev, cfg->cb, (void *)dev);
	if (rc != 0) {
		LOG_ERR("UART IRQ setup failed: %d", rc);
		return rc;
	}

	uart_irq_rx_enable(cfg->uart_dev);

	return rc;
}

static DEVICE_API(sensor, rrh62000_driver_api) = {
		.sample_fetch = rrh62000_sample_fetch,
		.channel_get = rrh62000_channel_get,
};

#define DEFINE_RRH62000(n)                                                                           \
	static struct rrh62000_data rrh62000_data_##n;                                                 \
                                                                                                   \
	static const struct rrh62000_cfg rrh62000_cfg_##n = { \
	.uart_dev = DEVICE_DT_GET(DT_INST_BUS(n)), \
	.cb = rrh62000_uart_isr, \
	};    \
                                                                                                   \
	SENSOR_DEVICE_DT_INST_DEFINE(n, rrh62000_init, NULL, &rrh62000_data_##n, &rrh62000_cfg_##n,   \
				     POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,                     \
				     &rrh62000_driver_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_RRH62000)