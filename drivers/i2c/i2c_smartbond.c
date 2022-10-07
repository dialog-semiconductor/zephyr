/*
 * Copyright (c) 2022 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_smartbond_i2c

#include <errno.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <DA1469xAB.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(i2c_smartbond, CONFIG_I2C_LOG_LEVEL);

#define I2C_REG_SETF(reg, reg_name, field_name, val) \
        reg  = ((reg & ~(I2C_##reg_name##_REG_##field_name##_Msk)) | \
        ((I2C_##reg_name##_REG_##field_name##_Msk) & ((val) << (I2C_##reg_name##_REG_##field_name##_Pos))))

#define I2C_REG_GETF(reg, reg_name, field_name) \
        ((reg & (I2C_##reg_name##_REG_##field_name##_Msk)) >> (I2C_##reg_name##_REG_##field_name##_Pos))

#define SDK_CLK_CNT

//#define CONFIG_I2C_INTERRUPT_DRIVEN

struct i2c_smartbond_cfg {
	I2C_Type *regs;
	int periph_clock_config;
	const struct pinctrl_dev_config *pcfg;
};

struct i2c_smartbond_data {
    uint32_t dev_config;
    struct k_spinlock lock;
};

static int i2c_smartbond_configure(const struct device *dev, uint32_t dev_config)
{
    const struct i2c_smartbond_cfg *config = dev->config;
	struct i2c_smartbond_data *data = (struct i2c_smartbond_data *const)(dev)->data;
    k_spinlock_key_t key;

    key = k_spin_lock(&data->lock);

    /* Configure Speed (SCL frequency) */
    switch(I2C_SPEED_GET(dev_config)){
    case I2C_SPEED_STANDARD:
        I2C_REG_SETF(config->regs->I2C_CON_REG, I2C_CON, I2C_SPEED, 1U);
        break;
    case I2C_SPEED_FAST:
        I2C_REG_SETF(config->regs->I2C_CON_REG, I2C_CON, I2C_SPEED, 2UL);
        break;
    case I2C_SPEED_HIGH:
        I2C_REG_SETF(config->regs->I2C_CON_REG, I2C_CON, I2C_SPEED, 3UL);
        break;
    default:
        return -ENOTSUP;
    }

    /* Configure Mode */
	if ((dev_config & I2C_MODE_CONTROLLER) == I2C_MODE_CONTROLLER) {
        I2C_REG_SETF(config->regs->I2C_CON_REG, I2C_CON, I2C_MASTER_MODE, 1);
        I2C_REG_SETF(config->regs->I2C_CON_REG, I2C_CON, I2C_SLAVE_DISABLE, 1);
	}
    else{
        LOG_ERR("Only I2C Controller mode supported");
		return -ENOTSUP;
    }
    
    /* Configure addressing mode */
    I2C_REG_SETF(config->regs->I2C_CON_REG, I2C_CON, I2C_10BITADDR_MASTER, 0);

    /* Enable sending RESTART as master */
    I2C_REG_SETF(config->regs->I2C_CON_REG, I2C_CON, I2C_RESTART_EN, 1);

    while(!!(I2C_REG_GETF(config->regs->I2C_STATUS_REG, I2C_STATUS, MST_ACTIVITY))) {}

	k_spin_unlock(&data->lock, key);

    data->dev_config = dev_config;

    return 0;
}

static int i2c_smartbond_poll_read(const struct i2c_smartbond_cfg *const config,
                                    struct i2c_msg *const msg)
{
    size_t nn = 0;
    uint32_t rr = 0;

    if(!msg->buf || msg->len == 0){
        return -EINVAL;
    }
    else{
        while(nn < msg->len){
            while (rr < msg->len && (!!I2C_REG_GETF(config->regs->I2C_STATUS_REG, I2C_STATUS, TFNF))) {
                rr++;
                config->regs->I2C_DATA_CMD_REG = I2C_I2C_DATA_CMD_REG_I2C_CMD_Msk |
                                ((rr == msg->len && (msg->flags & I2C_MSG_STOP)) ?
                                        I2C_I2C_DATA_CMD_REG_I2C_STOP_Msk : 0) |
                                ((rr == 1 && (msg->flags & I2C_MSG_RESTART)) ?
                                        I2C_I2C_DATA_CMD_REG_I2C_RESTART_Msk : 0);
            }
            while (nn < msg->len && I2C_REG_GETF(config->regs->I2C_RXFLR_REG, I2C_RXFLR, RXFLR)) {
                    msg->buf[nn] = I2C_REG_GETF(config->regs->I2C_DATA_CMD_REG, I2C_DATA_CMD, I2C_DAT);
                    nn++;
            }

            if(config->regs->I2C_TX_ABRT_SOURCE_REG & 0x1FFFF){
                return -EIO;
            }
        }
    }
    return 0;
}

static int i2c_smartbond_poll_write(const struct i2c_smartbond_cfg *const config,
                                    struct i2c_msg *const msg)
{
    uint32_t len = msg->len;

    if(!msg->buf || msg->len == 0){
        return -EINVAL;
    }
    else{
        size_t offst = 0;
        while(len--){
            while(!I2C_REG_GETF(config->regs->I2C_STATUS_REG, I2C_STATUS, TFNF));
            config->regs->I2C_DATA_CMD_REG = msg->buf[offst] |
                (((len == 0) && (msg->flags & I2C_MSG_STOP)) ?
                        I2C_I2C_DATA_CMD_REG_I2C_STOP_Msk : 0) |
                (((offst == 0) && (msg->flags & I2C_MSG_RESTART)) ?
                        I2C_I2C_DATA_CMD_REG_I2C_RESTART_Msk : 0);
            offst++;
            if(config->regs->I2C_TX_ABRT_SOURCE_REG & 0x1FFFF){
                return -EIO;
            }
        }
    }
    return 0;
}

static int i2c_smartbond_transfer(const struct device *dev, 
                                struct i2c_msg *msgs,
                                uint8_t num_msgs,
                                uint16_t addr)
{
    const struct i2c_smartbond_cfg *config = dev->config;
    struct i2c_smartbond_data *data = (struct i2c_smartbond_data *const)(dev)->data;
    struct i2c_msg *current, *next;
    k_spinlock_key_t key;
    int ret = 0;

    current = msgs;

    for(uint8_t i = 1; i <= num_msgs; i++) {
        if (i < num_msgs) {
			next = current + 1;

			if ((current->flags & I2C_MSG_RW_MASK) != (next->flags & I2C_MSG_RW_MASK)) {
                current->flags |= I2C_MSG_RESTART;
			}

			if (current->flags & I2C_MSG_STOP) {
				ret = -EINVAL;
				break;
			}
		} else {
            current->flags |= I2C_MSG_STOP;
		}

		current++;
    }

	if (ret) {
		return ret;
	}
     
    key = k_spin_lock(&data->lock);

    if (!!I2C_REG_GETF(config->regs->I2C_ENABLE_STATUS_REG, I2C_ENABLE_STATUS, IC_EN)) {
        /* Wait for the master to become IDLE */
        while (!!I2C_REG_GETF(config->regs->I2C_STATUS_REG, I2C_STATUS, MST_ACTIVITY));

        /* Now is safe to disable the I2C to change the Target Address */
        I2C_REG_SETF(config->regs->I2C_ENABLE_REG, I2C_ENABLE, I2C_EN, 0);
    }
    /* Change the Target Address */
    I2C_REG_SETF(config->regs->I2C_TAR_REG, I2C_TAR, IC_TAR, addr);

    /* Enable again the I2C to use the new address */
    I2C_REG_SETF(config->regs->I2C_ENABLE_REG, I2C_ENABLE, I2C_EN, 1);

    /* Wait for the master to become IDLE */
    while (!!I2C_REG_GETF(config->regs->I2C_STATUS_REG, I2C_STATUS, MST_ACTIVITY)) {}

	for (; num_msgs > 0; num_msgs--, msgs++) {

		if ((msgs->flags & I2C_MSG_RW_MASK) == I2C_MSG_READ) {
            ret = i2c_smartbond_poll_read(config, msgs);
		} else {
            ret = i2c_smartbond_poll_write(config, msgs);
		}

		if (ret < 0) {
			break;
		}
	}

    k_spin_unlock(&data->lock, key);

    return ret;
}

static int i2c_smartbond_init(const struct device *dev)
{
    const struct i2c_smartbond_cfg *config = dev->config;
    int ret;

    ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
        LOG_ERR("Failed to configure I2C pins");
		return ret;
	}
    
    /* Reset I2C CLK_SEL */
    CRG_COM->RESET_CLK_COM_REG = (config->periph_clock_config << 1);

    /* Set I2C CLK ENABLE */
    CRG_COM->SET_CLK_COM_REG = config->periph_clock_config;

    /* Reset interrupt mask */
    config->regs->I2C_INTR_MASK_REG = 0x0000U;

    /* Disable I2C Controller */
    I2C_REG_SETF(config->regs->I2C_ENABLE_REG, I2C_ENABLE, I2C_EN, 0);
    while(!!I2C_REG_GETF(config->regs->I2C_ENABLE_STATUS_REG, I2C_ENABLE_STATUS, IC_EN));
    
    /* Set values for I2C Clock (SCL) */
#ifdef SDK_CLK_CNT
    config->regs->I2C_SS_SCL_HCNT_REG = 0x90;
    config->regs->I2C_SS_SCL_LCNT_REG = 0x9E;
    
    config->regs->I2C_FS_SCL_HCNT_REG = 0x10;
    config->regs->I2C_FS_SCL_LCNT_REG = 0x2E;

    config->regs->I2C_HS_SCL_HCNT_REG = 0x06;
    config->regs->I2C_HS_SCL_LCNT_REG = 0x10;
#else
    config->regs->I2C_SS_SCL_HCNT_REG = 0x91;
    config->regs->I2C_SS_SCL_LCNT_REG = 0xAB;
    
    config->regs->I2C_FS_SCL_HCNT_REG = 0x1A;
    config->regs->I2C_FS_SCL_LCNT_REG = 0x32;

    config->regs->I2C_HS_SCL_HCNT_REG = 0x06;
    config->regs->I2C_HS_SCL_LCNT_REG = 0x10;
#endif

    /* Set high-speed master code */
    I2C_REG_SETF(config->regs->I2C_HS_MADDR_REG, I2C_HS_MADDR, I2C_IC_HS_MAR, 0x01);
    
	return i2c_smartbond_configure(dev, I2C_MODE_CONTROLLER | I2C_SPEED_SET(I2C_SPEED_HIGH));
}

static void i2c_smartbond_isr(const struct device *dev)
{
    const struct i2c_smartbond_cfg *config = dev->config;
    struct i2c_smartbond_data *data = dev->data;
    
}

#ifdef CONFIG_I2C_INTERRUPT_DRIVEN
#define I2C_SMARTBOND_CONFIGURE(id)			\
    IRQ_CONNECT(DT_INST_IRQN(id),		\
            DT_INST_IRQ(id, priority),	\
            i2c_smartbond_isr,		\
            DEVICE_DT_INST_GET(id), 0);	\
    irq_enable(DT_INST_IRQN(id));
#else
#define I2C_SMARTBOND_CONFIGURE(id)
#endif

static const struct i2c_driver_api i2c_smartbond_driver_api = {
	.configure = i2c_smartbond_configure,
	.transfer = i2c_smartbond_transfer,
};

#define I2C_SMARTBOND_DEVICE(id)   \
    PINCTRL_DT_INST_DEFINE(id);     \
	static const struct i2c_smartbond_cfg i2c_smartbond_##id##_cfg = {		       \
		.regs = (I2C_Type *)DT_INST_REG_ADDR(id),			      \
        .periph_clock_config = DT_INST_PROP(id, periph_clock_config),			\
        .pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(id),	    \
	};								       \
	static struct i2c_smartbond_data i2c_smartbond_##id##_data;  \
    static int i2c_smartbond_##id##_init(const struct device *dev)				\
	{											\
		int ret = i2c_smartbond_init(dev);						\
        I2C_SMARTBOND_CONFIGURE(id);							\
        return ret;                             \
	}											\
	I2C_DEVICE_DT_INST_DEFINE(id,								\
			      i2c_smartbond_##id##_init,					\
			      NULL,								\
			      &i2c_smartbond_##id##_data,					\
			      &i2c_smartbond_##id##_cfg,					\
			      POST_KERNEL, CONFIG_I2C_INIT_PRIORITY,			\
			      &i2c_smartbond_driver_api);					\

DT_INST_FOREACH_STATUS_OKAY(I2C_SMARTBOND_DEVICE)
