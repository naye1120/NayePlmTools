#ifndef PAYLOAD_MGR_H
#define PAYLOAD_MGR_H

#include <stddef.h>

/*
 * Populates json_buf with items (slot layout) and usb payloads.
 * Returns the complete length, or 0 with empty output on serialization failure.
 */
size_t payload_mgr_list_json(char *json_buf, size_t buf_size);

/* Delete payload by slot key or filename */
int payload_mgr_delete_payload(const char *slot_or_filename);

/* Import file to managed storage */
int payload_mgr_import_to_storage_mode(const char *filename, const char *temp_path,
                                       const char *mode, const char *install_source,
                                       const char *install_source_detail,
                                       char *msg_buf, size_t msg_buf_size,
                                       char *out_slot, size_t out_slot_size);

/* Check if payload exists for web upload check */
int payload_mgr_check_existing(const char *filename, char *out_json, size_t out_size);

/* USB Import */
int payload_mgr_usb_check(const char *usb_path, char *out_json, size_t out_size);
int payload_mgr_usb_move(const char *usb_path, const char *mode, int keep_original,
                         char *out_json, size_t out_size);

/* [CN-FORK] v0.6.0.178 引导器同步（实现在 payload_mgr.c）。
 *
 * 把刚装好的载荷同步进越狱引导器目录 /data/ps5_autoloader：
 *   · autoload.txt 里已登记同一工具（按去掉版本号的工具名匹配）→ 换成新版本：
 *     复制新 ELF 进目录、删掉旧版本文件、改写 txt；
 *   · 没登记、allow_register=1 → 追加登记（只对本工具用，别顺手把别人的载荷塞进自启）；
 *   · 没登记、allow_register=0 → 什么都不做。
 * 返回值 0 = 已更新/已登记，1 = 无需处理，-1 = 失败。每条分支都会写日志 ——
 * 旧实现有三处静默 return，真机上「上传成功但引导器没变」根本查不出卡在哪一环。 */
int payload_mgr_autoloader_sync(const char *new_filename, const char *new_file_path,
                                int allow_register);

/* [CN-FORK] 这个载荷文件名是不是「本工具自己」（autoload.txt 按工具名匹配）。 */
int payload_mgr_is_self_payload(const char *filename);

/* [CN-FORK] 启动对账：把引导器目录里那份本工具对齐到 storage / 自更新目录里最新的一份。
 * v0.6.0.178 重写 —— 旧实现找的是 slot 化之前的旧布局 payloads/<工具名>/，v0.6.0 起载荷
 * 落在 payloads/local|repo/<key>/，那个目录根本不存在，整段成了死代码。 */
void payload_mgr_bootstrap_self_sync(void);

#endif
