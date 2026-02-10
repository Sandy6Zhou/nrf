/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_gsensor.c
**文件描述:        G-Sensor 管理模块实现文件 (LSM6DSV16X)
**当前版本:        V1.0
**作    者:        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期:        2026.01.15
*********************************************************************
** 功能描述:        1. 实现 LSM6DSV16X 的 I2C 初始化与数据读取
**                 2. 实现电源控制逻辑 (P2.6)
**                 3. 使用 ST 官方 STdC 驱动库
*********************************************************************/

#include "my_comm.h"
#include "lsm6dsv16x_reg.h"

/* 注册 G-Sensor 模块日志 */
LOG_MODULE_REGISTER(my_gsensor, LOG_LEVEL_INF);

/* 从设备树获取硬件配置 */
#define I2C_NODE DT_ALIAS(gsensor_i2c)
static const struct device *i2c_dev = DEVICE_DT_GET(I2C_NODE);

#define GSENSOR_PWR_NODE DT_ALIAS(gsensor_pwr_ctrl)
static const struct gpio_dt_spec gsensor_pwr_gpio = GPIO_DT_SPEC_GET(GSENSOR_PWR_NODE, gpios);

static const struct gpio_dt_spec gsen_int = GPIO_DT_SPEC_GET(DT_ALIAS(gsensor_int), gpios);
static struct gpio_callback gsensor_gpio_cb;

/* LSM6DSV16X I2C 从机地址 */
#define MY_LSM6DSV16X_I2C_ADDR 0x6A

/* LSM6DSV16X 芯片 ID */
#define MY_LSM6DSV16X_ID 0x71

/* STdC 接口实现 */
static int32_t lsm6dsv16x_write(void *handle, uint8_t reg, const uint8_t *data, uint16_t len)
{
    const struct device *dev = (const struct device *)handle;
    return i2c_burst_write(dev, MY_LSM6DSV16X_I2C_ADDR, reg, data, len);
}

static int32_t lsm6dsv16x_read(void *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    const struct device *dev = (const struct device *)handle;
    return i2c_burst_read(dev, MY_LSM6DSV16X_I2C_ADDR, reg, data, len);
}

static stmdev_ctx_t lsm_ctx = {
    .write_reg = lsm6dsv16x_write,
    .read_reg = lsm6dsv16x_read,
    .mdelay = k_msleep,
    .handle = (void *)DEVICE_DT_GET(I2C_NODE),
};

/* 当前识别到的传感器类型 (仅支持 LSM6DSV16X) */
static bool sensor_ready = false;

/* 消息队列定义 */
K_MSGQ_DEFINE(my_gsensor_msgq, sizeof(MSG_S), 10, 4);

/* 线程数据与栈定义 */
K_THREAD_STACK_DEFINE(my_gsensor_task_stack, MY_GSENSOR_TASK_STACK_SIZE);
static struct k_thread my_gsensor_task_data;

/********************************************************************
**函数名称:  my_gsensor_task
**入口参数:  无
**出口参数:  无
**函数功能:  G-Sensor 模块主线程，处理来自消息队列的任务
**返 回 值:  无
*********************************************************************/
static void my_gsensor_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    MSG_S msg;

    LOG_INF("G-Sensor thread started");

    for (;;)
    {
        my_recv_msg(&my_gsensor_msgq, (void *)&msg, sizeof(MSG_S), K_FOREVER);
        
        switch (msg.msgID)
        {
            case MY_MSG_GSENSOR_READ:
            {
                /* 读取六轴数据 */
                if (sensor_ready)
                {
                    int16_t acc_raw[3], gyro_raw[3];
                    
                    /* 读取加速度计数据 */
                    if (lsm6dsv16x_acceleration_raw_get(&lsm_ctx, acc_raw) == 0)
                    {
                        LOG_INF("ACC: X=%6d, Y=%6d, Z=%6d", acc_raw[0], acc_raw[1], acc_raw[2]);
                    }
                    else
                    {
                        LOG_ERR("Failed to read accelerometer");
                    }
                    
                    /* 读取陀螺仪数据 */
                    if (lsm6dsv16x_angular_rate_raw_get(&lsm_ctx, gyro_raw) == 0)
                    {
                        LOG_INF("GYR: X=%6d, Y=%6d, Z=%6d", gyro_raw[0], gyro_raw[1], gyro_raw[2]);
                    }
                    else
                    {
                        LOG_ERR("Failed to read gyroscope");
                    }
                }
                else
                {
                    LOG_WRN("Sensor not ready");
                }
                break;
            }
            
            default:
                break;
        }
    }
}

/********************************************************************
**函数名称:  lsm6dsv16x_check_id
**入口参数:  无
**出口参数:  无
**函数功能:  检查 LSM6DSV16X 是否在线
**返 回 值:  true 表示识别成功
*********************************************************************/
static bool lsm6dsv16x_check_id(void)
{
    uint8_t chip_id = 0;

    if (lsm6dsv16x_device_id_get(&lsm_ctx, &chip_id) == 0)
    {
        if (chip_id == MY_LSM6DSV16X_ID)
        {
            LOG_INF("LSM6DSV16X identified ID: 0x%02X", chip_id);
            return true;
        }
    }

    return false;
}

int my_gsensor_pwr_on(bool on)
{
    LOG_INF("GSENSOR Power Control: %s", on ? "Power ON" : "Power OFF");
    return gpio_pin_set_dt(&gsensor_pwr_gpio, on ? 1 : 0);
}

int my_gsensor_read_data(struct gsensor_data *data)
{
    int16_t data_raw[3];
    
    if (!sensor_ready)
    {
        return -ENODEV;
    }

    if (lsm6dsv16x_acceleration_raw_get(&lsm_ctx, data_raw) == 0)
    {
        data->x = data_raw[0];
        data->y = data_raw[1];
        data->z = data_raw[2];
        return 0;
    }
    
    return -EIO;
}

static void gsensor_gpio_isr(const struct device *dev,
                          struct gpio_callback *cb,
                          uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    int level;

    if (pins & BIT(gsen_int.pin))
    {
        level = gpio_pin_get_dt(&gsen_int);
        // LOG_INF("gsen_int:%d", level);
        //TODO 发消息给ctrl去再次获取该引脚电平状态，以及消抖处理
    }
}

int my_gsensor_init(k_tid_t *tid)
{
    int err;

    /* 1. 检查硬件接口是否就绪 */
    if (!device_is_ready(i2c_dev) ||
        !device_is_ready(gsen_int.port) ||
        !device_is_ready(gsensor_pwr_gpio.port))
    {
        LOG_ERR("I2C device not ready");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&gsensor_pwr_gpio))
    {
        LOG_ERR("GSENSOR Power GPIO not ready");
        return -ENODEV;
    }

    /* 2. 配置电源引脚并开启电源 */
    err = gpio_pin_configure_dt(&gsensor_pwr_gpio, GPIO_OUTPUT_ACTIVE);
    if (err)
    {
        LOG_ERR("Failed to configure GSENSOR Power GPIO (err %d)", err);
        return err;
    }
    
    /* 2.1. 配置中断引脚 */
    err = gpio_pin_configure_dt(&gsen_int, GPIO_INPUT);
    if (err)
    {
        LOG_ERR("Failed to configure GSENSOR interrupt GPIO input mode (err %d)", err);
        return err;
    }
    err = gpio_pin_interrupt_configure_dt(&gsen_int, GPIO_INT_EDGE_RISING);
    if (err)
    {
        LOG_ERR("Failed to configure GSENSOR Interrupt GPIO (err %d)", err);
        return err;
    }

    gpio_init_callback(&gsensor_gpio_cb, gsensor_gpio_isr, BIT(gsen_int.pin));
    gpio_add_callback(gsen_int.port, &gsensor_gpio_cb);

    /* 等待芯片上电稳定 */
    k_sleep(K_MSEC(50));

    /* 3. 识别 LSM6DSV16X 传感器 */
    if (lsm6dsv16x_check_id())
    {
        sensor_ready = true;
        LOG_INF("GSENSOR identified as LSM6DSV16X");

        /* 初始化 LSM6DSV16X */
        lsm6dsv16x_reset_set(&lsm_ctx, LSM6DSV16X_GLOBAL_RST);
        k_sleep(K_MSEC(30));
        lsm6dsv16x_block_data_update_set(&lsm_ctx, PROPERTY_ENABLE);
        
        /* 配置加速度计：120Hz, 2g */
        lsm6dsv16x_xl_data_rate_set(&lsm_ctx, LSM6DSV16X_ODR_AT_120Hz);
        lsm6dsv16x_xl_full_scale_set(&lsm_ctx, LSM6DSV16X_2g);
        
        /* 配置陀螺仪：120Hz, 250dps */
        lsm6dsv16x_gy_data_rate_set(&lsm_ctx, LSM6DSV16X_ODR_AT_120Hz);
        lsm6dsv16x_gy_full_scale_set(&lsm_ctx, LSM6DSV16X_250dps);
    }
    else
    {
        LOG_ERR("LSM6DSV16X not detected");
        return -ENODEV;
    }

    /* 4. 初始化消息队列 */
    my_init_msg_handler(MOD_GSENSOR, &my_gsensor_msgq);

    /* 5. 启动 G-Sensor 线程 */
    *tid = k_thread_create(&my_gsensor_task_data, my_gsensor_task_stack,
                           K_THREAD_STACK_SIZEOF(my_gsensor_task_stack),
                           my_gsensor_task, NULL, NULL, NULL,
                           MY_GSENSOR_TASK_PRIORITY, 0, K_NO_WAIT);

    /* 6. 设置线程名称 */
    k_thread_name_set(*tid, "MY_GSENSOR");

    LOG_INF("G-Sensor module initialized");

    return 0;
}
