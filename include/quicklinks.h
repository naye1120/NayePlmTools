#ifndef QUICKLINKS_H
#define QUICKLINKS_H

#include <stddef.h>

/* [CN-FORK] 「常用入口」快捷访问条目：存 /data/nayeplmtools/quicklinks.json。
 * 两类来源：src=manual（用户手动添加）/ src=auto（从 app.db pprDeeplinkUri 自动收录的
 * 已安装应用磁贴链接）。自动收录在 /quicklinks 请求时按需同步一次（stat app.db 的
 * mtime+size 闸门跳过无变化的情况），不轮询；用户删除的 URL 记入 hidden 抑制名单，
 * 之后不再被自动加回（手动重新添加则解除抑制）。
 * 条目点击直接跳转 URL；列表接口顺带对 URL 的 host:port 做 TCP 探活给出在线状态点。 */

#define QUICKLINKS_CONFIG_PATH "/data/nayeplmtools/quicklinks.json"
#define MAX_QUICKLINKS         32

/* 输出 {"entries":[{"name":..,"url":..,"src":"auto|manual","alive":bool},...]}，含逐条 TCP 探活。
 * 进入时先做一次 app.db 磁贴同步（需落盘时同步写入，锁外探活）。
 * 探活为同步串行（每条最多 ~400ms），条目数上限 32，最坏十几秒仅出现在整段
 * 局域网不可达时；返回 0。 */
int quicklinks_list_json(char *buf, size_t size);

/* 新增条目：name 必填（<=127 字符），url 必须以 http:// 或 https:// 开头（<=511 字符），
 * 重复 URL 拒绝；保存为 src=manual 并解除该 URL 的删除抑制。
 * 成功返回 0；失败返回 -1 并在 msg 填原因。 */
int quicklinks_add(const char *name, const char *url, char *msg, size_t msz);

/* 按下标删除（下标即 /quicklinks 返回的 entries 数组顺序）；删除的 URL 进抑制名单，
 * 避免 app.db 同步把 auto 条目原样加回。成功返回 0；失败返回 -1 并在 msg 填原因。 */
int quicklinks_remove(int index, char *msg, size_t msz);

#endif
