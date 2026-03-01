/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        nfc_reader_api.c
**文件描述:        NFC读卡器API层实现
**当前版本:        V1.0
**作    者:        Harrison Wu
**完成日期:        2026.01.22
*********************************************************************
** 功能描述:        实现Type A卡片操作接口
*********************************************************************/

#include "nfc_reader_api.h"
#include "fm175xx_driver.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* 注册NFC Reader模块日志 */
LOG_MODULE_REGISTER(nfc_reader, LOG_LEVEL_INF);

/* Type A卡片信息 */
struct picc_a_struct PICC_A;

/* 命令代码 */
static const uint8_t RF_CMD_ANTICOL[3] = {0x93, 0x95, 0x97};

/********************************************************************
**函数名称:  nfc_set_send_crc
**入口参数:  mode --- 1使能，0禁用
**函数功能:  设置发送CRC
**返 回 值:  无
*********************************************************************/
void nfc_set_send_crc(uint8_t mode)
{
    if (mode) {
        fm175xx_modify_reg(JREG_TXMODE, JBIT_TXCRCEN, JBIT_TXCRCEN);
    } else {
        fm175xx_modify_reg(JREG_TXMODE, JBIT_TXCRCEN, 0);
    }
}

/********************************************************************
**函数名称:  nfc_set_receive_crc
**入口参数:  mode --- 1使能，0禁用
**函数功能:  设置接收CRC
**返 回 值:  无
*********************************************************************/
void nfc_set_receive_crc(uint8_t mode)
{
    if (mode) {
        fm175xx_modify_reg(JREG_RXMODE, JBIT_RXCRCEN, JBIT_RXCRCEN);
    } else {
        fm175xx_modify_reg(JREG_RXMODE, JBIT_RXCRCEN, 0);
    }
}

/********************************************************************
**函数名称:  nfc_set_timeout
**入口参数:  microseconds --- 超时时间（微秒）
**函数功能:  设置通信超时时间
**返 回 值:  无
*********************************************************************/
void nfc_set_timeout(uint32_t microseconds)
{
    uint16_t preset;
    uint8_t mode;
    
    /* 计算定时器预置值 */
    preset = (uint16_t)(microseconds / 10);  /* 假设定时器时钟为100kHz */
    
    /* 设置定时器模式 */
    mode = 0x80 | ((preset >> 8) & 0x0F);    /* 自动模式 + 高4位 */
    fm175xx_write_reg(JREG_TMODE, mode);
    fm175xx_write_reg(JREG_TPRESCALER, 0x3E);  /* 预分频 */
    fm175xx_write_reg(JREG_TRELOADHI, (preset >> 8) & 0xFF);
    fm175xx_write_reg(JREG_TRELOADLO, preset & 0xFF);
}

/********************************************************************
**函数名称:  nfc_set_cw
**入口参数:  mode --- CW_DISABLE/CW_ENABLE
**函数功能:  设置载波开关
**返 回 值:  无
*********************************************************************/
void nfc_set_cw(uint8_t mode)
{
    fm175xx_write_reg(JREG_TXCONTROL, (mode & 0x03) | 0x80);
}

/********************************************************************
**函数名称:  nfc_command_execute
**入口参数:  cmd_status --- 命令状态结构体
**函数功能:  执行NFC命令
**返 回 值:  FM175XX_SUCCESS或错误码
*********************************************************************/
uint8_t nfc_command_execute(struct command_struct *cmd_status)
{
    uint8_t reg_val;
    uint8_t irq_val;
    uint8_t result = FM175XX_SUCCESS;
    uint32_t timeout_cnt = 0;
    const uint32_t MAX_TIMEOUT = 10000;  /* 最大超时计数 */
    
    /* 清空FIFO */
    fm175xx_clear_fifo();
    
    /* 写入发送数据到FIFO */
    if (cmd_status->nBytesToSend > 0) {
        fm175xx_write_fifo(cmd_status->nBytesToSend, cmd_status->pSendBuf);
    }
    
    /* 设置位帧 */
    fm175xx_modify_reg(JREG_BITFRAMING, JMASK_RXBITS, cmd_status->nBitsToSend);
    
    /* 设置接收等待 */
    fm175xx_write_reg(JREG_RXSEL, RXWAIT);
    
    /* 设置CRC */
    nfc_set_send_crc(cmd_status->SendCRCEnable);
    nfc_set_receive_crc(cmd_status->ReceiveCRCEnable);
    
    /* 清除中断标志 */
    fm175xx_read_reg(JREG_COMMIRQ, &reg_val);
    fm175xx_read_reg(JREG_DIVIRQ, &reg_val);
    
    /* 启动命令 */
    fm175xx_write_reg(JREG_COMMAND, cmd_status->Cmd);
    
    /* 等待命令完成或超时 */
    timeout_cnt = 0;
    while (1) {
        fm175xx_read_reg(JREG_COMMIRQ, &irq_val);
        
        /* 检查定时器中断（超时） */
        if (irq_val & JBIT_TIMERIRq) {
            result = FM175XX_TIMER_ERR;
            break;
        }
        
        /* 检查命令完成中断 */
        if (irq_val & JBIT_IRQ) {
            break;
        }
        
        /* 超时保护 */
        if (++timeout_cnt > MAX_TIMEOUT) {
            result = FM175XX_TIMER_ERR;
            break;
        }
    }
    
    /* 读取错误寄存器 */
    fm175xx_read_reg(JREG_ERROR, &reg_val);
    if (reg_val & 0x13) {  /* 协议错误、奇偶校验错误或CRC错误 */
        result = FM175XX_COMM_ERR;
    }
    
    /* 读取接收数据 */
    if ((result == FM175XX_SUCCESS) && (cmd_status->nBytesToReceive > 0)) {
        fm175xx_read_reg(JREG_FIFOLEVEL, &reg_val);
        cmd_status->nBytesReceived = reg_val & 0x7F;
        
        if (cmd_status->nBytesReceived > 0) {
            fm175xx_read_fifo(cmd_status->nBytesReceived, cmd_status->pReceiveBuf);
        }
        
        /* 检查接收长度 */
        if (cmd_status->nBytesReceived != cmd_status->nBytesToReceive) {
            result = FM175XX_LENGTH_ERR;
        }
    }
    
    /* 读取冲突位置 */
    fm175xx_read_reg(JREG_COLL, &reg_val);
    cmd_status->CollPos = reg_val & 0x1F;
    
    cmd_status->Error = result;
    return result;
}

/********************************************************************
**函数名称:  nfc_init_reader_a
**入口参数:  无
**函数功能:  初始化Type A读卡器
**返 回 值:  无
*********************************************************************/
void nfc_init_reader_a(void)
{
    uint8_t reg_val;
    
    /* 设置接收增益 */
    fm175xx_read_reg(JREG_RFCFG, &reg_val);
    reg_val = (reg_val & ~JMASK_RXGAIN) | (RXGAIN_A << 4);
    fm175xx_write_reg(JREG_RFCFG, reg_val);
    
    /* 设置GSN */
    fm175xx_write_reg(JREG_GSN, (GSNON_A << 4) | GSP_A);
    
    /* 设置接收阈值 */
    fm175xx_read_reg(JREG_RXTHRESHOLD, &reg_val);
    reg_val = (reg_val & ~JMASK_MINLEVEL) | (MINLEVEL_A << 4);
    reg_val = (reg_val & ~JMASK_COLLEVEL) | COLLLEVEL_A;
    fm175xx_write_reg(JREG_RXTHRESHOLD, reg_val);
    
    /* 设置调制宽度（106Kbps） */
    fm175xx_write_reg(JREG_MODWIDTH, MODWIDTH_106);
    
    /* 设置TX/RX模式 */
    fm175xx_write_reg(JREG_TXMODE, 0x00);  /* 106Kbps, ISO14443A */
    fm175xx_write_reg(JREG_RXMODE, 0x00);  /* 106Kbps, ISO14443A */
    
    /* 清空FIFO */
    fm175xx_clear_fifo();
}

/********************************************************************
**函数名称:  nfc_reader_a_wakeup
**入口参数:  无
**函数功能:  Type A唤醒卡片（WUPA命令）
**返 回 值:  FM175XX_SUCCESS或错误码
*********************************************************************/
uint8_t nfc_reader_a_wakeup(void)
{
    struct command_struct cmd_status;
    uint8_t outbuf[1] = {0x52};  /* WUPA命令 */
    uint8_t inbuf[2];
    uint8_t result;
    
    cmd_status.SendCRCEnable = FM175XX_RESET;
    cmd_status.ReceiveCRCEnable = FM175XX_RESET;
    cmd_status.pSendBuf = outbuf;
    cmd_status.pReceiveBuf = inbuf;
    cmd_status.nBytesToSend = 1;
    cmd_status.nBitsToSend = 7;  /* 短帧，7位 */
    cmd_status.nBytesToReceive = 2;
    cmd_status.nBitsToReceive = 0;
    cmd_status.Timeout = 10;
    cmd_status.Cmd = CMD_TRANSCEIVE;
    
    result = nfc_command_execute(&cmd_status);
    
    if (result == FM175XX_SUCCESS) {
        PICC_A.ATQA[0] = inbuf[0];
        PICC_A.ATQA[1] = inbuf[1];
        LOG_INF("ATQA: %02X %02X", PICC_A.ATQA[0], PICC_A.ATQA[1]);
    }
    
    return result;
}

/********************************************************************
**函数名称:  nfc_reader_a_request
**入口参数:  无
**函数功能:  Type A请求卡片（REQA命令）
**返 回 值:  FM175XX_SUCCESS或错误码
*********************************************************************/
uint8_t nfc_reader_a_request(void)
{
    struct command_struct cmd_status;
    uint8_t outbuf[1] = {0x26};  /* REQA命令 */
    uint8_t inbuf[2];
    uint8_t result;
    
    cmd_status.SendCRCEnable = FM175XX_RESET;
    cmd_status.ReceiveCRCEnable = FM175XX_RESET;
    cmd_status.pSendBuf = outbuf;
    cmd_status.pReceiveBuf = inbuf;
    cmd_status.nBytesToSend = 1;
    cmd_status.nBitsToSend = 7;  /* 短帧，7位 */
    cmd_status.nBytesToReceive = 2;
    cmd_status.nBitsToReceive = 0;
    cmd_status.Timeout = 10;
    cmd_status.Cmd = CMD_TRANSCEIVE;
    
    result = nfc_command_execute(&cmd_status);
    
    if (result == FM175XX_SUCCESS) {
        PICC_A.ATQA[0] = inbuf[0];
        PICC_A.ATQA[1] = inbuf[1];
        LOG_INF("ATQA: %02X %02X", PICC_A.ATQA[0], PICC_A.ATQA[1]);
    }
    
    return result;
}

/********************************************************************
**函数名称:  nfc_reader_a_anticoll
**入口参数:  cascade_level --- 级联等级(0-2)
**函数功能:  Type A防冲突
**返 回 值:  FM175XX_SUCCESS或错误码
*********************************************************************/
uint8_t nfc_reader_a_anticoll(uint8_t cascade_level)
{
    struct command_struct cmd_status;
    uint8_t outbuf[2] = {RF_CMD_ANTICOL[cascade_level], 0x20};
    uint8_t inbuf[5];
    uint8_t result;
    
    if (cascade_level > 2) {
        return FM175XX_PARAM_ERR;
    }
    
    /* 设置冲突后值 */
    fm175xx_modify_reg(JREG_COLL, JBIT_VALUESAFTERCOLL, JBIT_VALUESAFTERCOLL);
    
    cmd_status.SendCRCEnable = FM175XX_RESET;
    cmd_status.ReceiveCRCEnable = FM175XX_RESET;
    cmd_status.pSendBuf = outbuf;
    cmd_status.pReceiveBuf = inbuf;
    cmd_status.nBytesToSend = 2;
    cmd_status.nBitsToSend = 0;
    cmd_status.nBytesToReceive = 5;
    cmd_status.nBitsToReceive = 0;
    cmd_status.Timeout = 10;
    cmd_status.Cmd = CMD_TRANSCEIVE;
    
    result = nfc_command_execute(&cmd_status);
    
    if ((result == FM175XX_SUCCESS) && (cmd_status.nBytesReceived == 5)) {
        memcpy(PICC_A.UID + (cascade_level * 4), inbuf, 4);
        PICC_A.BCC[cascade_level] = inbuf[4];
        
        /* 校验BCC */
        if ((PICC_A.UID[cascade_level * 4] ^ 
             PICC_A.UID[cascade_level * 4 + 1] ^ 
             PICC_A.UID[cascade_level * 4 + 2] ^ 
             PICC_A.UID[cascade_level * 4 + 3]) != PICC_A.BCC[cascade_level]) {
            result = FM175XX_COMM_ERR;
        }
    } else {
        result = FM175XX_COMM_ERR;
    }
    
    return result;
}

/********************************************************************
**函数名称:  nfc_reader_a_select
**入口参数:  cascade_level --- 级联等级(0-2)
**函数功能:  Type A选择卡片
**返 回 值:  FM175XX_SUCCESS或错误码
*********************************************************************/
uint8_t nfc_reader_a_select(uint8_t cascade_level)
{
    struct command_struct cmd_status;
    uint8_t outbuf[7];
    uint8_t inbuf[1];
    uint8_t result;
    
    if (cascade_level > 2) {
        return FM175XX_PARAM_ERR;
    }
    
    outbuf[0] = RF_CMD_ANTICOL[cascade_level];
    outbuf[1] = 0x70;
    memcpy(&outbuf[2], PICC_A.UID + (cascade_level * 4), 4);
    outbuf[6] = PICC_A.BCC[cascade_level];
    
    cmd_status.SendCRCEnable = FM175XX_SET;
    cmd_status.ReceiveCRCEnable = FM175XX_SET;
    cmd_status.pSendBuf = outbuf;
    cmd_status.pReceiveBuf = inbuf;
    cmd_status.nBytesToSend = 7;
    cmd_status.nBitsToSend = 0;
    cmd_status.nBytesToReceive = 1;
    cmd_status.nBitsToReceive = 0;
    cmd_status.Timeout = 10;
    cmd_status.Cmd = CMD_TRANSCEIVE;
    
    result = nfc_command_execute(&cmd_status);
    
    if ((result == FM175XX_SUCCESS) && (cmd_status.nBytesReceived == 1)) {
        PICC_A.SAK[cascade_level] = inbuf[0];
        LOG_INF("SAK%d: %02X", cascade_level, PICC_A.SAK[cascade_level]);
    } else {
        result = FM175XX_COMM_ERR;
    }
    
    return result;
}

/********************************************************************
**函数名称:  nfc_reader_a_halt
**入口参数:  无
**函数功能:  Type A休眠卡片
**返 回 值:  FM175XX_SUCCESS或错误码
*********************************************************************/
uint8_t nfc_reader_a_halt(void)
{
    struct command_struct cmd_status;
    uint8_t outbuf[2] = {0x50, 0x00};
    uint8_t inbuf[1];
    uint8_t result;
    
    cmd_status.SendCRCEnable = FM175XX_SET;
    cmd_status.ReceiveCRCEnable = FM175XX_SET;
    cmd_status.pSendBuf = outbuf;
    cmd_status.pReceiveBuf = inbuf;
    cmd_status.nBytesToSend = 2;
    cmd_status.nBitsToSend = 0;
    cmd_status.nBytesToReceive = 0;
    cmd_status.nBitsToReceive = 0;
    cmd_status.Timeout = 10;
    cmd_status.Cmd = CMD_TRANSCEIVE;
    
    result = nfc_command_execute(&cmd_status);
    
    /* HALT命令无响应表示成功 */
    if (result == FM175XX_TIMER_ERR) {
        result = FM175XX_SUCCESS;
    }
    
    return result;
}

/********************************************************************
**函数名称:  nfc_reader_a_card_activate
**入口参数:  无
**函数功能:  Type A激活卡片（完整流程）
**返 回 值:  FM175XX_SUCCESS或错误码
*********************************************************************/
uint8_t nfc_reader_a_card_activate(void)
{
    uint8_t result;
    uint8_t cascade_level;
    
    /* 唤醒卡片 */
    result = nfc_reader_a_wakeup();
    if (result != FM175XX_SUCCESS) {
        return result;
    }
    
    /* 根据ATQA判断级联等级 */
    if ((PICC_A.ATQA[0] & 0xC0) == 0x00) {
        cascade_level = 1;  /* 4字节UID */
    } else if ((PICC_A.ATQA[0] & 0xC0) == 0x40) {
        cascade_level = 2;  /* 7字节UID */
    } else if ((PICC_A.ATQA[0] & 0xC0) == 0x80) {
        cascade_level = 3;  /* 10字节UID */
    } else {
        return FM175XX_PARAM_ERR;
    }
    
    LOG_INF("Cascade level: %d", cascade_level);
    
    /* 防冲突和选择流程 */
    for (PICC_A.CASCADE_LEVEL = 0; PICC_A.CASCADE_LEVEL < cascade_level; PICC_A.CASCADE_LEVEL++) {
        result = nfc_reader_a_anticoll(PICC_A.CASCADE_LEVEL);
        if (result != FM175XX_SUCCESS) {
            return result;
        }
        
        result = nfc_reader_a_select(PICC_A.CASCADE_LEVEL);
        if (result != FM175XX_SUCCESS) {
            return result;
        }
    }
    
    /* 打印UID */
    LOG_INF("UID: ");
    for (uint8_t i = 0; i < (cascade_level * 4); i++) {
        LOG_INF("%02X ", PICC_A.UID[i]);
    }
    
    return FM175XX_SUCCESS;
}
