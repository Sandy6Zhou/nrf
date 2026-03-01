/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        fm175xx_driver.c
**文件描述:        FM175XX NFC 芯片驱动实现文件
**当前版本:        V1.0
**作    者:        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期:        2026.02.28
*********************************************************************
** 功能描述:        实现 FM17550/FM17622 芯片 I2C 通信和卡片操作
*********************************************************************/

#include "../inc/fm175xx_driver.h"
#include "../inc/fm175xx_reg.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(fm175xx_driver, LOG_LEVEL_INF);

/* I2C 设备配置 */
#define NFC_I2C_NODE    DT_ALIAS(nfc_i2c)
static const struct device *nfc_i2c_dev = DEVICE_DT_GET(NFC_I2C_NODE);

/* NPD 复位引脚配置 */
#define NFC_NPD_NODE    DT_ALIAS(nfc_npd_ctrl)
static const struct gpio_dt_spec nfc_npd_gpio = GPIO_DT_SPEC_GET(NFC_NPD_NODE, gpios);

/* 轮询状态 */
static bool g_poll_running = false;
static struct k_work_delayable g_poll_work;
static nfc_card_detected_cb_t g_card_cb = NULL;

/* 前向声明轮询处理函数 */
static void fm175xx_poll_handler(struct k_work *work);

/* 卡片信息 */
static struct {
    uint8_t uid[16];
    uint8_t uid_len;
    uint8_t sak;
    bool present;
} g_card_info;

/********************************************************************
**函数名称:  fm175xx_npd_reset
**入口参数:  无
**出口参数:  无
**函数功能:  执行 NPD 硬件复位
**返 回 值:  无
*********************************************************************/
static void fm175xx_npd_reset(void)
{
    /* NPD 低电平复位 */
    gpio_pin_set_dt(&nfc_npd_gpio, 0);
    k_sleep(K_MSEC(2));

    /* NPD 高电平释放 */
    gpio_pin_set_dt(&nfc_npd_gpio, 1);
    k_sleep(K_MSEC(2));

    LOG_INF("FM175XX NPD reset completed");
}

/********************************************************************
**函数名称:  fm175xx_read_reg
**入口参数:  reg      ---        寄存器地址
**           data     ---        数据缓冲区
**出口参数:  data     ---        读取到的数据
**函数功能:  读取 FM175XX 寄存器
**返 回 值:  0 表示成功，负值表示失败
*********************************************************************/
int fm175xx_read_reg(uint8_t reg, uint8_t *data)
{
    int ret;
    uint8_t tx_buf = reg & 0x3F;

    ret = i2c_write(nfc_i2c_dev, &tx_buf, 1, FM175XX_I2C_ADDR);
    if (ret < 0) {
        return ret;
    }

    ret = i2c_read(nfc_i2c_dev, data, 1, FM175XX_I2C_ADDR);
    return ret;
}

/********************************************************************
**函数名称:  fm175xx_write_reg
**入口参数:  reg      ---        寄存器地址
**           data     ---        要写入的数据
**出口参数:  无
**函数功能:  写入 FM175XX 寄存器
**返 回 值:  0 表示成功，负值表示失败
*********************************************************************/
int fm175xx_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t tx_buf[2] = {reg & 0x3F, data};
    return i2c_write(nfc_i2c_dev, tx_buf, 2, FM175XX_I2C_ADDR);
}

/********************************************************************
**函数名称:  fm175xx_driver_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化 FM175XX 驱动
**返 回 值:  0 表示成功，负值表示失败
*********************************************************************/
int fm175xx_driver_init(void)
{
    int ret;
    uint8_t version;

    /* 检查 I2C 设备 */
    if (!device_is_ready(nfc_i2c_dev)) {
        LOG_ERR("NFC I2C device not ready");
        return -ENODEV;
    }

    /* 检查 NPD GPIO */
    if (!device_is_ready(nfc_npd_gpio.port)) {
        LOG_ERR("NFC NPD GPIO not ready");
        return -ENODEV;
    }

    /* 配置 NPD GPIO 为输出模式，默认高电平 */
    ret = gpio_pin_configure_dt(&nfc_npd_gpio, GPIO_OUTPUT_HIGH);
    if (ret < 0) {
        LOG_ERR("Failed to configure NPD GPIO: %d", ret);
        return ret;
    }

    /* 执行硬件复位 */
    fm175xx_npd_reset();

    /* 读取版本寄存器 */
    ret = fm175xx_read_reg(JREG_VERSION, &version);
    if (ret < 0) {
        LOG_ERR("Failed to read FM175XX version: %d", ret);
        return ret;
    }

    LOG_INF("FM175XX Version: 0x%02X", version);

    /* 软件复位 */
    fm175xx_write_reg(JREG_COMMAND, CMD_SOFT_RESET);
    k_sleep(K_MSEC(1));

    /* 清空 FIFO */
    uint8_t reg_val;
    fm175xx_read_reg(JREG_FIFOLEVEL, &reg_val);
    reg_val |= 0x80;
    fm175xx_write_reg(JREG_FIFOLEVEL, reg_val);

    /* 初始化轮询工作项 */
    k_work_init_delayable(&g_poll_work, fm175xx_poll_handler);
    g_poll_running = false;
    g_card_cb = NULL;
    g_card_info.present = false;

    LOG_INF("FM175XX driver initialized");
    return 0;
}

/********************************************************************
**函数名称:  fm175xx_driver_deinit
**入口参数:  无
**出口参数:  无
**函数功能:  反初始化 FM175XX 驱动
**返 回 值:  0 表示成功
*********************************************************************/
int fm175xx_driver_deinit(void)
{
    fm175xx_poll_stop();
    return 0;
}

/********************************************************************
**函数名称:  fm175xx_get_version
**入口参数:  无
**出口参数:  version  ---        版本号存储指针
**函数功能:  读取 FM175XX 芯片版本号
**返 回 值:  0 表示成功
*********************************************************************/
int fm175xx_get_version(uint8_t *version)
{
    return fm175xx_read_reg(JREG_VERSION, version);
}

/********************************************************************
**函数名称:  fm175xx_poll_handler
**入口参数:  work     ---        工作项指针
**出口参数:  无
**函数功能:  卡片轮询处理函数
**返 回 值:  无
*********************************************************************/
static void fm175xx_poll_handler(struct k_work *work)
{
    /* TODO: 实现卡片检测逻辑 */
    if (g_poll_running) {
        k_work_reschedule(&g_poll_work, K_MSEC(500));
    }
}

/********************************************************************
**函数名称:  fm175xx_poll_start
**入口参数:  cb       ---        卡片检测回调函数
**出口参数:  无
**函数功能:  启动卡片轮询
**返 回 值:  0 表示成功
*********************************************************************/
int fm175xx_poll_start(nfc_card_detected_cb_t cb)
{
    if (g_poll_running) {
        return 0;
    }

    g_card_cb = cb;
    g_poll_running = true;

    k_work_schedule(&g_poll_work, K_NO_WAIT);

    LOG_INF("FM175XX polling started");
    return 0;
}

/********************************************************************
**函数名称:  fm175xx_poll_stop
**入口参数:  无
**出口参数:  无
**函数功能:  停止卡片轮询
**返 回 值:  0 表示成功
*********************************************************************/
int fm175xx_poll_stop(void)
{
    if (!g_poll_running) {
        return 0;
    }

    g_poll_running = false;
    k_work_cancel_delayable(&g_poll_work);

    LOG_INF("FM175XX polling stopped");
    return 0;
}

/********************************************************************
**函数名称:  fm175xx_modify_reg
**入口参数:  reg_addr --- 寄存器地址
**           mask     --- 要修改的位掩码
**           set      --- 要设置的值
**函数功能:  修改 FM175XX 寄存器的特定位
**返 回 值:  0 表示成功，负值表示失败
*********************************************************************/
int fm175xx_modify_reg(uint8_t reg_addr, uint8_t mask, uint8_t set)
{
    int ret;
    uint8_t reg_val;

    ret = fm175xx_read_reg(reg_addr, &reg_val);
    if (ret < 0) {
        return ret;
    }

    reg_val = (reg_val & ~mask) | (set & mask);

    return fm175xx_write_reg(reg_addr, reg_val);
}

/********************************************************************
**函数名称:  fm175xx_read_fifo
**入口参数:  length   --- 要读取的字节数
**出口参数:  data_buf --- 数据缓冲区
**函数功能:  从 FIFO 读取数据
**返 回 值:  0 表示成功，负值表示失败
*********************************************************************/
int fm175xx_read_fifo(uint8_t length, uint8_t *data_buf)
{
    int ret;
    uint8_t i;

    for (i = 0; i < length; i++) {
        ret = fm175xx_read_reg(JREG_FIFODATA, &data_buf[i]);
        if (ret < 0) {
            LOG_ERR("Read FIFO byte %d failed: %d", i, ret);
            return ret;
        }
    }

    return 0;
}

/********************************************************************
**函数名称:  fm175xx_write_fifo
**入口参数:  length   --- 要写入的字节数
**           data_buf --- 数据缓冲区
**函数功能:  向 FIFO 写入数据
**返 回 值:  0 表示成功，负值表示失败
*********************************************************************/
int fm175xx_write_fifo(uint8_t length, uint8_t *data_buf)
{
    int ret;
    uint8_t i;

    for (i = 0; i < length; i++) {
        ret = fm175xx_write_reg(JREG_FIFODATA, data_buf[i]);
        if (ret < 0) {
            LOG_ERR("Write FIFO byte %d failed: %d", i, ret);
            return ret;
        }
    }

    return 0;
}

/********************************************************************
**函数名称:  fm175xx_clear_fifo
**入口参数:  无
**函数功能:  清空 FIFO
**返 回 值:  0 表示成功，负值表示失败
*********************************************************************/
int fm175xx_clear_fifo(void)
{
    int ret;
    uint8_t reg_val;

    /* 设置 FlushBuffer 位 */
    ret = fm175xx_read_reg(JREG_FIFOLEVEL, &reg_val);
    if (ret < 0) {
        return ret;
    }

    reg_val |= 0x80; /* FlushBuffer bit */
    ret = fm175xx_write_reg(JREG_FIFOLEVEL, reg_val);
    if (ret < 0) {
        return ret;
    }

    /* 验证 FIFO 已清空 */
    ret = fm175xx_read_reg(JREG_FIFOLEVEL, &reg_val);
    if (ret < 0) {
        return ret;
    }

    if ((reg_val & 0x7F) != 0) {
        LOG_WRN("FIFO not empty after clear: %d bytes", reg_val & 0x7F);
        return -EIO;
    }

    return 0;
}
