/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_battery.c
**文件描述:        电池管理模块实现文件
**当前版本:        V1.0
**作    者:        周森达 (zhousenda@jimiiot.com)
**完成日期:        2026.02.04
*********************************************************************
** 功能描述:        1. 电池电压、NTC采集以及电源开关使能
**                 2. 电池状态、充电状态检测
*********************************************************************/

/* 必须在包含 my_comm.h 之前定义 BLE_LOG_MODULE_ID，避免与 my_ble_log.h 中的默认定义冲突 */
#define BLE_LOG_MODULE_ID BLE_LOG_MOD_BATTERY

#include "my_comm.h"

LOG_MODULE_REGISTER(my_battery, LOG_LEVEL_INF);

/* zephyr,user 里有两个 io-channels：index 0 = 通道0(电池)，index 1 = 通道1(NTC) */
#define ZEPHYR_USER_NODE DT_PATH(zephyr_user)

//电量状态阈值, 百分比表示, 0~100%
#define BATT_TIMER_MS 100  /**< 电池状态检查定时器的周期（毫秒），用于控制LED亮灭 */
#define CHG_DEBOUNCE_MS 50 /**< 充电引脚相关消抖时间（毫秒） */
#define CHG_CTRL_LED_MS 500/**< 充电状态LED控制定时器的周期（毫秒） */
#define CHG_CHECK_S 10 /**< 充电状态电池电量更新周期（秒） */

#define BATT_EMPTY_VAL 5    /**< 电池电量为空的阈值，当电量低于此值时，电池状态为 BATT_EMPTY */
#define BATT_LOW_VAL 30     /**< 电池电量低的阈值，当电量低于此值时，电池状态为 BATT_LOW */
#define BATT_NORMAL_VAL 45  /**< 电池电量正常的阈值，当电量低于此值时，电池状态为 BATT_NORMAL */
#define BATT_FAIR_VAL 65    /**< 电池电量良好的阈值，当电量低于此值时，电池状态为 BATT_FAIR */
#define BATT_HIGH_VAL 85    /**< 电池电量高的阈值，当电量低于此值时，电池状态为 BATT_HIGH */

#define CHG_BATT_LOW_VAL 30     /**< 充电时电池电量低的阈值，当电量低于此值时，充电状态为 CHG_POWER_LOW */
#define CHG_BATT_FAIR_VAL 65    /**< 充电时电池电量中等的阈值，当电量低于此值时，充电状态为 CHG_POWER_FAIR */
#define CHG_BATT_HIGH_VAL 85    /**< 充电时电池电量高的阈值，当电量低于此值时，充电状态为 CHG_POWER_HIGH */

/* 通道 0：电池电压 */
static const struct adc_dt_spec batt_adc = ADC_DT_SPEC_GET_BY_IDX(ZEPHYR_USER_NODE, 0);

static const struct gpio_dt_spec batt_pwr_en = GPIO_DT_SPEC_GET(DT_ALIAS(batt_pwr_ctrl), gpios);
static const struct gpio_dt_spec charge_state = GPIO_DT_SPEC_GET(DT_ALIAS(batt_state), gpios);
static const struct gpio_dt_spec charge_det = GPIO_DT_SPEC_GET(DT_ALIAS(charge_detect), gpios);

static struct gpio_callback batt_gpio_cb;
// ADC原始采样值
static int16_t batt_raw;

static struct adc_sequence batt_seq = {
    .buffer = &batt_raw,
    .buffer_size = sizeof(batt_raw),
};

static struct k_timer g_chg_stat_timer; // 充电状态检测消抖定时器，用于充电状态引脚的消抖处理
static struct k_timer g_chg_det_timer;  // 充电检测消抖定时器，用于充电检测引脚的消抖处理

// 正常状态LED控制结构体，包含定时器、电池状态和计数器
Batt_LED_Ctrl_S g_batt_led_ctrl = { 0 };

// 充电状态LED控制结构体，包含定时器、充电电池状态和计数器
CHG_LED_Ctrl_S g_chg_led_ctrl = { 0 };

static int g_chg_stat_level = 0;        // 当前充电状态引脚电平，用于消抖处理
static int g_chg_det_level = 0;         // 当前充电检测引脚电平，用于消抖处理

int g_battery_val = 0;//电池电量含量，0~100%，调试用

// 电源状态，初始值为未连接
MY_CHG_STATE g_charg_state = NO_CHARGING;

/*********************************************************************
**函数名称:  batt_timer_handler
**入口参数:  timer 定时器指针，由系统自动传递
**出口参数:  无
**函数功能:  定时器回调函数，向控制模块发送电池状态作LED控制
*********************************************************************/
static void batt_timer_handler(struct k_timer *timer)
{
    MSG_S msg;  // 消息结构体，用于发送电池状态显示消息

    g_batt_led_ctrl.time_count++;  // 增加电池状态时间计数器

    msg.msgID = MY_MSG_SHOW_BATTERY;  // 设置消息ID为电池状态显示消息
    msg.DataLen = sizeof(g_batt_led_ctrl);  // 设置消息数据长度
    msg.pData = (void *)&g_batt_led_ctrl;  // 设置消息数据指针为电池LED控制结构体地址

    /*
    定时器发送消息到CTRL线程中处理控制LED,如果线程阻塞，会导致LED控制出现异常
    后续需要考虑是否直接在中断中处理，避免线程阻塞
    */
    my_send_msg_data(MOD_CTRL, MOD_CTRL, &msg);  // 发送消息到控制模块
}

/* 读取电池电压原始值，并转换为 mV（不含分压系数） */
int batt_read_mv(int32_t *mv)
{
    int err;
    int32_t val_mv;

    err = adc_read(batt_adc.dev, &batt_seq);
    if (err < 0) return err;

    val_mv = batt_raw;
    // TODO 后续需取均值，以及开关ADC处理
    err = adc_raw_to_millivolts_dt(&batt_adc, &val_mv);
    if (err < 0) return err;

    *mv = val_mv;
    return 0;
}

int batt_adc_init(void)
{
    int err;

    /* 检查 ADC 是否就绪 */
    if (!adc_is_ready_dt(&batt_adc))
    {
        return -ENODEV;
    }

    /* 配置电池电压通道（配置来自 devicetree） */
    err = adc_channel_setup_dt(&batt_adc);
    if (err < 0) return err;

    /* 初始化 sequence（会填充通道掩码等） */
    err = adc_sequence_init_dt(&batt_adc, &batt_seq);
    if (err < 0) return err;

    return 0;
}

/* 电池使能：true=打开，false=关闭 */
void batt_enable(bool on)
{
    MY_LOG_INF("%s:%d", __func__, on);
    gpio_pin_set_dt(&batt_pwr_en, on ? 1 : 0);
}

/*********************************************************************
**函数名称:  chg_timer_handler
**入口参数:  timer 定时器指针，由系统自动传递
**出口参数:  无
**函数功能:  1.充电状态LED控制定时器的回调函数，用于控制充电状态LED的显示
**          2.在特定条件下停止充电，以及发送消息更新充电状态。
*********************************************************************/
void chg_timer_handler(struct k_timer *timer)
{
    // 根据时间计数器的值，控制LED的闪烁模式
    if (g_chg_led_ctrl.time_count % 3 == 0)
    {
        // 在第1.5s的前1s点亮LED
        batt_led_set_level(g_chg_led_ctrl.state + 1);
    }
    else if (g_chg_led_ctrl.time_count % 3 == 2)
    {
        // 在第1s处熄灭对应LED
        batt_led_set_level(g_chg_led_ctrl.state);
    }

    // 当时间计数器达到特定值且充电状态为高电量或满电量时，禁用电池电源,当电池电压过低时，不能禁用充电功能
    // 消除充电电压对电池电压抬高效应的影响。在采集电池电压输出高1s关闭充电路径，采集完毕输出低打开供电路径
    if(g_chg_led_ctrl.time_count == ((CHG_CHECK_S-1) * 2) &&
       g_chg_led_ctrl.state > CHG_BATT_FAIR)
    {
        batt_enable(false);  // 禁用电池电源
    }

    // 当时间计数器达到阈值时，发送充电状态显示消息
    // 每10s更新一次充电电池电量状态
    if (g_chg_led_ctrl.time_count >= CHG_CHECK_S * 2)
    {
        my_send_msg(MOD_CTRL, MOD_CTRL, MY_MSG_SHOW_CHARG);     // 发送消息到控制模块
    }

    g_chg_led_ctrl.time_count++;  // 增加时间计数器
}

/*********************************************************************
**函数名称:  my_battery_show_chgled
**入口参数:  无
**出口参数:  无
**函数功能:  检查充电检测引脚的状态，根据检测结果更新充电状态并控制LED显示。
** 如果检测到充电，则启动充电LED控制定时器；如果未检测到充电，则关闭所有电池LED。
*********************************************************************/
void my_battery_show_chgled()
{
    // 检查充电检测引脚状态，如果为 1 表示正在充电
    if (gpio_pin_get_dt(&charge_det) == 1)
    {
        LOG_INF("The charger is plugged in.");
        g_charg_state = CHARGING;  // 设置充电状态为正在充电
        my_battery_update_state();  // 更新电池状态和充电状态
        
        // 根据充电状态控制电池电源使能
        if(g_charg_state == CHARGING)
        {
            batt_enable(true);  // 启用电池电源
        }
        else if (g_charg_state == CHARG_FULL)
        {
            batt_enable(false);  // 禁用电池电源
        }

        g_chg_led_ctrl.time_count = 0;  // 重置充电LED控制定时器的时间计数
        // 启动充电LED控制定时器，立即执行一次，然后以 CHG_CTRL_LED_MS 为周期重复执行
        k_timer_start(g_chg_led_ctrl.timer, K_MSEC(0), K_MSEC(CHG_CTRL_LED_MS));

        // 检查正常情况下的电池状态LED控制定时器是否正在运行
        if (k_timer_remaining_get(g_batt_led_ctrl.timer) != 0)
        {
            // 如果正在运行，停止定时器，避免与充电状态LED显示冲突
            k_timer_stop(g_batt_led_ctrl.timer);
        }
    }
    else
    {
        g_charg_state = NO_CHARGING;  // 设置充电状态为未充电
        batt_enable(true);  // 启用电池电源
        batt_led_set_level(0);  // 关闭所有电池LED
        k_timer_stop(g_chg_led_ctrl.timer);  // 停止充电LED控制定时器
        LOG_INF("The charger is not plugged in.");
    }
}

/*********************************************************************
**函数名称:  batt_chg_stat_handle
**入口参数:  timer 定时器指针，由系统自动传递
**出口参数:  无
**函数功能:  定时器回调函数，比较当前检测到的电平与上一次保存的电平，实现消抖功能，
**向控制模块发送消息作LED控制，当充电状态引脚电平为 0 时，表示充电已满
*********************************************************************/
void batt_chg_stat_handle(struct k_timer *timer)
{
    static int s_last_chg_stat_level = 0;   // 上一次充电状态引脚电平，用于消抖处理

    // 如果当前检测到的电平与上一次保存的电平相同，说明是抖动，直接返回
    if (g_chg_stat_level == s_last_chg_stat_level)
        return;

    // 再次读取引脚电平，如果与之前检测到的电平一致，则认为是稳定状态
    if (g_chg_stat_level == gpio_pin_get_dt(&charge_state))
    {
        s_last_chg_stat_level = g_chg_stat_level;  // 更新上一次保存的电平

        // 如果充电状态引脚电平为 0，表示充电已满
        if (g_chg_stat_level == 0)
        {
            g_charg_state = CHARG_FULL;  // 设置充电状态为已满
        }

        my_send_msg(MOD_CTRL, MOD_CTRL, MY_MSG_SHOW_CHARG);     // 发送消息到控制模块
    }
}

/*********************************************************************
**函数名称:  batt_chg_det_handle
**入口参数:  timer 定时器指针，由系统自动传递
**出口参数:  无
**函数功能:  定时器回调函数，比较当前检测到的电平与上一次保存的电平，实现消抖功能，向控制模块发送消息作LED控制
*********************************************************************/
void batt_chg_det_handle(struct k_timer *timer)
{
    static int s_last_chg_det_level = 0;    // 上一次充电检测引脚电平，用于消抖处理

    // 如果当前检测到的电平与上一次保存的电平相同，说明是抖动，直接返回
    if (g_chg_det_level == s_last_chg_det_level)
        return;

    // 再次读取引脚电平，如果与之前检测到的电平一致，则认为是稳定状态
    if (g_chg_det_level == gpio_pin_get_dt(&charge_det))
    {
        s_last_chg_det_level = g_chg_det_level;  // 更新上一次保存的电平

        my_send_msg(MOD_CTRL, MOD_CTRL, MY_MSG_SHOW_CHARG);     // 发送消息到控制模块
    }
}

/* 共用 ISR：根据 pins 掩码区分是 P1.7 还是 P1.8 触发 */
static void batt_gpio_isr(const struct device *dev,
                        struct gpio_callback *cb, 
                        uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);

    if (pins & BIT(charge_state.pin))
    {
        /* P1.7 触发：电池状态变化（充电中 / 充满） */
        g_chg_stat_level = gpio_pin_get_dt(&charge_state);
        //50ms消抖处理
        k_timer_start(&g_chg_stat_timer, K_MSEC(CHG_DEBOUNCE_MS), K_NO_WAIT);
    }

    if (pins & BIT(charge_det.pin))
    {
        /* P1.8 触发：充电检测变化（开始充电 / 停止充电） */
        g_chg_det_level = gpio_pin_get_dt(&charge_det);
        //50ms消抖处理
        k_timer_start(&g_chg_det_timer, K_MSEC(CHG_DEBOUNCE_MS), K_NO_WAIT);
    }
}

int batt_gpio_init(void)
{
    int ret;
    // 正常状态LED控制定时器，用于控制正常状态LED的显示
    static struct k_timer s_batt_timer;
    // 充电状态LED控制定时器，用于控制充电状态LED的显示
    static struct k_timer s_chg_timer;

    if (!device_is_ready(batt_pwr_en.port) ||
        !device_is_ready(charge_state.port) ||
        !device_is_ready(charge_det.port))
    {
        return -ENODEV;
    }

    /* 使能口：输出，默认打开 */
    ret = gpio_pin_configure_dt(&batt_pwr_en, GPIO_OUTPUT_ACTIVE);
    if (ret) return ret;

    gpio_pin_set_dt(&batt_pwr_en, 0);

    /* 状态/充电检测：输入（上拉在 DTS 里已经配置） */
    ret = gpio_pin_configure_dt(&charge_state, GPIO_INPUT);
    if (ret) return ret;

    ret = gpio_pin_configure_dt(&charge_det, GPIO_INPUT);
    if (ret) return ret;

    /* 配置中断：
     * 根据硬件逻辑选择：
     *  - GPIO_INT_EDGE_FALLING：只在拉低时触发
     *  - GPIO_INT_EDGE_RISING：只在拉高时触发
     *  - GPIO_INT_EDGE_BOTH：高/低变化都触发
     */
    ret = gpio_pin_interrupt_configure_dt(&charge_state, GPIO_INT_EDGE_BOTH);
    if (ret) return ret;

    ret = gpio_pin_interrupt_configure_dt(&charge_det, GPIO_INT_EDGE_BOTH);
    if (ret) return ret;

    /* 一个回调处理两个引脚 */
    gpio_init_callback(&batt_gpio_cb, batt_gpio_isr,
                       BIT(charge_state.pin) | BIT(charge_det.pin));
    gpio_add_callback(charge_state.port, &batt_gpio_cb);
    gpio_add_callback(charge_det.port, &batt_gpio_cb);

    // 初始化正常状态LED控制定时器
    g_batt_led_ctrl.timer = &s_batt_timer;
    k_timer_init(g_batt_led_ctrl.timer, batt_timer_handler, NULL);

    // 初始化充电状态LED控制定时器
    g_chg_led_ctrl.timer = &s_chg_timer;
    k_timer_init(g_chg_led_ctrl.timer, chg_timer_handler, NULL);

    // 初始化充电状态(是否充满)检测引脚消抖定时器
    k_timer_init(&g_chg_stat_timer, batt_chg_stat_handle, NULL);

    // 初始化充电检测（是否有充电器插入）消抖定时器
    k_timer_init(&g_chg_det_timer, batt_chg_det_handle, NULL);

    return 0;
}


/*********************************************************************
**函数名称:  my_battery_update_state
**入口参数:  无
**出口参数:  无
**函数功能:  检查并更新电池状态和充电状态
*********************************************************************/
void my_battery_update_state()
{
    //TODO 增加ADC值检测电压，根据电压判断电池电量,更新电池状态和充电电量状态

    // 根据电池含量判断电池状态
    if (g_battery_val <= BATT_EMPTY_VAL)
    {
        g_batt_led_ctrl.state = BATT_EMPTY;  // 电池电量为空
    }
    else if (g_battery_val <= BATT_LOW_VAL)
    {
        g_batt_led_ctrl.state = BATT_LOW;  // 电池电量低
    }
    else if (g_battery_val <= BATT_NORMAL_VAL)
    {
        g_batt_led_ctrl.state = BATT_NORMAL;  // 电池电量正常
    }
    else if (g_battery_val <= BATT_FAIR_VAL)
    {
        g_batt_led_ctrl.state = BATT_FAIR;  // 电池电量良好
    }
    else if (g_battery_val <= BATT_HIGH_VAL)
    {
        g_batt_led_ctrl.state = BATT_HIGH;  // 电池电量高
    }
    else
    {
        g_batt_led_ctrl.state = BATT_FULL;  // 电池电量满
    }

    LOG_INF("battery class:%d", g_batt_led_ctrl.state);  // 输出电池状态等级

    // 如果正在充电
    if (g_charg_state != NO_CHARGING)
    {
        // 检查充电状态引脚，如果为低电平表示充电已满
        if (gpio_pin_get_dt(&charge_state) == 0)
        {
            g_charg_state = CHARG_FULL;  // 设置充电状态为已满
            LOG_INF("charge full");  // 输出充电已满信息
        }

        // 根据电池电量判断充电状态
        if (g_battery_val <= CHG_BATT_LOW_VAL)
        {
            g_chg_led_ctrl.state = CHG_BATT_LOW;  // 充电电量低
        }
        else if (g_battery_val <= CHG_BATT_FAIR_VAL)
        {
            g_chg_led_ctrl.state = CHG_BATT_FAIR;  // 充电电量中等
        }
        else if (g_battery_val <= CHG_BATT_HIGH_VAL)
        {
            g_chg_led_ctrl.state = CHG_BATT_HIGH;  // 充电电量高
        }
        else
        {
            g_chg_led_ctrl.state = CHG_BATT_FULL;  // 充电电量满
        }

        LOG_INF("charge class:%d", g_chg_led_ctrl.state);  // 输出充电状态等级
    }
}

/*********************************************************************
**函数名称:  my_battery_show
**入口参数:  无
**出口参数:  无
**函数功能:  启动100ms循环定时器控制LED显示电池状态
*********************************************************************/
void my_battery_show()
{
    if(g_charg_state != NO_CHARGING)
    {
        MY_LOG_INF("be charging");  // 设备正在充电，输出充电状态
        return;  // 直接返回，不进行后续的电池状态检查
    }

    my_battery_update_state();  // 检查并更新当前电池状态

    //当按键重复按下时，重新开始定时器
    g_batt_led_ctrl.time_count = 0;  // 重置时间计数器
    // 启动电池状态LED控制定时器，立即执行一次，然后以 100ms 为周期重复执行
    k_timer_start(g_batt_led_ctrl.timer, K_MSEC(0), K_MSEC(BATT_TIMER_MS));
}
