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
void pldmgr_json_escape(const char *src, char *dst, size_t dst_size);

#ifdef __cplusplus
}
#endif
