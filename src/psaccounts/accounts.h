/* PS5 Account Tools — unified account view.
 *
 * Joins the local login users with their offline-activation slot and NP
 * sign-in state into a single list, and dispatches per-account actions
 * (activate / deactivate / sign-in / sign-out). All in-process.
 */

#pragma once

#include <stdint.h>

/* JSON: {"ok":true,"mainUserId":N,"currentUserId":N,"accounts":[{...}, ...]}.
   账号来源是扫 16 个 SysReg 槽（名字 + user_id + account_id + login_flag），
   不是登录列表，所以切走前台的账号也一直在列表里。Caller frees. */
char *accounts_list_json(void);

/* action = "activate" | "deactivate" | "signin" | "signout" | "rename".
   custom_id: for "activate", use this account ID instead of auto-generating
              (pass 0 to auto-generate from username).
   arg:       for "rename", the new user name; NULL for the other actions.
   Returns {"ok":bool,"message":"...","account":{...}}. Caller frees. */
char *accounts_action(const char *action, uint32_t user_id, uint64_t custom_id,
                      const char *arg);

/* mode = "status" → 备份清单；"restore" → 写回所有原值并还原登记过的文件。
   Returns {"ok":bool,"message":"...",...}. Caller frees. */
char *accounts_safety_json(const char *mode);

/* 主账号（自动登录的那个）的 userId，来自 SysReg 全局键 auto_login_user
   @0x07010000。读不到或读到 0 都返回 0 = 无法判定。只用于界面标注，
   不再作为禁写依据。只读。 */
uint32_t accounts_main_uid(void);

/* 只读：把 signin 会写的注册表键列成 JSON。去掉连带写 base 槽之后，这些键全部
   落在目标槽；JSON 仍分开报 base/target 两个计数，好让界面能当场证实没串槽。
   Returns {"ok":bool,...}. Caller frees. */
char *accounts_preview_json(uint32_t user_id);

/* 只读：让系统库现场生成一个真正的远程游玩配对 PIN（8 位数字，约 5 分钟有效）。
 * 与主机菜单「设定 → 系统 → 远程游玩 → 与装置配对」显示的是同一个码，来源是
 * libSceRemoteplay 的 sceRemoteplayGeneratePinCode()。
 * 不写注册表、不碰任何文件。
 * 会话在这一步**故意不收尾**：配对要靠 sceRemoteplayConfirmDeviceRegist 轮询落账，
 * 出完码就把服务关了的话 Chiaki 能"添加主机"却连不上（已实测，就是这么坏的）。
 * 收尾由下面三条路保证其一发生：配对成功、配对失败、180 秒超时（PIN_TTL_SEC）—— 前两条走
 * accounts_pin_status_json()，超时由进程内的看门狗线程收（不看网页还在不在），
 * 进程退出时再由 accounts_pin_shutdown() 兜一次底。
 * Initialize 失败会如实报 initRc；生成失败会当场收尾并报 genRc/termRc。
 * 收尾失败（termRc 非 0）时，本函数与状态接口此后一律拒绝再点：真机上曾有一次
 * 进程带着已初始化的会话退出，服务被死进程占住，之后每次 Initialize 都返回
 * 0x80fc0003，直到重启主机才解锁 —— 宁可少出一次码，也不重复制造占用。
 * PIN 只对**当前前台账号**有效，所以这里不收槽参数。
 * Returns {"ok":bool,"status":"waiting","pin":"2836 0549","pinRaw":"28360549",
 *          "userId":...,"name":"...","slot":N,"accountId":"0x..",
 *          "accountIdB64":"..","expiresIn":180,"remaining":180,"message":"..."}
 * Caller frees. */
char *accounts_pin_json(void);

/* 只读：上一条 PIN 配到哪一步了。网页每秒问一次。可能的 status：
 *   idle    —— 当前没有进行中的配对（ok:false）
 *   waiting —— 还在等对方填码，带 remaining 剩余秒数与 stat
 *   paired  —— 配对成功（ConfirmDeviceRegist stat=2），已收尾，带 termRc
 *   failed  —— 配对失败（stat=3），带 pairErr（0x80FC1047=PIN 错，
 *              0x80FC1040=Account ID 错），已收尾
 *   expired —— 180 秒到点（PIN_TTL_SEC），已收尾
 * Returns {...} JSON. Caller frees. */
char *accounts_pin_status_json(void);

/* 进程退出前关掉还开着的远程游玩会话（幂等）。不关的话服务会被死进程占住，
   下次 Initialize 稳定吃 0x80fc0003。 */
void accounts_pin_shutdown(void);
