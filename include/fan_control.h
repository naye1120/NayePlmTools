#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include <stddef.h>

/* [CN-FORK] 风扇温控（模型 A + 常驻重发守护）：只改 /dev/icc_fan autoservo 的「目标温度」字节，
 * 交给 PS5 固件自己的热伺服闭环调速——payload 不实现风扇曲线、不写手动 duty、不碰传感器 API。
 * 协议出处：drakmor/fan_target 技术参考 + owendswang/ps5-fan-control + ShadowMountPlusFan。
 *
 * 状态模型（用户确认）：
 *   - 开关 enabled：打开功能即持久化，但「开启后默认失效」——不写硬件，重启/升级后仍保持启用；
 *   - 应用 applied：设定温度后点「应用」才真正下发、生效；
 *   - 守护线程：仅 enabled && applied 时按 interval 秒检测漂移并自动拉回（对抗进游戏复位）。
 *
 * 安全闸（务必保留）：
 *   1) safe_mode 或打不开 /dev/icc_fan → supported:false，一个 ioctl 都不发；
 *   2) 目标温度强制夹在 [60,85]°C——全部低于原厂 ~91°C，只会比出厂更凉、绝不更热；
 *   3) SET 前必先 GET 当前 28 字节对象、只改 byte5、SET 整个对象、再 GET 回读校验，
 *      绝不用全零缓冲直接 SET（会清零其它控制器参数）；
 *   4) 所有 ioctl 与运行态读写用同一把 pthread_mutex 串行化。 */

/* 在 main 启动序列里调用一次：从 config 恢复运行态并拉起常驻重发守护线程（幂等）。 */
void fan_control_start_daemon(void);

/* 读风扇温控状态 JSON（只读，绝不写硬件）：
 * {"supported":..,"enabled":..,"applied":..,"target":..,"hw_target":..,"min":..,"max":..,"interval":..}
 * 返回写入长度。 */
int fan_control_status_json(char *buf, size_t size);

/* 切换启用开关：仅持久化 enabled 并清除 applied（开启后默认失效），不碰硬件。
 * 输出 {"ok":true,"supported":true,"enabled":..,"applied":false,"target":..,"message":"switch_set"}。
 * 返回写入长度。 */
int fan_control_set_enabled_json(char *buf, size_t size, int enabled);

/* 应用（生效）：校验 target 后 GET-modify-SET-verify 下发到硬件，成功则置 enabled=1、applied=1 并持久化。
 * 输出 {"ok":..,"supported":..,"enabled":..,"applied":..,"target":..,"message":".."}。返回写入长度。 */
int fan_control_apply_json(char *buf, size_t size, int target_c);

#endif
