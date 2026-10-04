/* PS5 Account Tools — in-process NP fake sign-in / sign-out.
 *
 * A PS5-only reimplementation of earthonion's np-fake-signin, run inside the
 * PS5 Account Tools process instead of as a spawned ELF. On PS5 the payload only
 * writes auth.dat + config.dat and sets sceRegMgr keys — no HMAC key, no
 * account.dat/token.dat, and no ShellCore memory patch (all PS4-only). That
 * makes it safe to run directly with the escalated creds we already hold.
 *
 * These operate on an explicit local user (userId + name), so the accounts
 * page can act on any account, not just the foreground one. Requires the
 * account to be offline-activated first (non-zero account id in its slot).
 */

#pragma once

#include <stdint.h>

typedef struct {
  int      ok;
  char     message[320];
  uint32_t user_id;
  char     user_name[24];
  int      slot;         /* activation slot 1..16, or 0 if none */
  uint64_t account_id;   /* offline-activation account id from the registry */
  int      activated;    /* account id set + activation flags present */
  int      signed_in;    /* current sign-in flag state */
} npsignin_result_t;

/* Fill activation / sign-in status for the given user. */
int npsignin_status(uint32_t uid, const char *name, npsignin_result_t *out);

/* Perform the fake sign-in for the given user. */
int npsignin_signin(uint32_t uid, const char *name, npsignin_result_t *out);

/* 登出：把备份日志里**属于这个 uid** 的注册表记录逐条写回原值，并回灌它自己的
   auth.dat/config.dat。别的账号名下的记录一律不动。保留离线激活的 account_id。

   名下没有任何记录（= 这个登录态是别的工具写的，我们无从回滚）时，改走**基线登出**：
   signin_flag 归零、login_flag 写成实测的「已激活未登录」稳定态 0x1002，两个 NP
   会话文件先抄进备份层再从原位拿掉。写前逐键记原值，所以 Restore 仍能把那份登录
   状态原样放回去。抄不到原值就整笔否决，不做没有退路的删除。 */
int npsignin_signout(uint32_t uid, const char *name, npsignin_result_t *out);

/* 把某个账号在其 SysReg 槽里的 user_name 改成 new_name。写前记原值、写后回读
 * 比对、与别的槽查重，任一不满足都拒绝。成功后 out->user_name 是新名字。 */
int npsignin_rename(uint32_t uid, const char *old_name, const char *new_name,
                    npsignin_result_t *out);

/* 只读干跑（A3 知情预览）：枚举该账号执行 signin 会写的全部注册表 entity
   （只有它自己那一格，槽 1 不再被连带写）。不备份、不写注册表、
   不碰 auth.dat/config.dat。
   返回值：>0 = 该账号所在的槽（同时 ents/n_out 有效）；-1 = 没有可预览的写入
   计划（无槽或未激活）。 */
int npsignin_signin_plan(uint32_t uid, const char *name,
                         uint32_t *ents, int cap, int *n_out);
