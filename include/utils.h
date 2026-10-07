#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gets the local IP address. Returns 0 on success, -1 on failure. */
int pldmgr_get_local_ip(char *ip_buf, size_t buf_size);
void pldmgr_utils_get_payload_folder_name(const char *filename, char *out_buf, size_t out_size);

/* [CN-FORK] 规整 autoload.txt 的一行：剥掉 UTF-8 BOM 与首尾空白（含 CRLF）。
 * 实机这份文件常被 Windows 记事本手改（BOM + CRLF + 缩进），不剥掉会让
 * 行匹配与 folder_name 提取全部失效，导致重复追加条目或条目认不出来。 */
void pldmgr_utils_strip_config_line(char *s);

/* [CN-FORK] 安全模式哨兵：PLDMGR_SAFE_MODE_FLAG 存在即返回 1。用来在实机死机、
 * 又进不了页面时靠 FTP 建一个空文件止血（免重编译）。 */
int pldmgr_utils_safe_mode(void);
void pldmgr_json_escape(const char *src, char *dst, size_t dst_size);

/* [CN-FORK] v0.5.2.142 libcurl 的全局初始化，**全进程只做一次**。
 *
 * curl 官方文档写得很明确：`curl_global_init` 不是线程安全的，必须在起任何线程
 * 之前、或至少保证只被调用一次。原来 `game_runtime.c` 自带一个 pthread_once ——
 * 现在 `save_remote.c`（自动存档的传输层）也要 curl，两个各自持一把 once 控制块
 * 就会变成「两个线程各自认为自己是第一个」，正好踩中那个竞态。
 * 所以收到这里，谁用谁调，内部 pthread_once 保证只执行一次。
 *
 * ⚠ 实现**不在 utils.c**，在 src/curl_init.c：utils.c 被一堆纯文件系统的宿主
 * 测试链着（game_saves / media_scan / game_mgr），curl 符号一进 utils.o 它们
 * 就全编不过了。见 src/curl_init.c 文件头。 */
void pldmgr_curl_init(void);

#ifdef __cplusplus
}
#endif
