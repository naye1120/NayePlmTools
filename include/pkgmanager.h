/*
 * [CN-FORK] PKG Manager 内置集成
 *
 * 把 pkg-manager ELF 作为黑盒内置进本 payload：编译期经 src/pkgmanager_blob.S
 * (incbin) 嵌入基线，运行时播种到 /data/nayeplmtools/pkgmanager/ 并通过
 * elfldr (ps5_launch_elf) 拉起独立守护进程（其 UI 端口 8844，与本服务 8911
 * 互不冲突）。更新走「PKG安装」页手动上传覆盖 /data 副本，内置基线只做离线
 * 保底、永不覆盖用户已上传的文件。
 */
#pragma once

#include <stddef.h>

#include "pldmgr.h"

#define PKGMGR_DIR            BASE_DATA_DIR "/pkgmanager"
#define PKGMGR_ELF_PATH       PKGMGR_DIR "/pkg-manager.elf"
#define PKGMGR_VERSION_PATH   PKGMGR_DIR "/version.txt"
#define PKGMGR_TMP_PATH       PKGMGR_ELF_PATH ".tmp"
#define PKGMGR_UI_PORT        8844 /* PKG Manager 自带 Web UI/REST 端口（其源码 DEFAULT_HTTP_PORT） */
/* elfldr 拉起的 payload，其进程名（kinfo_proc.ki_comm）就是 ELF 文件名本身，
 * 因此这里必须始终等于 PKGMGR_ELF_PATH 的 basename，改动落盘文件名要同步改。 */
#define PKGMGR_PROC_NAME      "pkg-manager.elf"

/* 内置基线版本，须与 third_party/pkg-manager.elf 保持一致 */
#define PKGMGR_EMBEDDED_VERSION "1.4.0-CN"

/* 从 ELF 二进制里扫 PKGMGR_VER:<version> 签名串。注意：PKGManager 自己的 Makefile 用
 * -fdata-sections + --gc-sections，该签名无代码引用会被链接器回收，实际发布的 ELF 里
 * 通常不存在，因此只作首选线索、不作硬校验。命中返回 0，未命中返回 -1。 */
int pkgmanager_extract_version(const void *buf, size_t len, char *out, size_t osz);

/* /data 下没有 ELF 文件时把内置基线播种出去并写 version.txt；已存在则不动（保护用户上传的更新）。
 * 返回 0 成功（或无需播种），-1 失败并填 err。 */
int pkgmanager_seed_if_needed(char *err, size_t esz);

/* TCP 探活 127.0.0.1:8844，1=PKG Manager 正在运行 */
int pkgmanager_is_running(void);

/* 播种（如需）后经 elfldr 拉起 PKG Manager。返回 0 成功；失败返回 -1 并填 msg。 */
int pkgmanager_launch(char *msg, size_t msz);

/* [CN-FORK] 关闭 PKG Manager：枚举进程并 SIGKILL 名字命中 PKGMGR_PROC_NAME 的进程。
 * 返回 0 表示已发出信号或本来就没在跑；返回 -1 表示失败（枚举出错、或端口仍被占
 * 但找不到对应进程），两种情况都会填 msg。 */
int pkgmanager_stop(char *msg, size_t msz);

/* 组装状态 JSON：running/port/embedded_version/installed_version/file */
void pkgmanager_status_json(char *out, size_t osz);

/* 校验并落盘手动上传的 ELF（tmp_path 为已写完的 .tmp 文件，filename_hint 为浏览器原始
 * 文件名仅用于猜版本号）：ELF 头 + 体积区间 + 二进制含 "PKG Manager" 字符串才收；
 * 版本按 PKGMGR_VER 签名 → 文件名 → "unknown" 顺序提取，rename 原子覆盖并更新 version.txt。
 * 返回 0 成功并把版本号写入 msg；失败返回 -1 并在 msg 填原因（tmp 会被删除）。 */
int pkgmanager_commit_upload(const char *tmp_path, const char *filename_hint, char *msg, size_t msz);
