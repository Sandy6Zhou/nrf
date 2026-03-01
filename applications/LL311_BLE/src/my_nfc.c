/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_nfc.c
**文件描述:        NFC 读卡管理模块实现文件
**当前版本:        V3.0
**作    者:        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期:        2026.02.28
*********************************************************************
** 功能描述:        1. 通过 nfc_api 接口操作 NFC 功能
**                 2. 支持 Mifare Classic 和 NTag 卡片
**                 3. 实现电源控制逻辑
*********************************************************************/

#include "my_comm.h"
#include "nfc_api.h"

/* 注册 NFC 模块日志 */
LOG_MODULE_REGISTER(my_nfc, LOG_LEVEL_INF);

/* NFC 电源控制 */
#define NFC_PWR_NODE DT_ALIAS(nfc_pwr_ctrl)
static const struct gpio_dt_spec nfc_pwr_gpio = GPIO_DT_SPEC_GET(NFC_PWR_NODE, gpios);

/* 内部状态管理 */
struct my_nfc_context
{
    bool is_working;
    uint32_t work_duration_ms;
    bool card_present;
};

static struct my_nfc_context nfc_ctx;

/* 消息队列定义 */
K_MSGQ_DEFINE(my_nfc_msgq, sizeof(MSG_S), 10, 4);

/* 线程数据与栈定义 */
K_THREAD_STACK_DEFINE(my_nfc_task_stack, MY_NFC_TASK_STACK_SIZE);
static struct k_thread my_nfc_task_data;

/********************************************************************
**函数名称:  my_nfc_card_detected_cb
**入口参数:  type     ---        卡片类型
**           uid      ---        UID 数据
**           uid_len  ---        UID 长度
**           data     ---        卡片数据
**           data_len ---        数据长度
**出口参数:  无
**函数功能:  卡片检测回调函数
**返 回 值:  无
*********************************************************************/
static void my_nfc_card_detected_cb(nfc_card_type_t type,
                                     uint8_t *uid, uint8_t uid_len,
                                     uint8_t *data, uint8_t data_len)
{
    LOG_INF("Card detected, type: %d", type);
    /* TODO: 处理卡片检测事件 */
}

/********************************************************************
**函数名称:  my_nfc_task
**入口参数:  p1, p2, p3
**出口参数:  无
**函数功能:  NFC 模块主线程
**返 回 值:  无
*********************************************************************/
static void my_nfc_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    MSG_S msg;
    nfc_result_t result;

    LOG_INF("NFC thread started");

    /* 初始化 NFC API */
    result = nfc_api_init(my_nfc_card_detected_cb);
    if (result != NFC_SUCCESS) {
        LOG_ERR("Failed to initialize NFC API: %d", result);
        return;
    }

    LOG_INF("NFC API initialized successfully");

    for (;;) {
        my_recv_msg(&my_nfc_msgq, (void *)&msg, sizeof(MSG_S), K_FOREVER);

        switch (msg.msgID) {
        case MY_MSG_NFC_START_POLL:
            LOG_INF("Starting NFC polling");
            nfc_ctx.is_working = true;
            nfc_ctx.card_present = false;
            nfc_api_poll_start();
            break;

        case MY_MSG_NFC_STOP_POLL:
            LOG_INF("Stopping NFC polling");
            nfc_ctx.is_working = false;
            nfc_api_poll_stop();
            break;

        default:
            break;
        }
    }
}

/********************************************************************
**函数名称:  my_nfc_pwr_on
**入口参数:  on       ---        是否开启
**出口参数:  无
**函数功能:  NFC 模块电源控制函数
**返 回 值:  0 表示成功
*********************************************************************/
int my_nfc_pwr_on(bool on)
{
    LOG_INF("NFC Power: %s", on ? "ON" : "OFF");
    return gpio_pin_set_dt(&nfc_pwr_gpio, on ? 1 : 0);
}

/********************************************************************
**函数名称:  my_nfc_start_poll
**入口参数:  无
**出口参数:  无
**函数功能:  启动 NFC 轮询功能
**返 回 值:  0 表示成功
*********************************************************************/
int my_nfc_start_poll(void)
{
    MSG_S msg;
    msg.msgID = MY_MSG_NFC_START_POLL;
    msg.pData = NULL;
    msg.DataLen = 0;

    my_send_msg_data(MOD_MAIN, MOD_NFC, &msg);
    return 0;
}

/********************************************************************
**函数名称:  my_nfc_stop_poll
**入口参数:  无
**出口参数:  无
**函数功能:  停止 NFC 轮询功能
**返 回 值:  无
*********************************************************************/
void my_nfc_stop_poll(void)
{
    MSG_S msg;
    msg.msgID = MY_MSG_NFC_STOP_POLL;
    msg.pData = NULL;
    msg.DataLen = 0;

    my_send_msg_data(MOD_MAIN, MOD_NFC, &msg);
}

/********************************************************************
**函数名称:  my_nfc_init
**入口参数:  tid      ---        线程 ID
**出口参数:  无
**函数功能:  初始化 NFC 模块
**返 回 值:  0 表示成功
*********************************************************************/
int my_nfc_init(k_tid_t *tid)
{
    int err;

    /* 检查电源 GPIO */
    if (!gpio_is_ready_dt(&nfc_pwr_gpio)) {
        LOG_ERR("NFC Power GPIO not ready");
        return -ENODEV;
    }

    /* 配置电源引脚 */
    err = gpio_pin_configure_dt(&nfc_pwr_gpio, GPIO_OUTPUT_INACTIVE);
    if (err) {
        LOG_ERR("Failed to configure NFC Power GPIO: %d", err);
        return err;
    }

    /* 初始化状态 */
    nfc_ctx.is_working = false;
    nfc_ctx.card_present = false;

    /* 初始化消息队列 */
    my_init_msg_handler(MOD_NFC, &my_nfc_msgq);

    /* 启动 NFC 线程 */
    *tid = k_thread_create(&my_nfc_task_data, my_nfc_task_stack,
                           K_THREAD_STACK_SIZEOF(my_nfc_task_stack),
                           my_nfc_task, NULL, NULL, NULL,
                           MY_NFC_TASK_PRIORITY, 0, K_NO_WAIT);

    k_thread_name_set(*tid, "MY_NFC");

    LOG_INF("NFC module initialized (via nfc_api)");
    return 0;
}
