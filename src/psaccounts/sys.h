/* PS5 Account Tools — minimal system support API.
 *
 * Standalone replacement for elf-arsenal's src/sys.h. Declares only the
 * handful of helpers this payload actually uses; everything runs in-process
 * so there is no launcher/spawn surface.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* Foreground (currently-selected) local user. Writes the user name into
   name_out (may be NULL) and returns the 32-bit userId, or 0 if none. */
uint32_t sys_get_foreground_user(char *name_out, size_t name_out_size);

/* [CN-FORK] v0.5.2.99 并入主 ELF 后由 src/main.c 显式调用一次：拿到 root uid/gid、
    unrestricted authid+capabilities，并把 rootdir/jaildir 指到真 root vnode。
   sceRegMgr 的账号槽读写、/system_data/priv 下的 NP 文件与 profile 缓存全靠它。
   必须在任何账号接口被调用之前执行（源工程是在 constructor 里做的，见 sys_lite.c 注释）。 */
void psaccounts_escalate(void);
