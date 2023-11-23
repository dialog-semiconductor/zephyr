/*
 * Copyright (c) 2023 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT  renesas_smartbond_mipi_dbi

#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <system_DA1469x.h>
#include <DA1469xAB.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/smartbond_clock_control.h>
#include <zephyr/drivers/display.h>
#include <zephyr/dt-bindings/mipi_dbi/smartbond_mipi_dbi.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(smartbond_mipi_dbi, CONFIG_MIPI_DBI_LOG_LEVEL);

#define SMARTBOND_IRQN		DT_INST_IRQN(0)
#define SMARTBOND_IRQ_PRIO  DT_INST_IRQ(0, priority)

#define SMARTBOND_MIPI_DBI_CLK_DIV(_freq)                           \
    ((32000000U % (_freq)) ? (96000000U / (_freq)) : (32000000U / (_freq)))

#define MIPI_DBI_SMARTBOND_IS_PLL_REQUIRED \
			!!(32000000U % DT_PROP(DT_CHOSEN(zephyr_display), spi_max_frequency))

#define MIPI_DBI_SMARTBOND_IS_TE_ENABLED \
			DT_INST_PROP_OR(0, te_enable, 0)

#define MIPI_DBI_SMARTBOND_IS_DMA_PREFETCH_ENABLED \
			DT_INST_PROP_OR(0, dma_prefetch, 0)

#define MIPI_DBI_SMARTBOND_IS_RESET_REQUIRED \
			DT_INST_NODE_HAS_PROP(0, reset_gpios)

#define CLK_SYS_REG_SET_FIELD(_field, _var, _val) 			\
	((_var)) = 												\
	((_var) & ~(CRG_SYS_CLK_SYS_REG_ ## _field ## _Msk)) | 	\
	(((_val) << CRG_SYS_CLK_SYS_REG_ ## _field ## _Pos) & 	\
	CRG_SYS_CLK_SYS_REG_ ## _field ## _Msk)

#define LCDC_BGCOLOR_REG_SET_FIELD(_field, _var, _val) 			\
	((_var)) = 													\
	((_var) & ~(LCDC_LCDC_BGCOLOR_REG_ ## _field ## _Msk)) | 	\
	(((_val) << LCDC_LCDC_BGCOLOR_REG_ ## _field ## _Pos) & 	\
	LCDC_LCDC_BGCOLOR_REG_ ## _field ## _Msk)

#define LCDC_RESXY_REG_SET_FIELD(_field, _var, _val) 			\
	((_var)) = 													\
	((_var) & ~(LCDC_LCDC_RESXY_REG_ ## _field ## _Msk)) | 		\
	(((_val) << LCDC_LCDC_RESXY_REG_ ## _field ## _Pos) & 		\
	LCDC_LCDC_RESXY_REG_ ## _field ## _Msk)

#define LCDC_FRONTPORCHXY_REG_SET_FIELD(_field, _var, _val)			\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_FRONTPORCHXY_REG_ ## _field ## _Msk)) | 	\
	(((_val) << LCDC_LCDC_FRONTPORCHXY_REG_ ## _field ## _Pos) & 	\
	LCDC_LCDC_FRONTPORCHXY_REG_ ## _field ## _Msk)

#define LCDC_BLANKINGXY_REG_SET_FIELD(_field, _var, _val)			\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_BLANKINGXY_REG_ ## _field ## _Msk)) | 	\
	(((_val) << LCDC_LCDC_BLANKINGXY_REG_ ## _field ## _Pos) & 		\
	LCDC_LCDC_BLANKINGXY_REG_ ## _field ## _Msk)

#define LCDC_BACKPORCHXY_REG_SET_FIELD(_field, _var, _val)			\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_BACKPORCHXY_REG_ ## _field ## _Msk)) | 	\
	(((_val) << LCDC_LCDC_BACKPORCHXY_REG_ ## _field ## _Pos) & 	\
	LCDC_LCDC_BACKPORCHXY_REG_ ## _field ## _Msk)

#define LCDC_DBIB_CMD_REG_SET_FIELD(_field, _var, _val)				\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_DBIB_CMD_REG_ ## _field ## _Msk)) | 		\
	(((_val) << LCDC_LCDC_DBIB_CMD_REG_ ## _field ## _Pos) & 		\
	LCDC_LCDC_DBIB_CMD_REG_ ## _field ## _Msk)

#define LCDC_CLKCTRL_REG_SET_FIELD(_field, _var, _val) 				\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_CLKCTRL_REG_ ## _field ## _Msk)) | 		\
	(((_val) << LCDC_LCDC_CLKCTRL_REG_ ## _field ## _Pos) & 		\
	LCDC_LCDC_CLKCTRL_REG_ ## _field ## _Msk)

#define LCDC_DBIB_CFG_REG_SET_FIELD(_field, _var, _val)				\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_DBIB_CFG_REG_ ## _field ## _Msk)) | 		\
	(((_val) << LCDC_LCDC_DBIB_CFG_REG_ ## _field ## _Pos) & 		\
	LCDC_LCDC_DBIB_CFG_REG_ ## _field ## _Msk)

#define LCDC_LAYER0_STARTXY_REG_SET_FIELD(_field, _var, _val)		\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_LAYER0_STARTXY_REG_ ## _field ## _Msk)) | \
	(((_val) << LCDC_LCDC_LAYER0_STARTXY_REG_ ## _field ## _Pos) & 	\
	LCDC_LCDC_LAYER0_STARTXY_REG_ ## _field ## _Msk)

#define LCDC_LAYER0_MODE_REG_SET_FIELD(_field, _var, _val)			\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_LAYER0_MODE_REG_ ## _field ## _Msk)) | 	\
	(((_val) << LCDC_LCDC_LAYER0_MODE_REG_ ## _field ## _Pos) & 	\
	LCDC_LCDC_LAYER0_MODE_REG_ ## _field ## _Msk)

#define LCDC_LAYER0_STRIDE_REG_SET_FIELD(_field, _var, _val)		\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_LAYER0_STRIDE_REG_ ## _field ## _Msk)) | 	\
	(((_val) << LCDC_LCDC_LAYER0_STRIDE_REG_ ## _field ## _Pos) & 	\
	LCDC_LCDC_LAYER0_STRIDE_REG_ ## _field ## _Msk)

#define LCDC_LAYER0_SIZEXY_REG_SET_FIELD(_field, _var, _val)		\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_LAYER0_SIZEXY_REG_ ## _field ## _Msk)) | 	\
	(((_val) << LCDC_LCDC_LAYER0_SIZEXY_REG_ ## _field ## _Pos) & 	\
	LCDC_LCDC_LAYER0_SIZEXY_REG_ ## _field ## _Msk)

#define LCDC_LAYER0_RESXY_REG_SET_FIELD(_field, _var, _val)			\
	((_var)) = 														\
	((_var) & ~(LCDC_LCDC_LAYER0_RESXY_REG_ ## _field ## _Msk)) | 	\
	(((_val) << LCDC_LCDC_LAYER0_RESXY_REG_ ## _field ## _Pos) & 	\
	LCDC_LCDC_LAYER0_RESXY_REG_ ## _field ## _Msk)

#define LCDC_STATUS_REG_GET_FIELD(_field)							 		\
	((LCDC->LCDC_STATUS_REG & LCDC_LCDC_STATUS_REG_ ## _field ## _Msk) >>	\
	LCDC_LCDC_STATUS_REG_ ## _field ## _Pos)

#define LCDC_DBIB_CFG_REG_GET_FIELD(_field)							 			\
	((LCDC->LCDC_DBIB_CFG_REG & LCDC_LCDC_DBIB_CFG_REG_ ## _field ## _Msk) >>	\
	LCDC_LCDC_DBIB_CFG_REG_ ## _field ## _Pos)

#define LCDC_DBIB_CMD_REG_GET_FIELD(_field) 									\
	((LCDC->LCDC_DBIB_CMD_REG & LCDC_LCDC_DBIB_CMD_REG_ ## _field ## _Msk) >>	\
	LCDC_LCDC_DBIB_CMD_REG_ ## _field ## _Pos)

/* Min. horizontal frame width required by the host controller */
#define MIPI_DBI_SMARTBOND_MIN_ACTIVE_FRAME_WIDTH    4

/*
 * Timing settings are not required by the MIPI DBI controller. However, the folloing
 * min. timing settings are required. The rest timing settings should be zeroed.
 */
#define MIPI_DBI_SMARTBOND_VSYN_MIN_LEN   1
#define MIPI_DBI_SMARTBOND_HSYN_MIN_LEN   2

struct mipi_dbi_smartbond_timing_config {
	uint16_t vsync_len;
	uint16_t hsync_len;
	uint16_t hfront_porch;
	uint16_t vfront_porch;
	uint16_t vback_porch;
	uint16_t hback_porch;
};

struct mipi_dbi_smartbond_active_frame {
	uint16_t x0;
	uint16_t y0;
	uint16_t x1;
	uint16_t y1;
};

struct mipi_dbi_smartbond_layer_config {
	/*
	 * Base address of the frame to be displayed. Should first be translated
	 * to its physical address.
	 */
	uint32_t frame_address;
	/*
	 * X/Y coordinates of the top-left corner of the layer.
	 * (0, 0) is the top-left corner of the screen.
	 */
	int16_t start_x;
	int16_t start_y;
	/* X/Y resolution of layer in pixels */
	uint16_t size_x;
	uint16_t size_y;
	/* Frame color format */
	uint8_t color_format;
	/* Line to line disctance in bytes of frame in memory */
	int32_t stride;
};

struct mipi_dbi_smartbond_backcolor_config {
	uint8_t red;
	uint8_t green;
	uint8_t blue;
	uint8_t alpha;
};

struct mipi_dbi_smartbond_data {
	/* Provide mutual exclusion when a display operation is requested. */
	struct k_sem device_sem;
	/* Provide synchronization between task return and ISR firing */
	struct k_sem sync_sem;
	/* Flag indicating whether or not an underflow took place */
	bool underflow_flag;
	/* Active frame - used to configure host's timing settings */
	struct mipi_dbi_smartbond_active_frame active_frame;
	/* Layer settings */
	struct mipi_dbi_smartbond_layer_config layer_cfg;
};

struct mipi_dbi_smartbond_config {
	/* Reference to device instance's pinctrl configurations */
	const struct pinctrl_dev_config *pcfg;
	/* Reset GPIO */
	const struct gpio_dt_spec reset;
	/* Host controller's timing settings */
	const struct mipi_dbi_smartbond_timing_config timing_cfg;
	/* Horizontal display resolution in pixels */
	uint16_t dispx;
	/* Vertical display resolution in pixels */
	uint16_t dispy;
};

static inline void lcdc_dbib_cfg_reg_write(uint32_t value)
{
	while (LCDC_STATUS_REG_GET_FIELD(LCDC_DBIB_CMD_FIFO_FULL));
	LCDC->LCDC_DBIB_CFG_REG = value;
}

static int mipi_dbi_smartbond_set_timings(const struct device *dev)
{
	const struct mipi_dbi_smartbond_config *config = dev->config;
	struct mipi_dbi_smartbond_data *data = dev->data;

	uint16_t resx = data->active_frame.x1 - data->active_frame.x0;
	uint16_t resy = data->active_frame.y1 - data->active_frame.y0;
	uint32_t lcdc_resxy_reg = 0;

	// TODO Check if settings are OK
	data->layer_cfg.size_x = resx;
	data->layer_cfg.size_y = resy;


	/* Check if the active frame confronts to host requirements */ // TODO XXX Check if a workaround can be applied. it depends on how the base address should be translated
	if (resx < MIPI_DBI_SMARTBOND_MIN_ACTIVE_FRAME_WIDTH) {
		LOG_ERR("Frame width is less than %d", MIPI_DBI_SMARTBOND_MIN_ACTIVE_FRAME_WIDTH);
		return -1; // TODO: XXX Check what error message should be returned
	}

	LCDC_RESXY_REG_SET_FIELD(LCDC_RES_X, lcdc_resxy_reg, resx);
	LCDC_RESXY_REG_SET_FIELD(LCDC_RES_Y, lcdc_resxy_reg, resy);
	LCDC->LCDC_RESXY_REG = lcdc_resxy_reg;

	resx += config->timing_cfg.hfront_porch;
	resy += config->timing_cfg.vfront_porch;

	LCDC_FRONTPORCHXY_REG_SET_FIELD(LCDC_FPORCH_X, lcdc_resxy_reg, resx);
	LCDC_FRONTPORCHXY_REG_SET_FIELD(LCDC_FPORCH_Y, lcdc_resxy_reg, resy);
	LCDC->LCDC_FRONTPORCHXY_REG = lcdc_resxy_reg;

	resx += config->timing_cfg.hsync_len;
	resy += config->timing_cfg.vsync_len;

	LCDC_BLANKINGXY_REG_SET_FIELD(LCDC_BLANKING_X, lcdc_resxy_reg, resx);
	LCDC_BLANKINGXY_REG_SET_FIELD(LCDC_BLANKING_Y, lcdc_resxy_reg, resy);
	LCDC->LCDC_BLANKINGXY_REG = lcdc_resxy_reg;

	resx += config->timing_cfg.hback_porch;
	resy += config->timing_cfg.vback_porch;

	LCDC_BACKPORCHXY_REG_SET_FIELD(LCDC_BPORCH_X, lcdc_resxy_reg, resx);
	LCDC_BACKPORCHXY_REG_SET_FIELD(LCDC_BPORCH_Y, lcdc_resxy_reg, resy);
	LCDC->LCDC_BACKPORCHXY_REG = lcdc_resxy_reg;

	return 0;
}

static int32_t mipi_dbi_smartbonnd_stride_cal(struct mipi_dbi_smartbond_layer_config *layer)
{
	uint8_t num_of_bytes = 0;

	switch (layer->color_format) {
	case SMARTBOND_MIPI_DBI_L0_RGBA8888:
	case SMARTBOND_MIPI_DBI_L0_ARGB8888:
	case SMARTBOND_MIPI_DBI_L0_ABGR8888:
	case SMARTBOND_MIPI_DBI_L0_BGRA8888:
		num_of_bytes = 4;
		break;
	case SMARTBOND_MIPI_DBI_L0_RGBA5551:
	case SMARTBOND_MIPI_DBI_L0_RGB565:
		num_of_bytes = 2;
		break;
	case SMARTBOND_MIPI_DBI_L0_RGB332:
	case SMARTBOND_MIPI_DBI_L0_L8:
		num_of_bytes = 1;
		break;
	case SMARTBOND_MIPI_DBI_L0_L1:
		num_of_bytes = DIV_ROUND_UP(layer->size_x, 8);
		break;
	case SMARTBOND_MIPI_DBI_L0_L4:
		num_of_bytes = DIV_ROUND_UP(layer->size_y, 4);
		break;
	default:
		__ASSERT_MSG_INFO("Invalid layer format");
	}

	return num_of_bytes * layer->size_x;
}

// TODO PREFETCH LEVEL SHOULD BE ASSIGNED DURING CONFIG
// TODO MAGIC VALUE SHOULD BE CHECKED DURING CONFIG

static void mipi_dbi_smartbond_set_layer(const struct device *dev, bool status)
{
	// XXX TODO To be implemented
	struct mipi_dbi_smartbond_data *data = dev->data;
	uint32_t reg = 0;

	/* Controller's DMA can only handle physical addresses */
	LCDC->LCDC_LAYER0_BASEADDR_REG = black_orca_phy_addr(data->layer_cfg.frame_address);

	LCDC_LAYER0_STARTXY_REG_SET_FIELD(LCDC_L0_START_X, reg, data->layer_cfg.start_x);
	LCDC_LAYER0_STARTXY_REG_SET_FIELD(LCDC_L0_START_Y, reg, data->layer_cfg.start_y);
	LCDC->LCDC_LAYER0_STARTXY_REG = reg;

	LCDC_LAYER0_SIZEXY_REG_SET_FIELD(LCDC_L0_SIZE_X, reg, data->layer_cfg.size_x);
	LCDC_LAYER0_SIZEXY_REG_SET_FIELD(LCDC_L0_SIZE_Y, reg, data->layer_cfg.size_y);
	LCDC->LCDC_LAYER0_SIZEXY_REG = reg;

	LCDC_LAYER0_RESXY_REG_SET_FIELD(LCDC_L0_RES_X, reg, data->layer_cfg.size_x);
	LCDC_LAYER0_RESXY_REG_SET_FIELD(LCDC_L0_RES_Y, reg, data->layer_cfg.size_y);
	LCDC->LCDC_LAYER0_RESXY_REG = reg;

	reg = LCDC->LCDC_LAYER0_STRIDE_REG;
	LCDC_LAYER0_STRIDE_REG_SET_FIELD(LCDC_L0_STRIDE, reg, mipi_dbi_smartbonnd_stride_cal(&data->layer_cfg));
	LCDC->LCDC_LAYER0_STRIDE_REG = reg;

	LCDC_LAYER0_MODE_REG_SET_FIELD(LCDC_L0_EN, reg, status ? 1 : 0);
	LCDC_LAYER0_MODE_REG_SET_FIELD(LCDC_L0_COLOUR_MODE, reg, data->layer_cfg.color_format);
	LCDC->LCDC_LAYER0_MODE_REG = reg;
}

// XXX TODO Add prefecth level is enabled automatically based on the total number of frame buffer ...

static inline void
mipi_dbi_smartbond_set_backcolor(const struct mipi_dbi_smartbond_backcolor_config *backcolor_cfg)
{
	uint32_t lcdc_bgcolor_reg = 0;
	LCDC_BGCOLOR_REG_SET_FIELD(LCDC_BG_RED, lcdc_bgcolor_reg, backcolor_cfg->red);
	LCDC_BGCOLOR_REG_SET_FIELD(LCDC_BG_GREEN, lcdc_bgcolor_reg, backcolor_cfg->green);
	LCDC_BGCOLOR_REG_SET_FIELD(LCDC_BG_BLUE, lcdc_bgcolor_reg, backcolor_cfg->blue);
	LCDC_BGCOLOR_REG_SET_FIELD(LCDC_BG_ALPHA, lcdc_bgcolor_reg, backcolor_cfg->alpha);
	LCDC->LCDC_BGCOLOR_REG = lcdc_bgcolor_reg;
}

static void mipi_dbi_smartbond_send_cmd_data(bool cmd, const uint8_t *buf, size_t len)
{
	for (int i = 0; i < len; i++) {

		/* Workaround for D/C not aligned correctly with CLK. If SPI4 and not using hold and
		 * type changes wait until DBIB FIFO is empty. In any other case wait until there is
		 * some space empty in FIFO.
		 */
		if (LCDC_DBIB_CFG_REG_GET_FIELD(LCDC_DBIB_SPI4_EN) &&
			!LCDC_DBIB_CFG_REG_GET_FIELD(LCDC_DBIB_SPI_HOLD) &&
				LCDC_DBIB_CMD_REG_GET_FIELD(LCDC_DBIB_CMD_SEND) != cmd) {
			while (LCDC_STATUS_REG_GET_FIELD(LCDC_DBIB_CMD_PENDING));
		} else {
			while (LCDC_STATUS_REG_GET_FIELD(LCDC_DBIB_CMD_FIFO_FULL));
		}

		uint32_t lcdc_dbib_cmd_reg = 0;
		LCDC_DBIB_CMD_REG_SET_FIELD(LCDC_DBIB_CMD_SEND, lcdc_dbib_cmd_reg, cmd ? 0x1 : 0x0);
		LCDC_DBIB_CMD_REG_SET_FIELD(LCDC_DBIB_CMD_VAL, lcdc_dbib_cmd_reg, buf[i]);
		LCDC->LCDC_DBIB_CMD_REG = lcdc_dbib_cmd_reg;
	}
}

static void mipi_dbi_smartbond_te_set_status(bool enable, bool te_inversion)
{
	uint32_t lcdc_dbib_cfg_reg = LCDC->LCDC_DBIB_CFG_REG;

	if (enable) {
		LCDC->LCDC_GPIO_REG |= (te_inversion ? LCDC_LCDC_GPIO_REG_LCDC_TE_INV_Msk : 0);
		lcdc_dbib_cfg_reg_write(lcdc_dbib_cfg_reg & ~LCDC_LCDC_DBIB_CFG_REG_LCDC_DBIB_TE_DIS_Msk);
		LCDC->LCDC_INTERRUPT_REG |= (LCDC_LCDC_INTERRUPT_REG_LCDC_TE_IRQ_EN_Msk |
										LCDC_LCDC_INTERRUPT_REG_LCDC_VSYNC_IRQ_EN_Msk);
	} else {
		LCDC->LCDC_INTERRUPT_REG &= ~(LCDC_LCDC_INTERRUPT_REG_LCDC_TE_IRQ_EN_Msk |
										LCDC_LCDC_INTERRUPT_REG_LCDC_VSYNC_IRQ_EN_Msk);
		lcdc_dbib_cfg_reg_write(lcdc_dbib_cfg_reg | LCDC_LCDC_DBIB_CFG_REG_LCDC_DBIB_TE_DIS_Msk);
	}
}

/* Helper function to trigger the LCDC fetching data from frame buffer to the connected display */
static void mipi_dbi_smartbond_send_single_frame(const struct device *dev)
{
	struct mipi_dbi_smartbond_data *data = dev->data;

#if MIPI_DBI_SMARTBOND_IS_TE_ENABLED
	mipi_dbi_smartbond_te_set_status(true, DT_INST_PROP_OR(0, te_polarity, false));
	/*
	 * Wait for the TE signal to be asserted so display's refresh status can be synchronized with
	 * the current frame update.
	 */
	k_sem_take(&data->sync_sem, K_FOREVER);
#endif

	// TODO XXX Check if other signals are more suitable e.g. LCDC_FRAME_END_IRQ_EN
	LCDC->LCDC_INTERRUPT_REG |= LCDC_LCDC_INTERRUPT_REG_LCDC_VSYNC_IRQ_EN_Msk;

	/* Setting this bit will enable the host to start outputing pixel data */
	LCDC->LCDC_MODE_REG |= LCDC_LCDC_MODE_REG_LCDC_SFRAME_UPD_Msk;

	/* Wait for frame to be sent */
	k_sem_take(&data->sync_sem, K_FOREVER);
}

#if MIPI_DBI_SMARTBOND_IS_RESET_REQUIRED
static int mipi_dbi_smnartbond_reset(const struct device *dev, uint32_t delay)
{
	const struct mipi_dbi_smartbond_config *config = dev->config;
	int ret;

	if (config->reset.port == NULL) {
		LOG_INF("Reset signal is not defined, exiting the reset routine");
		return -ENOTSUP;
	}

	ret = gpio_pin_set_dt(&config->reset, 1);
	if (ret < 0) {
		return ret;
	}
	k_msleep(delay);

	return gpio_pin_set_dt(&config->reset, 0);
}
#endif

// TODO: Currently there is no way to buffer properties other than checking if command concerns partial update
static void mipi_dbi_smartbond_timings_is_update_required(const struct device *dev, uint32_t cmd,
							const uint8_t *data_buf, size_t len)
{
	struct mipi_dbi_smartbond_data *data = dev->data;

	if (cmd == MIPI_DCS_SET_COLUMN_ADDRESS) {
		__ASSERT(len == 4, "Invalid data length");
		data->active_frame.x0 = ((data_buf[0] << 8) | data_buf[1]);
		data->active_frame.x1 = ((data_buf[2] << 8) | data_buf[3]) + 1;
	} else if (cmd == MIPI_DCS_SET_PAGE_ADDRESS) {
		__ASSERT(len == 4, "Invalid data length");
		data->active_frame.y0 = ((data_buf[0] << 8) | data_buf[1]);
		data->active_frame.y1 = ((data_buf[2] << 8) | data_buf[3]) + 1;
	} else {
		goto _exit;
	}

	mipi_dbi_smartbond_set_timings(dev);

_exit:
	return;
}

static uint8_t mipi_dbi_smartbond_set_pixfmt(uint32_t mipi_dbi_pixfmt)
{
	uint8_t dbi_fmt = 0;

	switch (mipi_dbi_pixfmt) {
	case MIPI_DCS_PIXEL_FORMAT_24BIT:
		dbi_fmt = SMARTBOND_MIPI_DBI_P_RGB888;
		break;
	case MIPI_DCS_PIXEL_FORMAT_18BIT:
		dbi_fmt = SMARTBOND_MIPI_DBI_P_RGB666;
		break;
	case MIPI_DCS_PIXEL_FORMAT_16BIT:
		dbi_fmt = SMARTBOND_MIPI_DBI_P_RGB565;
		break;
	case MIPI_DCS_PIXEL_FORMAT_12BIT:
		dbi_fmt = SMARTBOND_MIPI_DBI_P_RGB444;
		break;
	case MIPI_DCS_PIXEL_FORMAT_8BIT:
		dbi_fmt = SMARTBOND_MIPI_DBI_P_RGB332;
		break;
	default:
		__ASSERT_MSG_INFO("Inavlid pixel color format");
	}

	return dbi_fmt;
}

static int mipi_dbi_smartbond_command_write(const struct device *dev,
											const struct mipi_dbi_config *dbi_config,
											uint8_t cmd, const uint8_t *data_buf,
											size_t len)
{
	struct mipi_dbi_smartbond_data *data = dev->data;

	__ASSERT(cmd != NULL, "Missing command");

	k_sem_take(&data->device_sem, K_FOREVER);

	uint32_t lcdc_dbib_cfg_reg = LCDC->LCDC_DBIB_CFG_REG;

	switch (dbi_config->mode) {
	case MIPI_DBI_MODE_SPI_3WIRE:
		LCDC_DBIB_CFG_REG_SET_FIELD(LCDC_DBIB_SPI3_EN, lcdc_dbib_cfg_reg, 1);
		LCDC_DBIB_CFG_REG_SET_FIELD(LCDC_DBIB_SPI4_EN, lcdc_dbib_cfg_reg, 0);
		break;
	case MIPI_DBI_MODE_SPI_4WIRE:
		LCDC_DBIB_CFG_REG_SET_FIELD(LCDC_DBIB_SPI4_EN, lcdc_dbib_cfg_reg, 1);
		LCDC_DBIB_CFG_REG_SET_FIELD(LCDC_DBIB_SPI3_EN, lcdc_dbib_cfg_reg, 0);
		break;
	default:
		__ASSERT_MSG_INFO("Invalid mode");
		return -EIO;
	}

	LCDC_DBIB_CFG_REG_SET_FIELD(LCDC_DBIB_FMT, lcdc_dbib_cfg_reg,
					mipi_dbi_smartbond_set_pixfmt(dbi_config->pixfmt));

	lcdc_dbib_cfg_reg_write(lcdc_dbib_cfg_reg);

	/* First send the requested command */
	mipi_dbi_smartbond_send_cmd_data(true, &cmd, 1);

	if (len >= 1) {
		if (cmd == MIPI_DCS_WRITE_MEMORY_START) {
			// TODO XXX Need to check if displays color format (capabilities - current color should be retrived and so the child dev should be retrived somehow - maybe via a config?)
			// TODO: Need to check if buffer size is in line with active frame and abort write if less than the active frame is provdided

			data->layer_cfg.frame_address = (uint32_t)data_buf;

			mipi_dbi_smartbond_set_layer(dev, true);
			/*
			* If command indicates frame update then trigger the host
			* to do so via its DMA accelerator.
			*/
			mipi_dbi_smartbond_send_single_frame(dev);
		} else {
			/* Otherwise, data should be transmitted via the DBIB interface */
			mipi_dbi_smartbond_send_cmd_data(false, data_buf, len);

			// TODO: Current workaround to get buffer properties
			mipi_dbi_smartbond_timings_is_update_required(dev, cmd, data_buf, len);
		}
	}

	k_sem_give(&data->device_sem);

	return 0;
}

// TODO Check if this will be supported; need to keep track the command whether this concerns pixel data or register data
static int mipi_dbi_smartbond_write_transfer(const struct device *dev,
											const struct mipi_dbi_config *dbi_config,
											const uint8_t *data_buf, size_t len)
{
	// TODO XXX Need to implement a logic where the base address is evaluated and split transfers into lines ...
	return -ENOSYS;
}

static void mipi_dbi_smartbond_set_status(bool enable)
{
	unsigned int key;
	uint32_t clk_sys_reg;

	/* Globally disable interrupts as some registers may also be used by other devices */
	key = irq_lock();
	clk_sys_reg = CRG_SYS->CLK_SYS_REG;

	if (enable) {
		CLK_SYS_REG_SET_FIELD(LCD_CLK_SEL, clk_sys_reg, MIPI_DBI_SMARTBOND_IS_PLL_REQUIRED ? 1 : 0);
		CLK_SYS_REG_SET_FIELD(LCD_ENABLE, clk_sys_reg, 1);

		irq_enable(SMARTBOND_IRQN);
	} else {
		/* XXX TODO Check is the status reg should be ecercise to check if lcdc is inactive  */

		/* Forcefully reset the host controller */
		CRG_SYS->CLK_SYS_REG |= CRG_SYS_CLK_SYS_REG_LCD_RESET_REQ_Msk;
		CLK_SYS_REG_SET_FIELD(LCD_RESET_REQ, clk_sys_reg, 0);
		CLK_SYS_REG_SET_FIELD(LCD_CLK_SEL, clk_sys_reg, 0);
		CLK_SYS_REG_SET_FIELD(LCD_ENABLE, clk_sys_reg, 0);

		irq_disable(SMARTBOND_IRQN);
	}

	CRG_SYS->CLK_SYS_REG = clk_sys_reg;
	irq_unlock(key);
}

static void mipi_dbi_smartbond_configure(const struct device *dev)
{
	uint32_t lcdc_dbib_cfg_reg;
	uint8_t clk_div =
		SMARTBOND_MIPI_DBI_CLK_DIV(DT_PROP(DT_CHOSEN(zephyr_display), spi_max_frequency));
	static struct mipi_dbi_smartbond_backcolor_config backcolor_cfg = {
		0xFF, 0xFF, 0xFF, 0x00
	};

	/* First enable the controller so registers can be updated TODO: XXX Check if this is indeed a prerequisite */
	mipi_dbi_smartbond_set_status(true);

	/*
	 * Setup the correct clock divider to achieve the requested pixel speed. In MIPI DBI interface the clock
	 * is further divded by two. Source of this divider is the main clock of the host controller and the
	 * generated period is defined as (LCDC_CLKCTRL_REG + 1) x host controller period.
	 */
	LCDC_CLKCTRL_REG_SET_FIELD(LCDC_CLK_DIV, LCDC->LCDC_CLKCTRL_REG,
		(clk_div >= 2 ? clk_div / 2 - 1 : (clk_div == 1 ? clk_div - 1 : clk_div)));

	lcdc_dbib_cfg_reg = LCDC->LCDC_DBIB_CFG_REG;
	LCDC_DBIB_CFG_REG_SET_FIELD(LCDC_DBIB_DMA_EN, lcdc_dbib_cfg_reg, 1);
	LCDC_DBIB_CFG_REG_SET_FIELD(LCDC_DBIB_RESX, lcdc_dbib_cfg_reg, 1);
	lcdc_dbib_cfg_reg_write(lcdc_dbib_cfg_reg);

	/* TE will be handled per frame update */
	mipi_dbi_smartbond_te_set_status(false, DT_INST_PROP_OR(0, te_polarity, false));
	/* Setup the timing generator */
	mipi_dbi_smartbond_set_timings(dev);
	/* Setup the layer (L0) controller */
	mipi_dbi_smartbond_set_layer(dev, false);
	/* Initialize the layer background color */
	mipi_dbi_smartbond_set_backcolor(&backcolor_cfg);
}

static void smartbond_mipi_dbi_isr(const void *arg)
{
	struct mipi_dbi_smartbond_data *data =  ((const struct device *)arg)->data;

	data->underflow_flag = LCDC_STATUS_REG_GET_FIELD(LCDC_STICKY_UNDERFLOW);
	if (data->underflow_flag) {
		/* Underflow sticky bit will remain high until cleared by writing any value to LCDC_INTERRUPT_REG. */
		uint32_t lcdc_interruput_reg = LCDC->LCDC_INTERRUPT_REG;
		LCDC->LCDC_INTERRUPT_REG = lcdc_interruput_reg;
	}

	/* Clear interrupt source here otherwise the ISR will keep entering infinitely */
	mipi_dbi_smartbond_te_set_status(false, DT_INST_PROP_OR(0, te_polarity, false));

	k_sem_give(&data->sync_sem);
}

// TODO: XXX Add magic value check

static int mipi_dbi_smartbond_init(const struct device *dev)
{
	const struct mipi_dbi_smartbond_config *config = dev->config;
	struct mipi_dbi_smartbond_data *data = dev->data;
	int ret;

	/* Device should be ready to be acquired */
	k_sem_init(&data->device_sem, 1, 1);
	/* Event should be signaled by LCDC ISR */
	k_sem_init(&data->sync_sem, 0, 1);

	/* Select default state at initialization time. */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("Could not apply MIPI DBI pins' default state (%d)", ret);
		return ret;
	}

#if MIPI_DBI_SMARTBOND_IS_RESET_REQUIRED
	if (config->reset.port) {
		 ret = gpio_pin_configure_dt(&config->reset, GPIO_OUTPUT_INACTIVE);
		 if (ret < 0) {
			LOG_ERR("COuld not configure reset GPIO (%d)", ret);
			return ret;
		 }
	}
#endif

#if MIPI_DBI_SMARTBOND_IS_PLL_REQUIRED
	const struct device *clock_dev = DEVICE_DT_GET(DT_NODELABEL(osc));

	if (!device_is_ready(clock_dev)) {
		__ASSERT_MSG_INFO("Clock device is not ready");
	}

	/* Check at build time is PLL is required to achieve the requested host speed. */
	ret = z_smartbond_select_sys_clk(SMARTBOND_CLK_PLL96M);
	if (ret < 0) {
		LOG_ERR("Could not switch to PLL");
		return ret;
	}
#endif

	mipi_dbi_smartbond_configure(dev);

	IRQ_CONNECT(SMARTBOND_IRQN, SMARTBOND_IRQ_PRIO, smartbond_mipi_dbi_isr,
													DEVICE_DT_INST_GET(0), 0);
	return 0;
}

static struct mipi_dbi_driver_api mipi_dbi_smartbond_driver_api = {
#if MIPI_DBI_SMARTBOND_IS_RESET_REQUIRED
	.reset = mipi_dbi_smnartbond_reset,
#endif
	.command_write = mipi_dbi_smartbond_command_write,
	.write_transfer = mipi_dbi_smartbond_write_transfer,
	/*
	 * Read operations are not supported natively by the host. This can be emulated via the
	 * standard SPI interface in the future.
	 */
};

// TODO POST_KERNEL SHOULD BE CHANGED AS THE CLOCK MANAGER SHOULD ALREADY UP AND RUNNING
#define SMARTBOND_MIPI_DBI_INIT(inst)                										\
	/* Define all pinctrl configuration for instance */ 	    							\
	PINCTRL_DT_INST_DEFINE(inst);                               							\
                                                                							\
	static const struct mipi_dbi_smartbond_config mipi_dbi_smartbond_config_## inst = {     \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),           							\
		.reset = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {}),  							\
		/* Min. required vertical blanking */												\
		.timing_cfg.vsync_len = MIPI_DBI_SMARTBOND_VSYN_MIN_LEN,							\
		/* Min. required horizontal blanking */												\
		.timing_cfg.hsync_len = MIPI_DBI_SMARTBOND_HSYN_MIN_LEN,							\
		.timing_cfg.hfront_porch = 0,													    \
		.timing_cfg.vfront_porch = 0,														\
		.timing_cfg.hback_porch = 0,														\
		.timing_cfg.vback_porch = 0,														\
		.dispx = DT_PROP(DT_CHOSEN(zephyr_display), width),									\
		.dispy = DT_PROP(DT_CHOSEN(zephyr_display), height), 								\
	};																						\
													 										\
	static struct mipi_dbi_smartbond_data mipi_dbi_smartbond_data_## inst = {				\
		.active_frame.x0 = 0,																\
		.active_frame.y0 = 0,																\
		.active_frame.x1 = DT_PROP(DT_CHOSEN(zephyr_display), width),	  					\
		.active_frame.y1 = DT_PROP(DT_CHOSEN(zephyr_display), height),						\
		.layer_cfg.start_x = 0,																\
		.layer_cfg.start_y = 0,																\
		.layer_cfg.size_x = DT_PROP(DT_CHOSEN(zephyr_display), width) ,                     \
		.layer_cfg.size_y = DT_PROP(DT_CHOSEN(zephyr_display), height),                     \
		.layer_cfg.color_format = DT_INST_PROP(inst, layer_format),							\
	}; 																						\
                                                                \
	DEVICE_DT_INST_DEFINE(inst, mipi_dbi_smartbond_init, NULL,  \
						&mipi_dbi_smartbond_data_## inst,		\
						&mipi_dbi_smartbond_config_## inst, 	\
						POST_KERNEL, 							\
						CONFIG_MIPI_DBI_INIT_PRIORITY, 			\
						&mipi_dbi_smartbond_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SMARTBOND_MIPI_DBI_INIT)
