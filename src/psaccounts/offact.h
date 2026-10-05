/* Elf Arsenal — offline account activation.
   Vendored from ps5-payload-dev/offact (GPL-3.0). The original ships
   an SDL2 fullscreen UI; we keep only the registry side-effects and
   drive them from the web UI. */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define OFFACT_SLOT_COUNT      16
#define OFFACT_NAME_MAX        32
#define OFFACT_TYPE_MAX        17
#define OFFACT_DEFAULT_FLAGS   0x1002

int      offact_get_name(int slot, char out[OFFACT_NAME_MAX]);
/* 写 user_name 的入口已被删除：上游用 32 长度写声明为 17 的字段会越界覆盖后
 * 15 字节，而本工程不需要它。改名见 npsignin_rename()。 */
int      offact_get_id(int slot, uint64_t *out);
int      offact_set_id(int slot, uint64_t id);
int      offact_get_type(int slot, char out[OFFACT_TYPE_MAX]);
int      offact_set_type(int slot, const char *type);
int      offact_get_flags(int slot, int *out);
int      offact_set_flags(int slot, int flags);
uint64_t offact_gen_id(const char *name);

/* [CN-FORK] v0.5.2.111 未激活账号「激活时将生成」的那个 id。
 *
 * 上游是 offact_gen_id(名字) —— 于是**改个名这个 id 就跟着变**：用户在界面上看到的
 * 「账号 ID」会在点完「修改」之后自己换一个值，而按常识账号 ID 一旦生成就不该被改名
 * 影响（只有显式改 ID 才该变）。所以这里改成按**本地用户 id** 派生：uid 是这个账号在
 * 系统里的稳定身份（改名只写注册表 0x200 的 name 字段，0x100 的 uid 一个字节都不动），
 * 拿它当种子才谈得上「生成后就不再变」。
 *
 * 派生算法与 offact_gen_id() 同一套 FNV-1a，只是种子换成 uid 的 4 个小端字节。 */
uint64_t offact_gen_id_for_user(uint32_t uid);

int offact_activate(int slot, uint64_t id);

/* Zero out id+flags (leaves the name and type untouched so the slot
   is still recognisable but no longer "activated"). */
int offact_clear(int slot);
