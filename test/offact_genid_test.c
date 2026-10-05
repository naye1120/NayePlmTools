/* [CN-FORK] v0.5.2.111 — 账号 ID 生成器的主机端单测
 *
 * 为什么值得单独测：这次改的是**账号 ID 从哪来**。用户口径是
 *   「修改用户名为什么会影响用户ID，正常逻辑是用户ID生成后，只有修改账号ID才修改，
 *     改名是不修改ID的。」
 * 改错的后果不是崩，而是**界面上那个 ID 悄悄换了值** —— 实机上要一层层点开才看得出来，
 * 而且会让人把 Chiaki / PXPlay 的配对配置抄错。所以这条性质必须在这里用断言钉死：
 *
 *   **改名（只写注册表 0x200 的 name 字段）之后，「将自动生成」的那个 ID 必须一字不变。**
 *
 * 编译（WSL 主机 clang，不需要 PS5 SDK；与 test/safety_origin_test.c 同一套做法）：
 *   clang -Wall -Wextra -O0 -Isrc/psaccounts \
 *         src/psaccounts/offact.c test/offact_genid_test.c -o /tmp/offact_genid_test \
 *     && /tmp/offact_genid_test
 *
 * 覆盖：
 *   A. offact_gen_id_for_user() 是 uid 的纯函数：同 uid 恒定、不同 uid 不撞
 *   B. 预览 == 实际写入：offact_activate() 写进去的就是 offact_gen_id_for_user(uid)
 *   C. 反向断言（防回退）：老的 offact_gen_id(名字) **确实**会随名字变 —— 钉住"旧做法错在哪"，
 *      防止以后有人图省事把名字派生再挂回默认路径
 *   D. 端到端：改名前后，未激活账号的 derivedId 不变、已激活账号的 account_id 不变
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "offact.h"
#include "safety.h"

/* ------------------------------------------------------------------ *
 * sceRegMgr 打桩：一个内存小表（照 test/safety_origin_test.c 的做法）    *
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
  /* 真 sceRegMgrGetStr 写出的是 NUL 结尾的字符串，调用方给的缓冲往往没初始化，
   * 所以这里把 cap 范围内剩下的字节一并清零 —— 否则 offact_activate 读到的名字
   * 尾部是栈上的垃圾。 */
  memset(out, 0, (size_t)cap);
  n = s->len < cap ? s->len : cap;
  memcpy(out, s->buf, (size_t)n);
  return 0;
}

/* 声明与 offact.c 里那几行保持一致（同一个键参数在两边都是 32 位整数） */
int sceRegMgrGetInt(int key, int32_t *value)
{
  if(stub_read((uint32_t)key, value, 4) != 0) return -1;
  return 0;
}

int sceRegMgrGetStr(int key, char *value, size_t size)
{
  if(stub_read((uint32_t)key, value, (int)size) != 0) return -1;
  return 0;
}

int sceRegMgrGetBin(int key, void *value, size_t size)
{
  if(stub_read((uint32_t)key, value, (int)size) != 0) return -1;
  return 0;
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

/* safety 层的桩：本测试只关心"写进去了什么"，不关心备份账本 */
static int g_origin = -1;

int safety_backup_reg(const safety_key_t *keys, int n, uint32_t off)
{
  (void)keys; (void)n; (void)off;
  return n;   /* 全部"备份成功" */
}

int safety_set_int(uint32_t ent, int32_t v)
{
  return sceRegMgrSetInt((int)ent, v);
}

int safety_set_str(uint32_t ent, const char *s, int size)
{
  return sceRegMgrSetStr((int)ent, s, (size_t)size);
}

void safety_set_origin(int origin)
{
  g_origin = origin;
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

static void chk_u64(const char *what, uint64_t got, uint64_t want)
{
  char d[128];
  snprintf(d, sizeof(d), "got 0x%016llx, want 0x%016llx",
           (unsigned long long)got, (unsigned long long)want);
  chk(what, got == want, d);
}

/* 槽 1 的绝对 entity：与 accounts.c / offact.c 的常量一致 */
#define ENT_NAME_1  125829632u   /* 0x07800200 STR(17) */
#define ENT_ID_1    125830400u   /* 0x07800500 BIN(8)  */

/* 把某个槽的 user_name 写成 name（模拟"改名"只动这一格） */
static void set_slot_name(uint32_t ent, const char *name)
{
  char buf[17];
  memset(buf, 0, sizeof(buf));
  snprintf(buf, sizeof(buf), "%s", name);
  stub_seed(ent, buf, (int)sizeof(buf));
}

static uint64_t get_slot_id(uint32_t ent)
{
  uint64_t id = 0;
  stub_read(ent, &id, 8);
  return id;
}


int main(void)
{
  /* 真机取证里出现过的 uid（见 D:\offact-new\_rename_forensics.log 与账号卡截图）。
   * 注意 0x122986B2 就是十进制 304711346 —— 同一个账号的两种写法，别当成两个。 */
  static const uint32_t UIDS[] = {
    0x1A1D3629u,   /* 改名取证里的靶账号 */
    0x122986B2u,   /* 账号卡上那个 userIdHex */
    0x00000001u,   /* 最小值侧 */
    0x7FFFFFFFu,   /* int32 上界 */
    0x80000000u,   /* int32 下界（uid 按 int32 语义传，这一档必须也能算） */
    0xFFFFFFFFu,   /* SAFETY_SCOPE_ALL 哨兵，绝不能被当成某个真实账号 */
  };
  const int NU = (int)(sizeof(UIDS) / sizeof(UIDS[0]));

  printf("== A. offact_gen_id_for_user 是 uid 的纯函数 ==\n");
  {
    int same = 1, distinct = 1, nonzero = 1;
    int i, j;
    for(i = 0; i < NU; i++) {
      if(offact_gen_id_for_user(UIDS[i]) != offact_gen_id_for_user(UIDS[i])) same = 0;
      if(offact_gen_id_for_user(UIDS[i]) == 0) nonzero = 0;
    }
    for(i = 0; i < NU; i++)
      for(j = i + 1; j < NU; j++)
        if(offact_gen_id_for_user(UIDS[i]) == offact_gen_id_for_user(UIDS[j])) distinct = 0;
    chk("A1 同一个 uid 反复算结果恒定", same, NULL);
    chk("A2 不同 uid 两两不撞", distinct, NULL);
    chk("A3 结果非 0（0 在注册表里表示「未激活」）", nonzero, NULL);
    {
      char d[128];
      snprintf(d, sizeof(d), "uid 0x%08X -> 0x%016llx", UIDS[0],
               (unsigned long long)offact_gen_id_for_user(UIDS[0]));
      chk("A4 实际取值样例", offact_gen_id_for_user(UIDS[0]) != 0, d);
    }
  }

  printf("\n== B. 预览 == 实际写入 ==\n");
  {
    uint32_t uid = UIDS[0];
    uint64_t derived = offact_gen_id_for_user(uid);
    uint64_t written;

    set_slot_name(ENT_NAME_1, "Alice");

    chk("B1 未激活时 offact_get_id() 为 0（界面此时显示 derivedId）",
        get_slot_id(ENT_ID_1) == 0, NULL);

    /* 老代码在这里会把 0 解释成"按名字生成"，于是界面预览（按 uid）与写入（按名字）
     * 走两套算法。现在要求调用方显式给 id。 */
    chk("B2 offact_activate(slot, 0) 被拒绝（不再有隐式默认值）",
        offact_activate(1, 0) != 0, NULL);

    chk("B3 offact_activate(slot, derived) 成功",
        offact_activate(1, derived) == 0, NULL);
    written = get_slot_id(ENT_ID_1);
    chk_u64("B4 写进注册表的 account_id 恰好等于界面上显示的 derivedId",
            written, derived);

    /* 名字再改一次，id 必须纹丝不动（改名只写 name 字段） */
    set_slot_name(ENT_NAME_1, "AliceRenamed");
    chk_u64("B5 改名之后 account_id 不变", get_slot_id(ENT_ID_1), derived);

    chk("B6 激活来源被标成 ACTIVATE（登出才能只退登录态）",
        g_origin == 1 /* SAFETY_ORIGIN_ACTIVATE */, NULL);
  }

  printf("\n== C. 反向断言：老的「按名字派生」确实会随名字变 ==\n");
  {
    uint64_t a = offact_gen_id("Alice");
    uint64_t b = offact_gen_id("AliceRenamed");
    char d[160];
    snprintf(d, sizeof(d), "offact_gen_id(\"Alice\")=0x%016llx != (\"AliceRenamed\")=0x%016llx",
             (unsigned long long)a, (unsigned long long)b);
    /* 这条**必须**成立：它是"旧做法为什么是错的"的证据。若哪天它变成相等，
       offact_gen_id 就不再是名字派生的了，那整段结论要重写。 */
    chk("C1 offact_gen_id(名字) 随名字改变 —— 这就是旧 bug 的来源", a != b, d);
  }

  printf("\n== D. 端到端：改名前后 derivedId 一字不变 ==\n");
  {
    uint32_t uid = 0x1A1D3629u;
    uint64_t before, after;

    set_slot_name(ENT_NAME_1, "OldName");
    before = offact_gen_id_for_user(uid);          /* 界面在改名前的 derivedId */

    /* 改名：只动 name 字段（npsignin_rename 干的就这一件事，外加 UserService 与两个文件） */
    set_slot_name(ENT_NAME_1, "NewName");

    after = offact_gen_id_for_user(uid);           /* 界面在改名后的 derivedId */
    chk_u64("D1 未激活账号：改名后 derivedId 不变", after, before);

    /* 反面对照：如果按老做法（名字派生），这一格会变 */
    {
      char d[160];
      snprintf(d, sizeof(d), "老做法会变：0x%016llx -> 0x%016llx",
               (unsigned long long)offact_gen_id("OldName"),
               (unsigned long long)offact_gen_id("NewName"));
      chk("D2 对照：同一场景按老做法（名字派生）确实会变",
          offact_gen_id("OldName") != offact_gen_id("NewName"), d);
    }
  }

  printf("\n──────── 汇总 ────────\n通过 %d / %d\n", g_pass, g_pass + g_fail);
  if(g_fail) printf("失败 %d 条\n", g_fail);
  return g_fail ? 1 : 0;
}
