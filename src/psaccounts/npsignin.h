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
  /* [CN-FORK] v0.5.2.101 改名要逐层如实回报（注册表 / UserService / profile 缓存 /
   * username.dat），320 字节装不下这一整条诊断串，会截成半句话。 */
  char     message[768];
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
   auth.dat/config.dat。别的账号名下的记录一律不动。

   [CN-FORK] v0.5.2.107 加 keep_activation 开关 —— 登出不再必然连带反激活：
     keep_activation = 1（界面上的「登出」）    只退登录态（来源 SIGNIN + 未标注的老
       记录），离线激活写的 account_id/type/flags **留着**，下次可直接再登入。
     keep_activation = 0（界面上的「登出并反激活」）不分来源全退，账号回到未激活。

   名下没有任何记录（= 这个登录态是别的工具写的，我们无从回滚）时，改走**基线登出**：
   signin_flag 归零、login_flag 写成实测的「已激活未登录」稳定态 0x1002，两个 NP
   会话文件先抄进备份层再从原位拿掉。写前逐键记原值，所以 Restore 仍能把那份登录
   状态原样放回去。抄不到原值就整笔否决，不做没有退路的删除。 */
int npsignin_signout(uint32_t uid, const char *name, npsignin_result_t *out,
                     int keep_activation);

/* 把某个账号在其 SysReg 槽里的 user_name 改成 new_name。写前记原值、写后回读
 * 比对、与别的槽查重，任一不满足都拒绝。成功后 out->user_name 是新名字。
 *
 * [CN-FORK] v0.5.2.101 显示源不止注册表一层，所以这里逐层尽力而为：① SysReg 槽
 * （硬条件，失败即整笔否决）② UserService ③ profile 缓存 online.json 的 firstName
 * ④ /user/home/<uid>/username.dat。后三层只在格式对得上时才动，写没写成都作为
 * 一段话拼进 message，供实机判定「主界面/切换用户列表到底读哪一层」。 */
int npsignin_rename(uint32_t uid, const char *old_name, const char *new_name,
                    npsignin_result_t *out);

/* 只读干跑（A3 知情预览）：枚举该账号执行 signin 会写的全部注册表 entity
   （只有它自己那一格，槽 1 不再被连带写）。不备份、不写注册表、
   不碰 auth.dat/config.dat。
   返回值：>0 = 该账号所在的槽（同时 ents/n_out 有效）；-1 = 没有可预览的写入
   计划（无槽或未激活）。 */
int npsignin_signin_plan(uint32_t uid, const char *name,
                         uint32_t *ents, int cap, int *n_out);
