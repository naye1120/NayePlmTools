/* [CN-FORK] v0.5.2.111 — 「第一次登出会报错、第二次才能登出」的根因回归测试
 *
 * 用户原话：
 *   「第一次登出会报这个，并且登出失败，第二次才能登出。」
 *
 * 根因：`fill_status()` 原来**只按名字**找槽位（`find_slot()`），而名字来自
 * `sceUserServiceGetUserName()`。UserService 对一个刚激活、还没进过壳层用户列表的
 * 账号会滞后一帧 —— 首帧取到空串或旧名 → `find_slot()` 返回 0 → `out->slot = 0`
 * → 登出走「没有对应的注册表槽位，无法登出」直接失败。第二次点击时 UserService
 * 已经跟上，于是成功。
 *
 * 修法：槽基址 +0x100 那一格的**本地用户 id** 才是这个账号在系统里的稳定身份
 * （改名只写 +0x200 的 name，uid 一个字节都不动），所以 `fill_status()` 改成
 * **uid 优先、名字兜底**，并把槽里的真名回填进 `out->user_name`。
 *
 * 为什么必须 uid 优先而不是「名字优先、uid 兜底」：名字是全局唯一的。一旦
 * UserService 返回的是**另一个槽**的名字，名字路径会理直气壮地指向那一格 —— 那比
 * 找不到槽位更糟（会去读写别人的身份）。C 组就是钉住这条性质。
 *
 * 编译（WSL 主机 clang，不需要 PS5 SDK；与 test/offact_genid_test.c 同一套做法）：
 *   clang -Wall -Wextra -O0 -Isrc/psaccounts \
 *         src/psaccounts/npsignin.c test/slot_resolve_test.c -o /tmp/slot_resolve_test \
 *     && /tmp/slot_resolve_test
 *
 * 注意：本测试**不链接真实的 safety.c**，只打桩它对外的那几个函数。理由有二：
 *   ① safety.c 会真的往 SAFETY_DIR（/data/accounts/safety）建目录写文件，主机上跑不了；
 *   ② 这一轮要验的是「槽位解不解得对」，备份账本的行为另有 test/safety_origin_test.c 覆盖。
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "npsignin.h"
#include "safety.h"

/* ------------------------------------------------------------------ *
 * sceRegMgr 打桩：一个内存小表（照 test/offact_genid_test.c 的做法）     *
 * ------------------------------------------------------------------ */

#define STUB_MAX 64
#define STUB_BYTES 256

typedef struct {
  uint32_t ent;
  int      len;
  unsigned char buf[STUB_BYTES];
} stub_t;

static stub_t g_stub[STUB_MAX];
static int    g_nstub = 0;

static stub_t *stub_find(uint32_t ent)
{
  int i;
  for(i = 0; i < g_nstub; i++)
    if(g_stub[i].ent == ent) return &g_stub[i];
  return NULL;
}

static void stub_seed(uint32_t ent, const void *p, int len)
{
  stub_t *s = stub_find(ent);
  if(!s) { s = &g_stub[g_nstub++]; s->ent = ent; }
  memset(s->buf, 0, sizeof(s->buf));
  memcpy(s->buf, p, (size_t)len);
  s->len = len;
}

static int stub_read(uint32_t ent, void *out, int cap)
{
  stub_t *s = stub_find(ent);
  int n;
  if(!s) return -1;
  /* 真 sceRegMgrGetStr 写出的是 NUL 结尾的字符串，调用方的缓冲往往没初始化，
   * 所以把 cap 范围内剩下的字节一并清零 —— 否则读到的名字尾部是栈上的垃圾。 */
  memset(out, 0, (size_t)cap);
  n = s->len < cap ? s->len : cap;
  memcpy(out, s->buf, (size_t)n);
  return 0;
}

int sceRegMgrGetInt(int key, int32_t *value)
{
  return stub_read((uint32_t)key, value, 4) == 0 ? 0 : -1;
}

int sceRegMgrGetStr(int key, char *value, size_t size)
{
  return stub_read((uint32_t)key, value, (int)size) == 0 ? 0 : -1;
}

int sceRegMgrGetBin(int key, void *value, size_t size)
{
  return stub_read((uint32_t)key, value, (int)size) == 0 ? 0 : -1;
}

int sceRegMgrSetInt(int key, int32_t value)
{
  stub_seed((uint32_t)key, &value, 4);
  return 0;
}

int sceRegMgrSetStr(int key, const char *value, size_t size)
{
  stub_seed((uint32_t)key, value, (int)size);
  return 0;
}

int sceRegMgrSetBin(int key, const void *value, size_t size)
{
  stub_seed((uint32_t)key, value, (int)size);
  return 0;
}

/* ------------------------------------------------------------------ *
 * UserService / SystemService 打桩                                     *
 * ------------------------------------------------------------------ */

/* 这个桩就是被测的那件事本身：它模拟「UserService 滞后」。测试用例通过它决定
 * 调用方（accounts_action）会拿到什么名字 —— 空串、旧名、或者**别人的名字**。 */
static const char *g_usvc_name = "";

int sceUserServiceGetUserName(int32_t user_id, char *name, size_t max_size)
{
  (void)user_id;
  if(!name || max_size == 0) return -1;
  snprintf(name, max_size, "%s", g_usvc_name);
  return 0;
}

int sceUserServiceSetUserName(int32_t user_id, const char *name, size_t max_size)
{
  (void)user_id; (void)name; (void)max_size;
  return 0;
}

int sceSystemServiceParamGetInt(int32_t paramId, int32_t *value)
{
  (void)paramId;
  if(value) *value = 1;   /* en-US */
  return 0;
}

/* ------------------------------------------------------------------ *
 * safety 层打桩：账本恒为「名下 0 条记录」→ 登出走基线分支              *
 * ------------------------------------------------------------------ */

static int g_origin = -1;

int  safety_init(void) { return 0; }
void safety_set_scope(uint32_t uid) { (void)uid; }
void safety_set_origin(int origin) { g_origin = origin; }

int safety_backup_reg(const safety_key_t *keys, int n, uint32_t off)
{
  (void)keys; (void)off;
  return n;                      /* 全部「备份成功」 */
}

int safety_set_int(uint32_t ent, int32_t v) { return sceRegMgrSetInt((int)ent, v); }
int safety_set_str(uint32_t ent, const char *s, int size)
{
  return sceRegMgrSetStr((int)ent, s, (size_t)size);
}

int safety_reg_count(void) { return 0; }
int safety_reg_count_scope(uint32_t uid) { (void)uid; return 0; }
int safety_reg_count_scope_mask(uint32_t uid, uint32_t m) { (void)uid; (void)m; return 0; }

/* 返回 0 = 没有任何记录被回滚 → npsignin_signout 走「基线登出」分支 */
int safety_restore_scope_mask(uint32_t uid, uint32_t m) { (void)uid; (void)m; return 0; }

int safety_backup_file(const char *path) { (void)path; return 0; }
/* -1 = 这条路径没登记过 → 登出跳过文件那一步（本测试不验文件层） */
int safety_file_was_absent(const char *path) { (void)path; return -1; }
int safety_restore_file(const char *path) { (void)path; return 0; }

/* ------------------------------------------------------------------ *
 * 槽位常量（与 src/psaccounts/accounts.c 对齐）                          *
 * ------------------------------------------------------------------ */

#define SLOT_BASE       125829120u            /* 0x07800000 */
#define SLOT_STRIDE     65536u                /* 0x10000    */
#define ENT_SLOT_UID    (SLOT_BASE + 0x100u)  /* INT        */
#define ENT_SLOT_NAME   (SLOT_BASE + 0x200u)  /* STR17      */
#define ENT_SLOT_ACCT   (SLOT_BASE + 0x500u)  /* BIN8       */
#define ENT_SLOT_LOGIN  (SLOT_BASE + 0x800u)  /* INT        */
#define ENT_SIGNIN_FLAG 125874185u            /* 0x1F8，stride 同样 65536 */

#define UID_A  0x1A1D3629u
#define UID_B  0x122986B2u
#define UID_X  0xDEADBEEFu                    /* 不在任何槽里 */

#define ACCT_A 0x1122334455667788ULL
#define ACCT_B 0x9988776655443322ULL

static void seed_slot(int slot, uint32_t uid, const char *name,
                      uint64_t acct, int32_t signed_in)
{
  uint32_t off = (uint32_t)(slot - 1) * SLOT_STRIDE;
  int32_t v;

  v = (int32_t)uid;          stub_seed(ENT_SLOT_UID   + off, &v, 4);
  stub_seed(ENT_SLOT_NAME  + off, name, (int)strlen(name) + 1);
  stub_seed(ENT_SLOT_ACCT  + off, &acct, 8);
  v = signed_in ? 1 : 0;     stub_seed(ENT_SIGNIN_FLAG + off, &v, 4);
  v = signed_in ? 6 : 0x1002;
  stub_seed(ENT_SLOT_LOGIN + off, &v, 4);
}

/* ------------------------------------------------------------------ *
 * 断言                                                                *
 * ------------------------------------------------------------------ */

static int g_pass = 0, g_fail = 0;

static void chk(const char *what, int cond, const char *detail)
{
  if(cond) { g_pass++; printf("  ok   %s%s%s\n", what, detail ? "  — " : "", detail ? detail : ""); }
  else     { g_fail++; printf(" FAIL  %s%s%s\n", what, detail ? "  — " : "", detail ? detail : ""); }
}

/* ------------------------------------------------------------------ *
 * 用例                                                                *
 * ------------------------------------------------------------------ */

int main(void)
{
  npsignin_result_t st, so;
  char buf[256];

  /* 槽 1 = Alice（已登录），槽 3 = Bob（未登录）。故意跳开槽 2，好让「解错一格」
   * 这种情况一眼看得出来。 */
  seed_slot(1, UID_A, "Alice", ACCT_A, 1);
  seed_slot(3, UID_B, "Bob",   ACCT_B, 0);

  printf("\nA. 名字滞后成空串（UserService 首帧还没跟上）\n");
  g_usvc_name = "";
  npsignin_status(UID_A, g_usvc_name, &st);
  chk("A1 仍按 uid 定位到槽 1", st.slot == 1, NULL);
  snprintf(buf, sizeof(buf), "user_name=\"%s\"", st.user_name);
  chk("A2 槽里的真名回填进 user_name", !strcmp(st.user_name, "Alice"), buf);
  chk("A3 account_id 照样读到", st.account_id == ACCT_A, NULL);
  chk("A4 登录态照样读到", st.signed_in == 1, NULL);
  chk("A5 activated 照样读到", st.activated == 1, NULL);

  printf("\nB. 名字滞后成旧名（刚改过名）\n");
  g_usvc_name = "OldAlice";
  npsignin_status(UID_A, g_usvc_name, &st);
  chk("B1 仍按 uid 定位到槽 1", st.slot == 1, NULL);
  snprintf(buf, sizeof(buf), "user_name=\"%s\"", st.user_name);
  chk("B2 回填的是槽里的现名 Alice", !strcmp(st.user_name, "Alice"), buf);

  printf("\nC. 拿到的名字属于别的槽 —— uid 优先的关键性质\n");
  /* 若按名字解，\"Bob\" 会指到槽 3，那比「找不到槽位」更糟：会去动别人的身份。 */
  g_usvc_name = "Bob";
  npsignin_status(UID_A, g_usvc_name, &st);
  chk("C1 不会串到槽 3", st.slot == 1, NULL);
  chk("C2 回填的仍是自己槽里的名字", !strcmp(st.user_name, "Alice"), st.user_name);
  chk("C3 读到的仍是自己的 account_id", st.account_id == ACCT_A, NULL);

  printf("\nD. 名字对得上时行为不变（老路径回归）\n");
  g_usvc_name = "Alice";
  npsignin_status(UID_A, g_usvc_name, &st);
  chk("D1 槽 1 + Alice", st.slot == 1 && !strcmp(st.user_name, "Alice"), NULL);

  printf("\nE. uid 查不到时退回名字路径（兜底仍在）\n");
  g_usvc_name = "Bob";
  npsignin_status(UID_X, g_usvc_name, &st);
  chk("E1 未知 uid + 已知名字 → 槽 3", st.slot == 3, NULL);
  chk("E2 名字也回填成 Bob", !strcmp(st.user_name, "Bob"), st.user_name);
  g_usvc_name = "Nobody";
  npsignin_status(UID_X, g_usvc_name, &st);
  chk("E3 两边都查不到 → 槽 0", st.slot == 0, NULL);

  printf("\nF. 登出：名字滞后时不再报「没有槽位」（用户报的那个 BUG）\n");
  g_usvc_name = "";
  npsignin_signout(UID_A, g_usvc_name, &so, 1);
  snprintf(buf, sizeof(buf), "ok=%d message=\"%.120s\"", so.ok, so.message);
  chk("F1 第一次点登出就成功", so.ok == 1, buf);
  chk("F2 不再出现「没有对应的注册表槽位」",
      strstr(so.message, "没有对应的注册表槽位") == NULL, NULL);
  chk("F3 走的是基线登出（说明槽位认出来了、signed_in 也读到了）",
      strstr(so.message, "已按实测基线复位") != NULL, NULL);
  chk("F4 提示语里带的是真名 Alice", strstr(so.message, "Alice") != NULL, NULL);
  /* 基线登出把登录标志清掉、login_flag 写成「已激活未登录」 */
  {
    int32_t sf = -1, lf = -1;
    sceRegMgrGetInt((int)(ENT_SIGNIN_FLAG + 0), &sf);
    sceRegMgrGetInt((int)(ENT_SLOT_LOGIN + 0), &lf);
    chk("F5 signin_flag 已清零", sf == 0, NULL);
    chk("F6 login_flag 已写成 0x1002", lf == 0x1002, NULL);
  }
  chk("F7 基线登出的写入来源标成了 SIGNIN（登出按来源筛时才退得掉）",
      g_origin == SAFETY_ORIGIN_SIGNIN, NULL);

  printf("\nG. 登出：uid 不在任何槽里时如实报错（不能假装成功）\n");
  g_usvc_name = "";
  npsignin_signout(UID_X, g_usvc_name, &so, 1);
  chk("G1 失败", so.ok == 0, NULL);
  chk("G2 说的是没有槽位",
      strstr(so.message, "没有对应的注册表槽位") != NULL, so.message);

  printf("\n%d passed, %d failed\n\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
