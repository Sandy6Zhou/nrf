/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_battery.h
**文件描述:        电池管理模块实现头文件
**当前版本:        V1.0
**作    者:        周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.02.04
*********************************************************************/

#ifndef _MY_BATTERY_H_
#define _MY_BATTERY_H_

// 电池状态枚举, 定义了电池的不同状态等级，从空到满
typedef enum
{
    BATT_EMPTY,   /* 电池电量为空 */
    BATT_LOW,     /* 电池电量低 */
    BATT_NORMAL,  /* 电池电量正常 */
    BATT_FAIR,    /* 电池电量良好 */
    BATT_HIGH,    /* 电池电量高 */
    BATT_FULL,    /* 电池电量满 */
} MY_BATT_STATE;

// 电池 LED 控制结构体, 用于控制电池状态 LED 显示的结构体
typedef struct
{
    struct k_timer *timer;       /* 定时器指针，用于控制 LED 显示的定时 */
    MY_BATT_STATE state;            /* 当前电池状态，使用 batt_state 枚举类型 */
    uint8_t time_count;          /* 时间计数器，用于控制 LED 闪烁和显示持续时间, LED 执行5s, 定时器设置为100ms , 50次*/
} Batt_LED_Ctrl_S;

// 电源状态枚举, 定义了设备的电源连接状态
typedef enum
{
    NO_CHARGING,    /* 充电器未连接 */
    CHARGING,       /* 充电器已连接（充电中） */
    CHARG_FULL,     /* 充电器已连接且电池已充满 */
} MY_CHG_STATE;

int batt_read_mv(int32_t *mv);
int ntc_read_raw(int16_t *raw);
int batt_adc_init(void);
void batt_enable(bool on);
int batt_gpio_init(void);

/*********************************************************************
**函数名称:  show_battary
**入口参数:  无
**出口参数:  无
**函数功能:  显示电池状态
**返 回 值:  无
*********************************************************************/
void my_battery_show();

#endif