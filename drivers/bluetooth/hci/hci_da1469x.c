/* h4.c - H:4 UART based Bluetooth driver */

/*
 * Copyright (c) 2023 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/sys/util.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/drivers/bluetooth/hci_driver.h>
#include <zephyr/irq.h>

#include <common/bt_str.h>

#include <DA1469xAB.h>
#include <da1469x_pdc.h>
#include <mbox.h>
#include <shm.h>

#define H4_NONE 0x00
#define H4_CMD  0x01
#define H4_ACL  0x02
#define H4_EVT  0x04
#define H4_ISO  0x05

static struct {
	struct net_buf *buf;
	struct k_fifo   fifo;

	uint16_t    remaining;
	uint16_t    discard;

	bool     have_hdr;
	bool     discardable;

	uint8_t     hdr_len;

	uint8_t     type;
	union {
	struct bt_hci_evt_hdr evt;
	struct bt_hci_acl_hdr acl;
	struct bt_hci_iso_hdr iso;
	uint8_t hdr[4];
    };
} rx = {
	.fifo = Z_FIFO_INITIALIZER(rx.fifo),
};

#define LOG_LEVEL CONFIG_BT_HCI_DRIVER_LOG_LEVEL
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(hci_da1469x);

static K_KERNEL_STACK_DEFINE(rx_thread_stack, CONFIG_BT_RX_STACK_SIZE);
static struct k_thread rx_thread_data;

static inline void h4_get_type(void)
{
	/* Get packet type */
	if (cmac_mbox_read(&rx.type, 1) != 1) {
		LOG_WRN("Unable to read H:4 packet type");
		rx.type = H4_NONE;
		return;
	}

	switch (rx.type) {
		case H4_EVT:
			rx.remaining = sizeof(rx.evt);
			rx.hdr_len = rx.remaining;
			break;
		case H4_ACL:
			rx.remaining = sizeof(rx.acl);
			rx.hdr_len = rx.remaining;
			break;
		case H4_ISO:
			if (IS_ENABLED(CONFIG_BT_ISO)) {
				rx.remaining = sizeof(rx.iso);
				rx.hdr_len = rx.remaining;
				break;
			}
			__fallthrough;
		default:
			LOG_ERR("Unknown H:4 type 0x%02x", rx.type);
			rx.type = H4_NONE;
	}
}

static void h4_read_hdr(void)
{
	int bytes_read = rx.hdr_len - rx.remaining;
	int ret;

	ret = cmac_mbox_read(rx.hdr + bytes_read, rx.remaining);
	if (unlikely(ret < 0)) {
		LOG_ERR("Unable to read from UART (ret %d)", ret);
	} else {
		rx.remaining -= ret;
	}
}

static inline void get_acl_hdr(void)
{
	h4_read_hdr();

	if (!rx.remaining) {
		struct bt_hci_acl_hdr *hdr = &rx.acl;

		rx.remaining = sys_le16_to_cpu(hdr->len);
		LOG_DBG("Got ACL header. Payload %u bytes", rx.remaining);
		rx.have_hdr = true;
	}
}

static inline void get_iso_hdr(void)
{
	h4_read_hdr();

	if (!rx.remaining) {
		struct bt_hci_iso_hdr *hdr = &rx.iso;

		rx.remaining = bt_iso_hdr_len(sys_le16_to_cpu(hdr->len));
		LOG_DBG("Got ISO header. Payload %u bytes", rx.remaining);
		rx.have_hdr = true;
	}
}

static inline void get_evt_hdr(void)
{
	struct bt_hci_evt_hdr *hdr = &rx.evt;

	h4_read_hdr();

	if (rx.hdr_len == sizeof(*hdr) && rx.remaining < sizeof(*hdr)) {
		switch (rx.evt.evt) {
			case BT_HCI_EVT_LE_META_EVENT:
				rx.remaining++;
				rx.hdr_len++;
				break;
		}
	}

	if (!rx.remaining) {
		if (rx.evt.evt == BT_HCI_EVT_LE_META_EVENT &&
		    (rx.hdr[sizeof(*hdr)] == BT_HCI_EVT_LE_ADVERTISING_REPORT)) {
			LOG_DBG("Marking adv report as discardable");
			rx.discardable = true;
		}

		rx.remaining = hdr->len - (rx.hdr_len - sizeof(*hdr));
		LOG_DBG("Got event header. Payload %u bytes", hdr->len);
		rx.have_hdr = true;
	}
}


static inline void copy_hdr(struct net_buf *buf)
{
	net_buf_add_mem(buf, rx.hdr, rx.hdr_len);
}

static void reset_rx(void)
{
	rx.type = H4_NONE;
	rx.remaining = 0U;
	rx.have_hdr = false;
	rx.hdr_len = 0U;
	rx.discardable = false;
}

static struct net_buf *get_rx(k_timeout_t timeout)
{
	LOG_DBG("type 0x%02x, evt 0x%02x", rx.type, rx.evt.evt);

	switch (rx.type) {
		case H4_EVT:
			return bt_buf_get_evt(rx.evt.evt, rx.discardable, timeout);
		case H4_ACL:
			return bt_buf_get_rx(BT_BUF_ACL_IN, timeout);
		case H4_ISO:
			if (IS_ENABLED(CONFIG_BT_ISO)) {
				return bt_buf_get_rx(BT_BUF_ISO_IN, timeout);
			}
	}

	return NULL;
}

static void rx_thread(void *p1, void *p2, void *p3)
{
	struct net_buf *buf;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_DBG("started");

	while (1) {
		LOG_DBG("rx.buf %p", rx.buf);

		/* We can only do the allocation if we know the initial
		 * header, since Command Complete/Status events must use the
		 * original command buffer (if available).
		 */
		if (rx.have_hdr && !rx.buf) {
			rx.buf = get_rx(K_FOREVER);
			LOG_DBG("Got rx.buf %p", rx.buf);
			if (rx.remaining > net_buf_tailroom(rx.buf)) {
				LOG_ERR("Not enough space in buffer");
				rx.discard = rx.remaining;
				reset_rx();
			} else {
				copy_hdr(rx.buf);
			}
		}

		/* Let the ISR continue receiving new packets */
		irq_enable(CMAC2SYS_IRQn);

		buf = net_buf_get(&rx.fifo, K_FOREVER);
		do {
			irq_enable(CMAC2SYS_IRQn);

			LOG_DBG("Calling bt_recv(%p)", buf);
			bt_recv(buf);

			/* Give other threads a chance to run if the ISR
			 * is receiving data so fast that rx.fifo never
			 * or very rarely goes empty.
			 */
			k_yield();

			irq_disable(CMAC2SYS_IRQn);
			buf = net_buf_get(&rx.fifo, K_NO_WAIT);
		} while (buf);
	}
}

static size_t h4_discard(size_t len)
{
	uint8_t buf[33];
	int err;

	err = cmac_mbox_read(buf, MIN(len, sizeof(buf)));
	if (unlikely(err < 0)) {
		LOG_ERR("Unable to read from UART (err %d)", err);
		return 0;
	}

	return err;
}

static inline void read_payload(void)
{
	struct net_buf *buf;
	uint8_t evt_flags;
	int read;

	if (!rx.buf) {
		size_t buf_tailroom;

		rx.buf = get_rx(K_NO_WAIT);
		if (!rx.buf) {
			if (rx.discardable) {
				LOG_WRN("Discarding event 0x%02x", rx.evt.evt);
				rx.discard = rx.remaining;
				reset_rx();
				return;
			}

			LOG_WRN("Failed to allocate, deferring to rx_thread");
			irq_disable(CMAC2SYS_IRQn);
			return;
		}

		LOG_DBG("Allocated rx.buf %p", rx.buf);

		buf_tailroom = net_buf_tailroom(rx.buf);
		if (buf_tailroom < rx.remaining) {
			LOG_ERR("Not enough space in buffer %u/%zu", rx.remaining, buf_tailroom);
			rx.discard = rx.remaining;
			reset_rx();
			return;
		}

		copy_hdr(rx.buf);
	}

	read = cmac_mbox_read(net_buf_tail(rx.buf), rx.remaining);
	if (unlikely(read < 0)) {
		LOG_ERR("Failed to read UART (err %d)", read);
		return;
	}

	net_buf_add(rx.buf, read);
	rx.remaining -= read;

	LOG_DBG("got %d bytes, remaining %u", read, rx.remaining);
	LOG_DBG("Payload (len %u): %s", rx.buf->len, bt_hex(rx.buf->data, rx.buf->len));

	if (rx.remaining) {
		return;
	}

	buf = rx.buf;
	rx.buf = NULL;

	if (rx.type == H4_EVT) {
		evt_flags = bt_hci_evt_get_flags(rx.evt.evt);
		bt_buf_set_type(buf, BT_BUF_EVT);
	} else {
		evt_flags = BT_HCI_EVT_FLAG_RECV;
		bt_buf_set_type(buf, BT_BUF_ACL_IN);
	}

	reset_rx();

	if (IS_ENABLED(CONFIG_BT_RECV_BLOCKING) &&
	    (evt_flags & BT_HCI_EVT_FLAG_RECV_PRIO)) {
		LOG_DBG("Calling bt_recv_prio(%p)", buf);
		bt_recv_prio(buf);
	}

	if (evt_flags & BT_HCI_EVT_FLAG_RECV) {
		LOG_DBG("Putting buf %p to rx fifo", buf);
		net_buf_put(&rx.fifo, buf);
	}
}

static inline void read_header(void)
{
	switch (rx.type) {
	case H4_NONE:
		h4_get_type();
		return;
	case H4_EVT:
		get_evt_hdr();
		break;
	case H4_ACL:
		get_acl_hdr();
		break;
	case H4_ISO:
		if (IS_ENABLED(CONFIG_BT_ISO)) {
			get_iso_hdr();
			break;
		}
		__fallthrough;
	default:
		CODE_UNREACHABLE;
		return;
	}

	if (rx.have_hdr && rx.buf) {
		if (rx.remaining > net_buf_tailroom(rx.buf)) {
			LOG_ERR("Not enough space in buffer");
			rx.discard = rx.remaining;
			reset_rx();
		} else {
			copy_hdr(rx.buf);
		}
	}
}

static inline void process_rx(void)
{
	LOG_DBG("remaining %u discard %u have_hdr %u rx.buf %p len %u", rx.remaining, rx.discard,
		rx.have_hdr, rx.buf, rx.buf ? rx.buf->len : 0);

	if (rx.discard) {
		rx.discard -= h4_discard(rx.discard);
		return;
	}

	if (rx.have_hdr) {
		read_payload();
	} else {
		read_header();
	}
}

void cmac_read_req(void)
{
	while (cmac_mbox_has_data()) {
		process_rx();
	}
}

static int bt_da1469x_open(void)
{
	k_tid_t tid;

	tid = k_thread_create(&rx_thread_data, rx_thread_stack,
			      K_KERNEL_STACK_SIZEOF(rx_thread_stack),
			      rx_thread, NULL, NULL, NULL,
			      K_PRIO_COOP(CONFIG_BT_RX_PRIO),
			      0, K_NO_WAIT);
	k_thread_name_set(tid, "bt_rx_thread");

	cmac_enable();
	irq_enable(CMAC2SYS_IRQn);

	return 0;
}

#ifdef CONFIG_BT_HCI_HOST
static int bt_da1469x_close(void)
{
	irq_disable(CMAC2SYS_IRQn);
	cmac_disable();

	return 0;
}
#endif /* CONFIG_BT_HCI_HOST */

static int bt_da1469x_send(struct net_buf *buf)
{
	switch (bt_buf_get_type(buf)) {
	case BT_BUF_ACL_OUT:
		LOG_DBG("ACL: buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);
		net_buf_push_u8(buf, H4_ACL);
		break;
	case BT_BUF_CMD:
		LOG_DBG("CMD: buf %p type %u len %u", buf, bt_buf_get_type(buf), buf->len);
		net_buf_push_u8(buf, H4_CMD);
		break;
	default:
		LOG_ERR("Unsupported type");
		return -EINVAL;
	}

	cmac_mbox_write(buf->data, buf->len);

	net_buf_unref(buf);

	return 0;
}

static const struct bt_hci_driver drv = {
	.name           = "BT DA1469x",
	.bus            = BT_HCI_DRIVER_BUS_IPM,
	.open           = bt_da1469x_open,
#ifdef CONFIG_BT_HCI_HOST
	.close          = bt_da1469x_close,
#endif
	.send           = bt_da1469x_send,
};

static int bt_da1469x_init(const struct device *unused)
{
	ARG_UNUSED(unused);

	irq_disable(CMAC2SYS_IRQn);

	bt_hci_driver_register(&drv);

	cmac_disable();
	cmac_load_image();
	cmac_configure_pdc();
	cmac_configure_shm();

	IRQ_CONNECT(CMAC2SYS_IRQn, 0, cmac_cmac2sys_isr, NULL, 0);

	return 0;
}

SYS_INIT(bt_da1469x_init, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);
