/*
 * Copyright (c) 2023 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <string.h>
#include <zephyr/drivers/rtc.h>

#define LOG_LEVEL CONFIG_RTC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#if DT_HAS_COMPAT_STATUS_OKAY(renesas_smartbond_rtc)
#define RTC_DEV_COMPAT renesas_smartbond_rtc
#else
#error "You need to enable RTC device"
#endif

static void *callback_test_user_data_address;
static uint32_t callback_called_counter;
static struct k_spinlock lock;
static uint32_t test_user_data = 0x1234;

#define EXPECTED_SUPPORTED_ALARM_FIELDS \
	(RTC_ALARM_TIME_MASK_SECOND | RTC_ALARM_TIME_MASK_MINUTE | RTC_ALARM_TIME_MASK_HOUR | \
	 RTC_ALARM_TIME_MASK_MONTH | RTC_ALARM_TIME_MASK_MONTHDAY)

static void test_rtc_update_callback_handler(const struct device *dev, void *user_data)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	callback_called_counter++;
	callback_test_user_data_address = user_data;

	k_spin_unlock(&lock, key);
}

static uint32_t rtc_alarm_handler_data = 0x1234;
struct k_sem alarm_sync;

static void rtc_alarm_handler(const struct device *dev, uint16_t id, void *user_data)
{
	uint32_t *cb_data = user_data;

	if (id != 0) {
		LOG_ERR("Invalid alarm cb id\n");
		return;
	}

	if (*cb_data != rtc_alarm_handler_data) {
		LOG_ERR("Invalid alarm cb data\n");
		return;
	}

	k_sem_give(&alarm_sync);

}

static void test_set_get(const struct device *dev)
{
	struct rtc_time rtc_time_set = {
		.tm_sec = 0,
		.tm_min = 16,
		.tm_hour = 21,
		.tm_mday = 13,
		.tm_mon = 10 - 1,
		.tm_year = 2023 - 1900,
		.tm_wday = 5, // Friday
		.tm_yday = -1,
		.tm_isdst = -1,
		.tm_nsec = 0
	};

	struct rtc_time rtc_time_get = { 0 };

	k_spinlock_key_t key;
	void *address;
	uint32_t counter;
	uint16_t alarm_mask, alarm_mask_get, alarm_mask_set;
	struct rtc_time rtc_alarm_get = { 0 };
	struct rtc_time rtc_alarm_set;

	if (rtc_set_time(dev, &rtc_time_set)) {
		LOG_ERR("Failed to set time\n");
		return;
	}

	if (rtc_get_time(dev, &rtc_time_get)) {
		LOG_ERR("Failed to get time\n");
		return;
	}

	LOG_INF("%d/%d/%d %d:%d:%d Day=%d\n", rtc_time_get.tm_mday, rtc_time_get.tm_mon + 1,
											rtc_time_get.tm_year + 1900,
									rtc_time_get.tm_hour, rtc_time_get.tm_min, rtc_time_get.tm_sec,
									rtc_time_get.tm_wday);

	if (memcmp(&rtc_time_get, &rtc_time_get, sizeof(rtc_time_get))) {
		LOG_ERR("Set time mismatch get time\n");
	}

	/* Seting time/date out of bandaries does not mean that RTC setting will fail.
	   This is because bit field masks will truncate values out of bandaries. */

	/* Set invalid date */
	rtc_time_get.tm_year -= 3600;
	if (rtc_set_time(dev, &rtc_time_get)) {
		LOG_WRN("Device failed correctly as time was invalid\n");
	} else {
		LOG_WRN("Device did not failed\n");
	}

	/* Make sure the last valid date was re-used */
	if (rtc_get_time(dev, &rtc_time_get)) {
		LOG_ERR("Failed to get time\n");
		return;
	}

	LOG_INF("%d/%d/%d %d:%d:%d Day=%d\n", rtc_time_get.tm_mday, rtc_time_get.tm_mon + 1,
											rtc_time_get.tm_year + 1900,
									rtc_time_get.tm_hour, rtc_time_get.tm_min, rtc_time_get.tm_sec,
									rtc_time_get.tm_wday);

	rtc_time_set.tm_sec = rtc_time_get.tm_sec = 0;

	if (memcmp(&rtc_time_get, &rtc_time_set, sizeof(rtc_time_get))) {
		LOG_ERR("RTC failed to recover from invalid time");
		return;
	}

	if (rtc_update_set_callback(dev, NULL, NULL)) {
		LOG_ERR("Failed to set update callback\n");
		return;
	};

	if (rtc_get_time(dev, &rtc_time_get)) {
		LOG_ERR("Failed to get time\n");
		return;
	}

	LOG_INF("%d/%d/%d %d:%d:%d Day=%d\n", rtc_time_get.tm_mday, rtc_time_get.tm_mon + 1,
											rtc_time_get.tm_year + 1900,
									rtc_time_get.tm_hour, rtc_time_get.tm_min, rtc_time_get.tm_sec,
									rtc_time_get.tm_wday);

	key = k_spin_lock(&lock);
	callback_called_counter = 0;
	address = callback_test_user_data_address;
	k_spin_unlock(&lock, key);

	k_msleep(500);

	key = k_spin_lock(&lock);
	counter = callback_called_counter;
	k_spin_unlock(&lock, key);

	if (counter) {
		LOG_ERR("Update callback should not have been called\n");
		return;
	}

	if (rtc_update_set_callback(dev, test_rtc_update_callback_handler, &test_user_data)) {
		LOG_ERR("Failed to set update callback\n");
		return;
	}

	LOG_INF("Delay for 10 sec...\n");
	k_sleep(K_MSEC(10000));

	key = k_spin_lock(&lock);
	counter = callback_called_counter;
	address = callback_test_user_data_address;
	k_spin_unlock(&lock, key);

	if ((*((uint32_t *)address) != test_user_data) || (counter == 0)) {
		LOG_ERR("Update_callback was not invoked\n");
		return;
	}

	if (counter < 10 && counter > 11) {
		LOG_ERR("Invalid update counter...\n");
		return;
	}

	LOG_INF("Counter value = %d\n", counter);

	if (rtc_update_set_callback(dev, NULL, NULL)) {
		LOG_ERR("Failed to set update callback\n");
		return;
	};

	LOG_INF("Delay for 5 sec...\n");
	k_msleep(5000);

	if (counter != callback_called_counter) {
		LOG_ERR("Update callback should not have been called\n");
		return;
	}

	if (rtc_get_time(dev, &rtc_time_get)) {
		LOG_ERR("Failed to get time\n");
		return;
	}

	LOG_INF("%d/%d/%d %d:%d:%d Day=%d\n", rtc_time_get.tm_mday, rtc_time_get.tm_mon + 1,
											rtc_time_get.tm_year + 1900,
									rtc_time_get.tm_hour, rtc_time_get.tm_min, rtc_time_get.tm_sec,
									rtc_time_get.tm_wday);

	if (rtc_alarm_get_supported_fields(dev, 0, &alarm_mask)) {
		LOG_ERR("Failed to get supported alarm mask\n");
		return;
	}

	if (alarm_mask != EXPECTED_SUPPORTED_ALARM_FIELDS) {
		LOG_ERR("Alarm mask mismatches with the expected one\n");
		return;
	}

	/* Invalidate mask */
	alarm_mask |= RTC_ALARM_TIME_MASK_NSEC;
	if (rtc_alarm_set_time(dev, 0, alarm_mask, NULL)) {
		LOG_WRN("Setting alarm failed correctly as mask was invalid\n");
	} else {
		LOG_ERR("Setting alarm did not failed\n");
	}
	/* Validate mask */
	alarm_mask &= ~RTC_ALARM_TIME_MASK_NSEC;

	alarm_mask_set = RTC_ALARM_TIME_MASK_SECOND | RTC_ALARM_TIME_MASK_MINUTE;
	rtc_alarm_set = rtc_time_get;
	rtc_alarm_set.tm_sec += 10;
	rtc_alarm_set.tm_min += 1;
	/* Invalidate fields that are not suppported */
	rtc_alarm_set.tm_wday = rtc_alarm_set.tm_year = -1;
	if (rtc_alarm_set_time(dev, 0, alarm_mask_set, &rtc_alarm_set)) {
		LOG_ERR("Failed to set alarm\n");
		return;
	}

	if (rtc_alarm_get_time(dev, 0, &alarm_mask_get, &rtc_alarm_get)) {
		LOG_ERR("Failed to read alarm\n");
		return;
	}

	LOG_INF("Next alarm in: %d/%d/%d %d:%d:%d Day=%d, Mask=%d\n", rtc_alarm_get.tm_mday, rtc_alarm_get.tm_mon + 1,
											rtc_time_get.tm_year + 1900,
									rtc_alarm_get.tm_hour, rtc_alarm_get.tm_min, rtc_alarm_get.tm_sec,
									rtc_time_get.tm_wday, alarm_mask_get);


	if (alarm_mask_set != alarm_mask_get) {
		LOG_ERR("Retrieved alarm mask mismatches with the previous one\n");
		return;
	}

	if (memcmp(&rtc_alarm_set, &rtc_alarm_get, sizeof(rtc_alarm_get))) {
		LOG_ERR("Retrieved alarm mismatches the previous one\n");
		return;
	}

	if (rtc_alarm_is_pending(dev, 0)) {
		LOG_ERR("Alarm was penidng though it should not\n");
		return;
	};

	LOG_INF("Delay for 71 sec...\n");
	k_sleep(K_SECONDS(71));

	if (rtc_alarm_is_pending(dev, 0)) {
		LOG_INF("Alarm triggered!\n");
	} else {
		LOG_ERR("Alarm failed to be triggered");
		return;
	}

	if (rtc_alarm_is_pending(dev, 0)) {
		LOG_ERR("Alarm was penidng though it should not\n");
		return;
	};

	if (rtc_get_time(dev, &rtc_time_get)) {
		LOG_ERR("Failed to get time\n");
		return;
	}

	if (((rtc_alarm_set.tm_sec + 1) != rtc_time_get.tm_sec) ||
		(rtc_alarm_set.tm_min != rtc_time_get.tm_min) ||
		(rtc_alarm_set.tm_hour != rtc_time_get.tm_hour)) {
		LOG_ERR("Alarm did not hit in time!\n");
		return;
	}

	LOG_INF("%d/%d/%d %d:%d:%d Day=%d\n", rtc_time_get.tm_mday, rtc_time_get.tm_mon + 1,
											rtc_time_get.tm_year + 1900,
									rtc_time_get.tm_hour, rtc_time_get.tm_min, rtc_time_get.tm_sec,
									rtc_time_get.tm_wday);

	k_sem_init(&alarm_sync, 0, 1);
	if (rtc_alarm_set_callback(dev, 0, rtc_alarm_handler, &rtc_alarm_handler_data)) {
		LOG_ERR("Failed to set alarm callback\n");
		return;
	}

	alarm_mask_set = RTC_ALARM_TIME_MASK_SECOND;;
	rtc_alarm_set = rtc_time_get;
	rtc_alarm_set.tm_sec += 10;

	/* Invalidate fields that are not suppported */
	rtc_alarm_set.tm_wday = rtc_alarm_set.tm_year = -1;
	if (rtc_alarm_set_time(dev, 0, alarm_mask_set, &rtc_alarm_set)) {
		LOG_ERR("Failed to set alarm\n");
		return;
	}

	if (rtc_alarm_get_time(dev, 0, &alarm_mask_get, &rtc_alarm_get)) {
		LOG_ERR("Failed to read alarm\n");
		return;
	}

	LOG_INF("Next alarm in: %d/%d/%d %d:%d:%d Day=%d, Mask=%d\n", rtc_alarm_get.tm_mday, rtc_alarm_get.tm_mon + 1,
											rtc_time_get.tm_year + 1900,
									rtc_alarm_get.tm_hour, rtc_alarm_get.tm_min, rtc_alarm_get.tm_sec,
									rtc_time_get.tm_wday, alarm_mask_get);


	if (alarm_mask_set != alarm_mask_get) {
		LOG_ERR("Retrieved alarm mask mismatches with the previous one\n");
		return;
	}

	if (memcmp(&rtc_alarm_set, &rtc_alarm_get, sizeof(rtc_alarm_get))) {
		LOG_ERR("Retrieved alarm mismatches the previous one\n");
		return;
	}

	if (k_sem_take(&alarm_sync, K_SECONDS(15)) != 0) {
		LOG_ERR("Alarm did not timeout in time\n");
		return;
	}

	if (rtc_get_time(dev, &rtc_time_get)) {
		LOG_ERR("Failed to get time\n");
		return;
	}

	if ((rtc_alarm_set.tm_sec != rtc_time_get.tm_sec) ||
		(rtc_alarm_set.tm_min != rtc_time_get.tm_min) ||
		(rtc_alarm_set.tm_hour != rtc_time_get.tm_hour)) {
		LOG_ERR("Alarm did not hit in time!\n");
		return;
	}

	LOG_INF("%d/%d/%d %d:%d:%d Day=%d\n", rtc_time_get.tm_mday, rtc_time_get.tm_mon + 1,
											rtc_time_get.tm_year + 1900,
									rtc_time_get.tm_hour, rtc_time_get.tm_min, rtc_time_get.tm_sec,
									rtc_time_get.tm_wday);

	if (rtc_alarm_set_time(dev, 0, 0, NULL)) {
		LOG_ERR("Failed to disable alarm\n");
		return;
	}

	LOG_INF("Test is finished!\n");

	return;
}

struct mode_test {
	const char *mode;
	void (*mode_func)(const struct device *dev);
};

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET_ONE(RTC_DEV_COMPAT);

	if (!device_is_ready(dev)) {
		LOG_ERR("RTC device is not ready\n");
		return 0;
	}

	const struct mode_test modes[] = {
		{.mode = "---------- RTC SET/GET TEST --------", .mode_func = test_set_get},
		{ }
	};
	int i;

	LOG_INF("###### RTC Test Vectors #######\n");

	for (i = 0; modes[i].mode; i++) {
		LOG_INF("%s", modes[i].mode);
		modes[i].mode_func(dev);
	}
	return 0;
}