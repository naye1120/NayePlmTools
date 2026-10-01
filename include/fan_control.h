#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include <stddef.h>

/* [CN-FORK] 风扇温控（模型 A）：只改 /dev/icc_fan autoservo 的「目标温度」字节，
 * 交给 PS5 固件自己的热伺服闭环调速——payload 不实现风扇曲线、不轮询、不写手动 duty。
 * 协议出处：drakmor/fan_target 技术参考 + owendswang/ps5-fan-control。
 *
 * 安全闸（务必保留）：
 *   1) safe_mode 或打不开 /dev/icc_fan → supported:false，一个 ioctl 都不发；
 *   2) 目标温度强制夹在 [60,85]°C——全部低于原厂 ~91°C，只会比出厂更凉、绝不更热；
 *   3) SET 前必先 GET 当前 28 字节对象、只改 byte5、SET 整个对象、再 GET 回读校验，
 *      绝不用全零缓冲直接 SET（会清零其它控制器参数）；
 *   4) 所有 ioctl 用 pthread_mutex 串行化。
 *
 * 已知限制：进游戏会触发 pattern 切换，ICC 把目标复位到出厂值。本版在每次读状态时
 * 检测漂移并重新下发（无后台守护线程），故用户刷新温控页即会把目标拉回。 */

/* 读风扇温控状态 JSON：{"supported":..,"enabled":..,"target":..,"hw_target":..,"min":..,"max":..}
 * 若已启用且硬件目标漂移，顺带重新下发设定值。返回写入长度。 */
int fan_control_status_json(char *buf, size_t size);

/* 应用用户操作：enabled=0 关闭（仅持久化，硬件保持当前值）；enabled=1 校验 target 后
 * GET-modify-SET-verify 下发并持久化。输出 {"ok":..,"supported":..,"enabled":..,"target":..,"message":".."}。
 * 返回写入长度。 */
int fan_control_apply_json(char *buf, size_t size, int enabled, int target_c);

#endif
