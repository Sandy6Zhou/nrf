/********************************************************************
**版权所有         上海复旦微电子集团股份有限公司
**文件名称:        fm175xx_reg.h
**文件描述:        FM175xx 寄存器定义头文件
**当前版本:        V1.0
**作    者:        移植自复旦微官方驱动
**完成日期:        2026.01.22
*********************************************************************
** 功能描述:        定义FM17550/FM17622芯片的寄存器地址和位掩码
*********************************************************************/

#ifndef _FM175XX_REG_H_
#define _FM175XX_REG_H_

/* Page 0 寄存器定义 */
#define JREG_PAGE0              0x00    /* Page寄存器 */
#define JREG_COMMAND            0x01    /* 命令寄存器 */
#define JREG_COMMIEN            0x02    /* 通信中断使能 */
#define JREG_DIVIEN             0x03    /* 分频中断使能 */
#define JREG_COMMIRQ            0x04    /* 通信中断请求 */
#define JREG_DIVIRQ             0x05    /* 分频中断请求 */
#define JREG_ERROR              0x06    /* 错误标志 */
#define JREG_STATUS1            0x07    /* 状态寄存器1 */
#define JREG_STATUS2            0x08    /* 状态寄存器2 */
#define JREG_FIFODATA           0x09    /* FIFO数据 */
#define JREG_FIFOLEVEL          0x0A    /* FIFO级别 */
#define JREG_WATERLEVEL         0x0B    /* 水位 */
#define JREG_CONTROL            0x0C    /* 控制寄存器 */
#define JREG_BITFRAMING         0x0D    /* 位帧 */
#define JREG_COLL               0x0E    /* 冲突 */

/* Page 1 寄存器定义 */
#define JREG_MODE               0x11    /* 模式 */
#define JREG_TXMODE             0x12    /* 发送模式 */
#define JREG_RXMODE             0x13    /* 接收模式 */
#define JREG_TXCONTROL          0x14    /* 发送控制 */
#define JREG_TXAUTO             0x15    /* 自动发送 */
#define JREG_TXSEL              0x16    /* 发送选择 */
#define JREG_RXSEL              0x17    /* 接收选择 */
#define JREG_RXTHRESHOLD        0x18    /* 接收阈值 */
#define JREG_DEMOD              0x19    /* 解调 */
#define JREG_FELICANFC          0x1A    /* FeliCa NFC */
#define JREG_FELICANFC2         0x1B    /* FeliCa NFC2 */
#define JREG_MIFARE             0x1C    /* Mifare */
#define JREG_MANUALRCV          0x1D    /* 手动接收 */
#define JREG_TYPEB              0x1E    /* Type B */

/* Page 2 寄存器定义 */
#define JREG_SERIALSPEED        0x1F    /* 串口速度 */
#define JREG_CRCRESULT1         0x21    /* CRC结果高字节 */
#define JREG_CRCRESULT2         0x22    /* CRC结果低字节 */
#define JREG_GSNLOADMOD         0x23    /* GSN负载调制 */
#define JREG_MODWIDTH           0x24    /* 调制宽度 */
#define JREG_TXBITPHASE         0x25    /* 发送位相位 */
#define JREG_RFCFG              0x26    /* RF配置 */
#define JREG_GSN                0x27    /* GSN */
#define JREG_CWGSP              0x28    /* CWGSP */
#define JREG_MODGSP             0x29    /* MODGSP */
#define JREG_TMODE              0x2A    /* 定时器模式 */
#define JREG_TPRESCALER         0x2B    /* 定时器预分频 */
#define JREG_TRELOADHI          0x2C    /* 定时器重载高字节 */
#define JREG_TRELOADLO          0x2D    /* 定时器重载低字节 */
#define JREG_TCOUNTERVALHI      0x2E    /* 定时器计数高字节 */
#define JREG_TCOUNTERVALLO      0x2F    /* 定时器计数低字节 */

/* Page 3 寄存器定义 */
#define JREG_TESTSEL1           0x31    /* 测试选择1 */
#define JREG_TESTSEL2           0x32    /* 测试选择2 */
#define JREG_TESTPINEN          0x33    /* 测试引脚使能 */
#define JREG_TESTPINVALUE       0x34    /* 测试引脚值 */
#define JREG_TESTBUS            0x35    /* 测试总线 */
#define JREG_AUTOTEST           0x36    /* 自动测试 */
#define JREG_VERSION            0x37    /* 版本 */
#define JREG_ANALOGTEST         0x38    /* 模拟测试 */
#define JREG_TESTDAC1           0x39    /* 测试DAC1 */
#define JREG_TESTDAC2           0x3A    /* 测试DAC2 */
#define JREG_TESTADC            0x3B    /* 测试ADC */

/* 命令定义 */
#define CMD_IDLE                0x00    /* 空闲 */
#define CMD_MEM                 0x01    /* 存储 */
#define CMD_GENID               0x02    /* 生成随机ID */
#define CMD_CALCCRC             0x03    /* 计算CRC */
#define CMD_TRANSMIT            0x04    /* 发送 */
#define CMD_NOCMDCHANGE         0x07    /* 不改变命令 */
#define CMD_RECEIVE             0x08    /* 接收 */
#define CMD_TRANSCEIVE          0x0C    /* 收发 */
#define CMD_AUTHENT             0x0E    /* 认证 */
#define CMD_SOFT_RESET          0x0F    /* 软复位 */

/* 位定义 */
#define JBIT_TXCRCEN            0x40    /* TX CRC使能 */
#define JBIT_RXCRCEN            0x20    /* RX CRC使能 */
#define JBIT_VALUESAFTERCOLL    0x80    /* 冲突后值 */
#define JBIT_IRQ                0x80    /* 中断请求 */
#define JBIT_TIMERIRq           0x01    /* 定时器中断 */

/* 位掩码定义 */
#define JMASK_COMMAND           0x0F    /* 命令位掩码 */
#define JMASK_RXBITS            0x07    /* 接收位掩码 */
#define JMASK_SPEED             0x70    /* 速度位掩码 */
#define JMASK_FRAMING           0x03    /* 帧位掩码 */
#define JMASK_MINLEVEL          0x0F    /* 最小电平掩码 */
#define JMASK_COLLEVEL          0x07    /* 冲突电平掩码 */
#define JMASK_RXGAIN            0x70    /* 接收增益掩码 */
#define JMASK_WATERLEVEL        0x3F    /* 水位掩码 */

/* 载波控制 */
#define CW_DISABLE              0
#define CW1_ENABLE              1
#define CW2_ENABLE              2
#define CW_ENABLE               3

/* 通信速率 */
#define MODWIDTH_106            0x26    /* 106Kbps */
#define MODWIDTH_212            0x13    /* 212Kbps */
#define MODWIDTH_424            0x09    /* 424Kbps */
#define MODWIDTH_848            0x04    /* 848Kbps */

/* 错误码 */
#define FM175XX_SUCCESS         0x00
#define FM175XX_TIMER_ERR       0xF1    /* 接收超时 */
#define FM175XX_COMM_ERR        0xF2    /* 通信错误 */
#define FM175XX_COLL_ERR        0xF3    /* 冲突错误 */
#define FM175XX_PARAM_ERR       0xF4    /* 参数错误 */
#define FM175XX_LENGTH_ERR      0xF5    /* 长度错误 */
#define FM175XX_AUTH_ERR        0xF6    /* 认证错误 */

/* 布尔值定义 */
#define FM175XX_SET             1
#define FM175XX_RESET           0
#define FM175XX_ENABLE          1
#define FM175XX_DISABLE         0

#endif /* _FM175XX_REG_H_ */
