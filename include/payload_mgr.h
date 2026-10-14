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

/* [CN-FORK] 启动自检：将已安装的 NayePlmTools 同步到 ps5_autoloader，
 * 解决旧版本没有 sync 代码导致首次升级到 .20 时 ps5_autoloader 未更新的问题 */
void payload_mgr_bootstrap_self_sync(void);

#endif
