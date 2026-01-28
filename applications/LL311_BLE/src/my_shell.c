/********************************************************************
**版权所有         深圳市几米物联有限公司
**文件名称:        my_shell.c
**文件描述:        Shell 命令行交互模块实现（基于 RTT）
**当前版本:        V1.0
**作    者:        Harrison Wu (wuyujiao@jimiiot.com)
**完成日期:        2026.01.22
*********************************************************************
** 功能描述:        注册自定义 Shell 命令，用于系统诊断和设备控制
*********************************************************************/

#include "my_comm.h"

#define LOG_MODULE_NAME my_shell
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

/********************************************************************
**函数名称:  cmd_system_info
**入口参数:  shell   ---        Shell 实例指针
**           argc    ---        参数数量
**           argv    ---        参数数组
**出口参数:  无
**函数功能:  输出系统信息（示例命令）
**返 回 值:  0 表示成功
*********************************************************************/
static int cmd_system_info(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "=== System Information ===");
    shell_print(shell, "Device: nRF54L15");
    shell_print(shell, "Build Time: %s %s", __DATE__, __TIME__);
    shell_print(shell, "Uptime: %lld ms", k_uptime_get());
    return 0;
}

/********************************************************************
**函数名称:  cmd_ble_info
**入口参数:  shell   ---        Shell 实例指针
**           argc    ---        参数数量
**           argv    ---        参数数组
**出口参数:  无
**函数功能:  输出蓝牙状态信息（示例命令）
**返 回 值:  0 表示成功
*********************************************************************/
static int cmd_ble_info(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "=== BLE Status ===");
    shell_print(shell, "Device Name: Harrison_UART_Service");
    shell_print(shell, "Advertising: Active");
    return 0;
}

/********************************************************************
**函数名称:  cmd_mem_stat
**入口参数:  shell   ---        Shell 实例指针
**           argc    ---        参数数量
**           argv    ---        参数数组
**出口参数:  无
**函数功能:  输出内存使用统计（示例命令）
**返 回 值:  0 表示成功
*********************************************************************/
static int cmd_mem_stat(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "=== Memory Statistics ===");
    shell_print(shell, "Heap Size: %d bytes", CONFIG_HEAP_MEM_POOL_SIZE);
    shell_print(shell, "Stack Usage: Check with 'kernel stacks'");
    return 0;
}

/********************************************************************
**函数名称:  cmd_reboot
**入口参数:  shell   ---        Shell 实例指针
**           argc    ---        参数数量
**           argv    ---        参数数组
**出口参数:  无
**函数功能:  系统重启命令
**返 回 值:  0 表示成功
*********************************************************************/
static int cmd_reboot(const struct shell *shell, size_t argc, char **argv)
{
    shell_print(shell, "System rebooting...");
    k_sleep(K_MSEC(500));
    sys_reboot(SYS_REBOOT_WARM);
    return 0;
}

/* 注册自定义命令到 Shell 子系统 */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_app,
    SHELL_CMD(sysinfo, NULL, "Display system information", cmd_system_info),
    SHELL_CMD(bleinfo, NULL, "Display BLE status", cmd_ble_info),
    SHELL_CMD(memstat, NULL, "Display memory statistics", cmd_mem_stat),
    SHELL_CMD(reboot, NULL, "Reboot system", cmd_reboot),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_REGISTER(app, &sub_app, "Application commands", NULL);

/********************************************************************
**函数名称:  my_shell_init
**入口参数:  无
**出口参数:  无
**函数功能:  初始化 Shell 模块（Zephyr Shell 自动初始化，此处仅做日志输出）
**返 回 值:  0 表示成功
*********************************************************************/
int my_shell_init(void)
{
    LOG_INF("Shell module initialized (RTT backend)");
    LOG_INF("Use 'app sysinfo', 'app bleinfo', etc. to interact");
    return 0;
}