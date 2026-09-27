#ifndef QUICKLINKS_H
#define QUICKLINKS_H

#include <stddef.h>

/* [CN-FORK] 「常用入口」自定义快捷访问条目：存 /data/nayeplmtools/quicklinks.json。
 * 内置已知服务自动探测表暂未实现，第一版只做手动维护的 URL 清单。
 * 条目点击直接跳转 URL；列表接口顺带对 URL 的 host:port 做 TCP 探活给出在线状态点。 */

#define QUICKLINKS_CONFIG_PATH "/data/nayeplmtools/quicklinks.json"
#define MAX_QUICKLINKS         32

/* 输出 {"entries":[{"name":..,"url":..,"alive":bool},...]}，含逐条 TCP 探活。
 * 探活为同步串行（每条最多 ~400ms），条目数上限 32，最坏十几秒仅出现在整段
 * 局域网不可达时；返回 0。 */
int quicklinks_list_json(char *buf, size_t size);

/* 新增条目：name 必填（<=127 字符），url 必须以 http:// 或 https:// 开头（<=511 字符），
 * 重复 URL 拒绝。成功返回 0；失败返回 -1 并在 msg 填原因。 */
int quicklinks_add(const char *name, const char *url, char *msg, size_t msz);

/* 按下标删除（下标即 /quicklinks 返回的 entries 数组顺序）。
 * 成功返回 0；失败返回 -1 并在 msg 填原因。 */
int quicklinks_remove(int index, char *msg, size_t msz);

#endif
