/*
 * Copyright 2023 Jerzy Kasenebrg
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_smartbond_regulator

#include <stdint.h>

#include <zephyr/drivers/regulator.h>
#include <zephyr/logging/log.h>
#include <DA1469xAB.h>

LOG_MODULE_REGISTER(regulator_da1469x, CONFIG_REGULATOR_LOG_LEVEL);

/* Rail should be powered during sleep, mode applies to VDD, V18, V18P */
#define DA1469X_MODE_SLEEP_LDO					0x01
/* Settings valid for VDD, V14, V18, V18P */
#define DA1469X_MODE_DCDC_HIGH_BATTERY_VOLTAGE_ON		0x02
#define DA1469X_MODE_DCDC_LOW_BATTERY_VOLTAGE_ON		0x04
/* Settings valid for V30 */
#define DA1469X_MODE_LDO_VBUS_ON				0x08
#define DA1469X_MODE_LDO_VBAT_ON				0x10
#define DA1469X_MODE_V30_VBAT_CLAMP_ON				0x20

#define DCDC_REG_FIELD_MSK(dcdc, field) \
	DCDC_DCDC_ ## dcdc ## _REG_DCDC_ ## dcdc ## _ ## field ## _Msk
#define DCDC_REG_FIELD_POS(dcdc, field) \
	DCDC_DCDC_ ## dcdc ## _REG_DCDC_ ## dcdc ## _ ## field ## _Pos

#define DCDC_REG_GET(dcdc, field) \
	((DCDC->DCDC_ ## dcdc ## _REG & DCDC_REG_FIELD_MSK(dcdc, field)) >> \
	DCDC_REG_FIELD_POS(dcdc, field))

#define DCDC_REG_SET(dcdc, field, val) \
	DCDC->DCDC_ ## dcdc ## _REG = \
	((DCDC->DCDC_ ## dcdc ## _REG & ~DCDC_REG_FIELD_MSK(dcdc, field)) | \
	((val) << DCDC_REG_FIELD_POS(dcdc, field)))

#define POWER_REG_FIELD_MSK(field) \
	CRG_TOP_POWER_CTRL_REG_ ## field ## _Msk
#define POWER_REG_FIELD_POS(field) \
	CRG_TOP_POWER_CTRL_REG_ ## field ## _Pos

#define POWER_REG_GET(field) \
	((CRG_TOP->POWER_CTRL_REG & POWER_REG_FIELD_MSK(field)) >> POWER_REG_FIELD_POS(field))

#define POWER_REG_SET(field, val) \
	CRG_TOP->POWER_CTRL_REG = \
	((CRG_TOP->POWER_CTRL_REG & ~POWER_REG_FIELD_MSK(field)) | \
	((val) << POWER_REG_FIELD_POS(field)))

static const uint32_t voltages_vdd_clamp[] = {
	1037000, 1005000, 978000, 946000,
	1120000, 1089000, 1058000, 1030000,
	952000, 918000, 889000, 861000,
	862000, 828000, 798000, 706000
};

static const uint32_t voltages_vdd[] = {
	900000, 1000000, 1100000, 1200000
};

static const uint32_t voltages_vdd_sleep[] = {
	750000, 800000, 850000, 900000,
};

static const uint32_t voltages_v14[] = {
	1200000, 1250000, 1300000, 1350000,
	1400000, 1450000, 1500000, 1550000
};

static const uint32_t voltages_v30[] = {
	3000000, 3300000
};

static const uint32_t voltages_v18[] = {
	1200000, 1800000
};

static const uint32_t voltages_v18p[] = { 1800000 };

enum da1469x_rail {
	VDD_CLAMP,
	VDD_SLEEP,
	VDD,
	V14,
	V18,
	V18P,
	V30,
};

struct regulator_da1469x_config {
	struct regulator_common_config common;
	const uint32_t *voltages;
	const uint8_t voltages_count;
	enum da1469x_rail rail;
	uint8_t flags;
};

struct regulator_da1469x_data {
	struct regulator_common_data common;
};

static int regulator_da1469x_enable(const struct device *dev)
{
	const struct regulator_da1469x_config *config = dev->config;

	switch (config->rail) {
	case VDD:
		POWER_REG_SET(LDO_CORE_ENABLE, 1);
		DCDC_REG_SET(VDD, ENABLE_HV,
			     !!(config->flags & DA1469X_MODE_DCDC_HIGH_BATTERY_VOLTAGE_ON));
		DCDC_REG_SET(VDD, ENABLE_LV,
			     !!(config->flags & DA1469X_MODE_DCDC_LOW_BATTERY_VOLTAGE_ON));
		break;
	case VDD_CLAMP:
		/* Always on */
		break;
	case VDD_SLEEP:
		POWER_REG_SET(LDO_CORE_RET_ENABLE_SLEEP, 1);
		break;
	case V14:
		POWER_REG_SET(LDO_RADIO_ENABLE, 1);
		DCDC_REG_SET(V14, ENABLE_HV,
			     !!(config->flags & DA1469X_MODE_DCDC_HIGH_BATTERY_VOLTAGE_ON));
		DCDC_REG_SET(V14, ENABLE_LV,
			     !!(config->flags & DA1469X_MODE_DCDC_LOW_BATTERY_VOLTAGE_ON));
		break;
	case V18:
		POWER_REG_SET(LDO_1V8_ENABLE, 1);
		POWER_REG_SET(LDO_1V8_RET_ENABLE_SLEEP, config->flags & DA1469X_MODE_SLEEP_LDO);
		DCDC_REG_SET(V18, ENABLE_HV,
			     !!(config->flags & DA1469X_MODE_DCDC_HIGH_BATTERY_VOLTAGE_ON));
		DCDC_REG_SET(V18, ENABLE_LV,
			     !!(config->flags & DA1469X_MODE_DCDC_LOW_BATTERY_VOLTAGE_ON));
		break;
	case V18P:
		POWER_REG_SET(LDO_1V8P_ENABLE, 1);
		POWER_REG_SET(LDO_1V8P_RET_ENABLE_SLEEP, config->flags & DA1469X_MODE_SLEEP_LDO);
		DCDC_REG_SET(V18P, ENABLE_HV,
			     !!(config->flags & DA1469X_MODE_DCDC_HIGH_BATTERY_VOLTAGE_ON));
		DCDC_REG_SET(V18P, ENABLE_LV,
			     !!(config->flags & DA1469X_MODE_DCDC_LOW_BATTERY_VOLTAGE_ON));
		break;
	case V30:
		POWER_REG_SET(LDO_3V0_RET_ENABLE_SLEEP, config->flags & DA1469X_MODE_SLEEP_LDO);
		POWER_REG_SET(LDO_3V0_MODE,
			      (((config->flags & DA1469X_MODE_LDO_VBUS_ON) ? 2 : 0) |
			       ((config->flags & DA1469X_MODE_LDO_VBAT_ON) ? 1 : 0)));
		break;
	default:
		break;
	}

	/*
	 * Enable DCDC if:
	 * 1. it was not already enabled, and
	 * 2. VBAT is above minimal value
	 * 3. Just turned on rail requested DCDC
	 */
	if (((DCDC->DCDC_CTRL1_REG & DCDC_DCDC_CTRL1_REG_DCDC_ENABLE_Msk) == 0) &&
	    (CRG_TOP->ANA_STATUS_REG & CRG_TOP_ANA_STATUS_REG_COMP_VBAT_HIGH_Msk) &&
	    (config->flags & (DA1469X_MODE_DCDC_HIGH_BATTERY_VOLTAGE_ON |
			      DA1469X_MODE_DCDC_LOW_BATTERY_VOLTAGE_ON))) {
		DCDC->DCDC_CTRL1_REG |= DCDC_DCDC_CTRL1_REG_DCDC_ENABLE_Msk;
	}

	return 0;
}

static int regulator_da1469x_disable(const struct device *dev)
{
	const struct regulator_da1469x_config *config = dev->config;

	switch (config->rail) {
	case VDD:
	case VDD_CLAMP:
		/* Always on */
		break;
	case VDD_SLEEP:
		POWER_REG_SET(LDO_CORE_RET_ENABLE_SLEEP, 0);
		break;
	case V14:
		POWER_REG_SET(LDO_RADIO_ENABLE, 0);
		DCDC_REG_SET(V14, ENABLE_HV, 0);
		DCDC_REG_SET(V14, ENABLE_LV, 0);
		break;
	case V18:
		POWER_REG_SET(LDO_1V8_ENABLE, 0);
		POWER_REG_SET(LDO_1V8_RET_ENABLE_SLEEP, 0);
		DCDC_REG_SET(V18, ENABLE_HV, 0);
		DCDC_REG_SET(V18, ENABLE_LV, 0);
		break;
	case V18P:
		POWER_REG_SET(LDO_1V8P_ENABLE, 0);
		POWER_REG_SET(LDO_1V8P_RET_ENABLE_SLEEP, 0);
		DCDC_REG_SET(V18P, ENABLE_HV, 0);
		DCDC_REG_SET(V18P, ENABLE_LV, 0);
		break;
	case V30:
		POWER_REG_SET(LDO_3V0_MODE, 0);
		POWER_REG_SET(LDO_3V0_RET_ENABLE_SLEEP, 0);
		break;
	default:
		break;
	}
	/* Turn off DCDC if it's no longer requested by any rail */
	if ((DCDC->DCDC_CTRL1_REG & DCDC_DCDC_CTRL1_REG_DCDC_ENABLE_Msk) &&
	    (DCDC_REG_GET(VDD, ENABLE_HV) == 0) &&
	    (DCDC_REG_GET(VDD, ENABLE_LV) == 0) &&
	    (DCDC_REG_GET(V14, ENABLE_HV) == 0) &&
	    (DCDC_REG_GET(V14, ENABLE_LV) == 0) &&
	    (DCDC_REG_GET(V18, ENABLE_HV) == 0) &&
	    (DCDC_REG_GET(V18, ENABLE_LV) == 0) &&
	    (DCDC_REG_GET(V18P, ENABLE_HV) == 0) &&
	    (DCDC_REG_GET(V18P, ENABLE_LV) == 0)) {
		DCDC->DCDC_CTRL1_REG &= ~DCDC_DCDC_CTRL1_REG_DCDC_ENABLE_Msk;
	}

	return 0;
}

static unsigned int regulator_da1469x_count_voltages(const struct device *dev)
{
	const struct regulator_da1469x_config *config = dev->config;

	return config->voltages_count;
}

static int regulator_da1469x_list_voltage(const struct device *dev,
					  unsigned int idx,
					  int32_t *volt_uv)
{
	const struct regulator_da1469x_config *config = dev->config;

	if (idx >= config->voltages_count) {
		return -EINVAL;
	}

	*volt_uv = config->voltages[idx];

	return 0;
}

static int regulator_da1469x_set_voltage(const struct device *dev, int32_t min_uv,
					 int32_t max_uv)
{
	int ret;
	const struct regulator_da1469x_config *config = dev->config;
	int i;

	/* Check if voltage can be provided */
	for (i = 0; i < config->voltages_count; ++i) {
		if (min_uv <= config->voltages[i] && config->voltages[i] <= max_uv) {
			break;
		}
	}
	if (i < config->voltages_count) {
		ret = 0;
		switch (config->rail) {
		case VDD_CLAMP:
			POWER_REG_SET(VDD_CLAMP_LEVEL, i);
			break;
		case VDD_SLEEP:
			POWER_REG_SET(VDD_SLEEP_LEVEL, i);
			break;
		case VDD:
			POWER_REG_SET(VDD_LEVEL, i);
			break;
		case V14:
			POWER_REG_SET(V14_LEVEL, i);
			break;
		case V18:
			POWER_REG_SET(V18_LEVEL, i);
			break;
		case V18P:
			break;
		case V30:
			POWER_REG_SET(V30_LEVEL, i << 1);
			break;
		}
	} else {
		ret = -EINVAL;
	}

	return ret;
}

static int regulator_da1469x_get_voltage(const struct device *dev,
					 int32_t *volt_uv)
{
	const struct regulator_da1469x_config *config = dev->config;
	int voltage_ix;

	switch (config->rail) {
	case VDD_CLAMP:
		voltage_ix = POWER_REG_GET(VDD_CLAMP_LEVEL);
		break;
	case VDD:
		voltage_ix = POWER_REG_GET(VDD_LEVEL);
		break;
	case VDD_SLEEP:
		voltage_ix = POWER_REG_GET(VDD_SLEEP_LEVEL);
		break;
	case V18:
		voltage_ix = POWER_REG_GET(V18_LEVEL);
		break;
	case V14:
		voltage_ix = POWER_REG_GET(V14_LEVEL);
		break;
	case V30:
		voltage_ix = POWER_REG_GET(V30_LEVEL) >> 1;
		break;
	default:
		voltage_ix = 0;
		break;
	}
	*volt_uv = config->voltages[voltage_ix];

	return 0;
}

static int regulator_da1469x_set_current_limit(const struct device *dev,
					       int32_t min_ua, int32_t max_ua)
{
	const struct regulator_da1469x_config *config = dev->config;
	int ret = 0;

	switch (config->rail) {
	case VDD:
		DCDC_REG_SET(VDD, CUR_LIM_MAX_HV, ((max_ua / 30000) - 1));
		DCDC_REG_SET(VDD, CUR_LIM_MAX_LV, ((max_ua / 30000) - 1));
		DCDC_REG_SET(VDD, CUR_LIM_MIN, ((max_ua / 30000) - 1));
		break;
	case V14:
		DCDC_REG_SET(V14, CUR_LIM_MAX_HV, ((max_ua / 30000) - 1));
		DCDC_REG_SET(V14, CUR_LIM_MAX_LV, ((max_ua / 30000) - 1));
		DCDC_REG_SET(V14, CUR_LIM_MIN, ((max_ua / 30000) - 1));
		break;
	case V18:
		DCDC_REG_SET(V18, CUR_LIM_MAX_HV, ((max_ua / 30000) - 1));
		DCDC_REG_SET(V18, CUR_LIM_MAX_LV, ((max_ua / 30000) - 1));
		DCDC_REG_SET(V18, CUR_LIM_MIN, ((max_ua / 30000) - 1));
		break;
	case V18P:
		DCDC_REG_SET(V18P, CUR_LIM_MAX_HV, ((max_ua / 30000) - 1));
		DCDC_REG_SET(V18P, CUR_LIM_MAX_LV, ((max_ua / 30000) - 1));
		DCDC_REG_SET(V18P, CUR_LIM_MIN, ((max_ua / 30000) - 1));
		break;
	default:
		ret = -ENOSYS;
		break;
	}

	return ret;
}

static int regulator_da1469x_get_current_limit(const struct device *dev,
					       int32_t *curr_ua)
{
	const struct regulator_da1469x_config *config = dev->config;
	int ret = 0;

	switch (config->rail) {
	case VDD:
		*curr_ua = (DCDC_REG_GET(VDD, CUR_LIM_MAX_HV) - 1) * 30000;
		break;
	case V14:
		*curr_ua = (DCDC_REG_GET(V14, CUR_LIM_MAX_HV) - 1) * 30000;
		break;
	case V18:
		*curr_ua = (DCDC_REG_GET(V18, CUR_LIM_MAX_HV) - 1) * 30000;
		break;
	case V18P:
		*curr_ua = (DCDC_REG_GET(V18P, CUR_LIM_MAX_HV) - 1) * 30000;
		break;
	default:
		ret = -ENOSYS;
	}

	return ret;
}

static const struct regulator_driver_api regulator_da1469x_api = {
	.enable = regulator_da1469x_enable,
	.disable = regulator_da1469x_disable,
	.count_voltages = regulator_da1469x_count_voltages,
	.list_voltage = regulator_da1469x_list_voltage,
	.set_voltage = regulator_da1469x_set_voltage,
	.get_voltage = regulator_da1469x_get_voltage,
	.set_current_limit = regulator_da1469x_set_current_limit,
	.get_current_limit = regulator_da1469x_get_current_limit,
};

static int regulator_da1469x_init(const struct device *dev)
{
	const struct regulator_da1469x_config *config = dev->config;

	regulator_common_data_init(dev);

	if (config->rail == V30) {
		POWER_REG_SET(LDO_3V0_REF, DT_INST_PROP_OR(DT_NODELABEL(V30),
							   regulator_v30_ref_bandgap, 0));
	}

	return regulator_common_init(dev, 0);
}

#define REGULATOR_DA1469X_DEFINE(node, id, rail_id)                            \
	static struct regulator_da1469x_data data_##id;                        \
                                                                               \
	static const struct regulator_da1469x_config config_##id = {           \
		.common = REGULATOR_DT_COMMON_CONFIG_INIT(node),               \
		.voltages = voltages_ ## id,                                   \
		.voltages_count = ARRAY_SIZE(voltages_ ## id),                 \
		.rail = rail_id,                                               \
		.flags = (DT_PROP(node, renesas_regulator_v30_clamp) *         \
			  DA1469X_MODE_V30_VBAT_CLAMP_ON) |                    \
			 (DT_PROP(node, renesas_regulator_v30_vbus) *          \
			  DA1469X_MODE_LDO_VBUS_ON) |                          \
			 (DT_PROP(node, renesas_regulator_v30_vbat) *          \
			  DA1469X_MODE_LDO_VBAT_ON) |                          \
			 (DT_PROP(node, renesas_regulator_dcdc_vbat_high) *    \
			  DA1469X_MODE_DCDC_HIGH_BATTERY_VOLTAGE_ON) |         \
			 (DT_PROP(node, renesas_regulator_dcdc_vbat_low) *     \
			  DA1469X_MODE_DCDC_LOW_BATTERY_VOLTAGE_ON) |          \
			 (DT_PROP(node, renesas_regulator_sleep_ldo) *         \
			  DA1469X_MODE_SLEEP_LDO)                              \
	};                                                                     \
	DEVICE_DT_DEFINE(node, regulator_da1469x_init, NULL, &data_##id,       \
			 &config_##id, PRE_KERNEL_1,                           \
			 CONFIG_REGULATOR_DA1469X_INIT_PRIORITY,               \
			 &regulator_da1469x_api);

#define REGULATOR_DA1469X_DEFINE_COND(inst, child, source)                     \
	COND_CODE_1(DT_NODE_EXISTS(DT_INST_CHILD(inst, child)),                \
		    (REGULATOR_DA1469X_DEFINE(                                 \
			DT_INST_CHILD(inst, child), child, source)),           \
		    ())

#define REGULATOR_DA1469X_DEFINE_ALL(inst)                                     \
	REGULATOR_DA1469X_DEFINE_COND(inst, vdd_clamp, VDD_CLAMP)              \
	REGULATOR_DA1469X_DEFINE_COND(inst, vdd_sleep, VDD_SLEEP)              \
	REGULATOR_DA1469X_DEFINE_COND(inst, vdd, VDD)                          \
	REGULATOR_DA1469X_DEFINE_COND(inst, v14, V14)                          \
	REGULATOR_DA1469X_DEFINE_COND(inst, v18, V18)                          \
	REGULATOR_DA1469X_DEFINE_COND(inst, v18p, V18P)                        \
	REGULATOR_DA1469X_DEFINE_COND(inst, v30, V30)                          \

DT_INST_FOREACH_STATUS_OKAY(REGULATOR_DA1469X_DEFINE_ALL)
