#ifndef THERMAL_H
#define THERMAL_H

#include <stddef.h>

/* [CN-FORK] 温控只读端点：输出 CPU / GPU(sensor0) / SSD(sensor1) 温度 + 风扇占空比快照。
 * 移植自 v0.5.1.63 stats.c 的温度/风扇采集与「传感器安全闸」，只保留温控所需最小集，
 * 不把型号/序列号/FPS/DCE 等历史包袱带回。
 *
 * 安全闸（务必保留，否则重演整机死机）：
 *   1) safe_mode 哨兵存在时一个内核调用都不发，直接回 {"enabled":false}；
 *   2) 所有 Sensor API 用 pthread_mutex 串行化（Sony 这些 API 不可重入）；
 *   3) 5s 快照缓存，并发/轮询命中缓存，最多每 5s 才真打一次内核。
 *
 * 输出示例：
 *   {"enabled":true,"cpu":{"temp":48},"gpu":{"temp":51},"ssd":{"temp":44},"fan":{"duty":32,"max":100}}
 *   safe_mode 时：{"enabled":false}
 * 温度读不到填 -1，前端据此显示占位。返回写入长度。 */
int thermal_stats_json(char *buf, size_t size);

#endif
