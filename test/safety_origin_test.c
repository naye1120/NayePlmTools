/* [CN-FORK] v0.5.2.107 — safety 层「按来源还原」的主机端单测
 *
 * 为什么值得单独测：这次改的是**回滚范围**，改错的后果不是崩，而是"以为还原了其实没有"
 * 或者"还原多了把用户的激活抹了"。这类错误在实机上很难看出来（要重启才生效），所以
 * 逻辑必须在这里用断言钉死。
 *
 * 编译（WSL 主机 clang，不需要 PS5 SDK）：
 *   clang -Wall -Wextra -O0 -DSAFETY_DIR='"/tmp/safety_origin_test"' \
 *         -Isrc/psaccounts src/psaccounts/safety.c test/safety_origin_test.c \
 *         -o /tmp/safety_origin_test && /tmp/safety_origin_test
 *
 * 覆盖：
 *   A. 老日志兼容 —— 5 字段（无 origin）与越界 origin 都降级成 NONE，
 *      并且**两种**还原都管得到它（绝不能因为格式换代把原值丢掉）
 *   B. 激活 → 登入 → 「仅登出」：登录态退掉，离线激活留着
 *   C. 再「完全还原」：一切归零，日志清空
 *   D. 改名不被「仅登出」退掉，但「完全还原」会退
 *   E. 同一个 entity 被两类动作先后写到时，记录归属**最早那次**（真实重叠：
 *      槽 1 的 FLAGS 就是 LOGIN_FLAG，激活和登入都会写它）
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "safety.h"

/* ------------------------------------------------------------------ *
 * sceRegMgr 打桩：一个内存小表                                          *
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

static int stub_read(uint32_t ent, void *out, int cap, int *len_out)
{
  stub_t *s = stub_find(ent);
  int n;
  if(!s) return -1;
  n = s->len < cap ? s->len : cap;
  memcpy(out, s->buf, (size_t)n);
  *len_out = n;
  return 0;
}

int sceRegMgrGetInt(uint32_t key, int32_t *value)
{
  int len = 0;
  if(stub_read(key, value, 4, &len) != 0) return -1;
  return 0;
}

int sceRegMgrGetStr(uint32_t key, char *value, size_t size)
{
  int len = 0;
  if(stub_read(key, value, (int)size, &len) != 0) return -1;
  return 0;
}

int sceRegMgrGetBin(uint32_t key, void *value, size_t size)
{
  int len = 0;
  if(stub_read(key, value, (int)size, &len) != 0) return -1;
  return 0;
}

static int stub_write(uint32_t ent, const void *p, int len)
{
  stub_seed(ent, p, len);
  return 0;
}

int sceRegMgrSetInt(uint32_t key, int32_t value)
{
  return stub_write(key, &value, 4);
}

int sceRegMgrSetStr(uint32_t key, const char *value, size_t size)
{
  return stub_write(key, value, (int)size);
}

int sceRegMgrSetBin(uint32_t key, const void *value, size_t size)
{
  return stub_write(key, value, (int)size);
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

static void chk_int(const char *what, long got, long want)
{
  char d[96];
  snprintf(d, sizeof(d), "got %ld, want %ld", got, want);
  chk(what, got == want, d);
}

static void chk_str(const char *what, const char *got, const char *want)
{
  char d[160];
  snprintf(d, sizeof(d), "got \"%s\", want \"%s\"", got, want);
  chk(what, strcmp(got, want) == 0, d);
}

/* ------------------------------------------------------------------ *
 * 被测实体（真实编号，与 npsignin.c / offact.c 一致）                    *
 * ------------------------------------------------------------------ */

#define E_ID        125830400u   /* bin 8  · 激活写（= KEY_ID(slot1) = REG_KEY_ACCOUNT_ID） */
#define E_TYPE      125874183u   /* str 17 · 激活写（= KEY_TYPE(slot1) = REG_KEY_NP_ENV）   */
#define E_FLAGS     125831168u   /* int 4  · **激活与登入都写**（= KEY_FLAGS = LOGIN_FLAG）  */
#define E_EMAIL     125830656u   /* str 65 · 登入写                                        */
#define E_SIGNIN    125874185u   /* int 4  · 登入写                                        */
#define E_NAME      125829632u   /* str 17 · 改名写                                        */

#define UID_A       100u
#define UID_B       200u
#define UID_LEGACY  777u

static const uint64_t ID_BEFORE = 0x0000000000000000ull;
static const uint64_t ID_AFTER  = 0xDEADBEEFCAFEBABEull;

static const char *TYPE_BEFORE = "unknown";
static const char *TYPE_AFTER  = "np";
static const char *EMAIL_BEFORE = "old@example.com";
static const char *EMAIL_AFTER  = "fake@np.example";
static const char *NAME_BEFORE  = "OldName";
static const char *NAME_AFTER   = "NewName";

static void seed_world(void)
{
  int32_t z = 0, two = 0x1002;
  char b[STUB_BYTES];

  memset(b, 0, sizeof(b));
  memcpy(b, &ID_BEFORE, 8);
  stub_seed(E_ID, b, 8);

  memset(b, 0, 17);
  memcpy(b, TYPE_BEFORE, strlen(TYPE_BEFORE));
  stub_seed(E_TYPE, b, 17);

  stub_seed(E_FLAGS, &z, 4);

  memset(b, 0, 65);
  memcpy(b, EMAIL_BEFORE, strlen(EMAIL_BEFORE));
  stub_seed(E_EMAIL, b, 65);

  stub_seed(E_SIGNIN, &z, 4);

  memset(b, 0, 17);
  memcpy(b, NAME_BEFORE, strlen(NAME_BEFORE));
  stub_seed(E_NAME, b, 17);

  (void)two;
}

static uint64_t read_id(void)
{
  uint64_t v = 0;
  int len = 0;
  stub_read(E_ID, &v, 8, &len);
  return v;
}

static int32_t read_int(uint32_t ent)
{
  int32_t v = 0;
  int len = 0;
  stub_read(ent, &v, 4, &len);
  return v;
}

static void read_str(uint32_t ent, char *out, int cap)
{
  int len = 0;
  memset(out, 0, (size_t)cap);
  stub_read(ent, out, cap, &len);
}

/* ------------------------------------------------------------------ *
 * A. 老日志兼容：手工造一份"本字段出现之前"的索引                       *
 * ------------------------------------------------------------------ */

static void write_blob(uint32_t ent, const void *p, int len)
{
  char path[128];
  FILE *f;
  snprintf(path, sizeof(path), "%s/%u.bin", SAFETY_REG_DIR, ent);
  f = fopen(path, "wb");
  fwrite(p, 1, (size_t)len, f);
  fclose(f);
}

static void handcraft_legacy_log(void)
{
  FILE *f;
  char b[STUB_BYTES];

  mkdir(SAFETY_DIR, 0755);
  mkdir(SAFETY_REG_DIR, 0755);
  mkdir(SAFETY_FILE_DIR, 0755);

  /* 第 1 条：5 字段（老格式，没有 origin） */
  memset(b, 0, sizeof(b));
  stub_read(E_ID, b, 8, &(int){0});
  write_blob(E_ID, b, 8);

  /* 第 2 条：6 字段但 origin 越界（99）—— 必须降级成 NONE 而不是被丢掉 */
  memset(b, 0, sizeof(b));
  {
    int len = 0;
    stub_read(E_EMAIL, b, 65, &len);
    write_blob(E_EMAIL, b, 65);
  }

  f = fopen(SAFETY_REG_IDX, "w");
  fprintf(f, "%u %d %d %d %u\n",       E_ID,    SAFETY_BIN, 8,  8,  UID_LEGACY);
  fprintf(f, "%u %d %d %d %u %d\n",    E_EMAIL, SAFETY_STR, 65, 65, UID_LEGACY, 99);
  fclose(f);
}

/* ------------------------------------------------------------------ */

int main(void)
{
  char s[STUB_BYTES];
  int32_t z = 0;
  int32_t one = 1, flags_on = 0x1002;
  safety_key_t act[3], sig[3], ren[1];
  int before_id_le;

  printf("== safety 层「按来源还原」单测 ==\n");

  /* 干净重来 */
  system("rm -rf " SAFETY_DIR);

  seed_world();

  /* ── A. 老日志兼容 ─────────────────────────────────────────── */
  printf("\nA. 老日志兼容（5 字段 / 越界 origin）\n");
  handcraft_legacy_log();
  chk("safety_init 可写", safety_init() == 0, NULL);
  chk_int("读到 2 条", safety_reg_count(), 2);
  chk_int("两条都降级成 NONE（M_NONE 命中 2）",
          safety_reg_count_scope_mask(UID_LEGACY, SAFETY_ORIGIN_M_NONE), 2);
  chk_int("「仅登出」的掩码也覆盖它们（M_SIGNOUT_ONLY 命中 2）",
          safety_reg_count_scope_mask(UID_LEGACY, SAFETY_ORIGIN_M_SIGNOUT_ONLY), 2);
  chk_int("M_ACTIVATE 不命中它们",
          safety_reg_count_scope_mask(UID_LEGACY, SAFETY_ORIGIN_M_ACTIVATE), 0);

  /* 破坏掉真实值，好验证还原确实发生了 */
  before_id_le = read_id();
  {
    memset(s, 0, sizeof(s));
    stub_seed(E_ID, s, 8);                       /* 归零 */
    memset(s, 0, 65);
    memcpy(s, "clobbered", 9);
    stub_seed(E_EMAIL, s, 65);
  }
  chk("完全还原把老记录也退回去", safety_restore_reg() == 2, NULL);
  chk_int("ID 回到老值", (long)read_id(), (long)before_id_le);
  read_str(E_EMAIL, s, 65);
  chk_str("EMAIL 回到老值", s, EMAIL_BEFORE);
  chk_int("日志已清空", safety_reg_count(), 0);

  /* ── B. 激活 → 登入 → 「仅登出」 ─────────────────────────────── */
  printf("\nB. 激活 → 登入 → 「仅登出」\n");
  seed_world();

  safety_set_scope(UID_A);
  safety_set_origin(SAFETY_ORIGIN_ACTIVATE);
  act[0].ent = E_ID;    act[0].type = SAFETY_BIN; act[0].size = 8;
  act[1].ent = E_TYPE;  act[1].type = SAFETY_STR; act[1].size = 17;
  act[2].ent = E_FLAGS; act[2].type = SAFETY_INT; act[2].size = 4;
  chk_int("激活备份 3 个键", safety_backup_reg(act, 3, 0), 3);
  chk("激活写入全部成功",
      safety_set_bin(E_ID, &ID_AFTER, 8) == 0 &&
      safety_set_str(E_TYPE, TYPE_AFTER, 17) == 0 &&
      safety_set_int(E_FLAGS, flags_on) == 0, NULL);

  safety_set_origin(SAFETY_ORIGIN_SIGNIN);
  sig[0].ent = E_EMAIL;  sig[0].type = SAFETY_STR; sig[0].size = 65;
  sig[1].ent = E_SIGNIN; sig[1].type = SAFETY_INT; sig[1].size = 4;
  sig[2].ent = E_FLAGS;  sig[2].type = SAFETY_INT; sig[2].size = 4;   /* 与激活重叠 */
  chk_int("登入备份时重叠的 FLAGS 被去重（只记 2 条）",
          safety_backup_reg(sig, 3, 0), 2);
  chk("登入写入全部成功",
      safety_set_str(E_EMAIL, EMAIL_AFTER, 65) == 0 &&
      safety_set_int(E_SIGNIN, one) == 0, NULL);

  chk_int("M_ACTIVATE 名下 3 条", safety_reg_count_scope_mask(UID_A, SAFETY_ORIGIN_M_ACTIVATE), 3);
  chk_int("M_SIGNIN 名下 2 条",   safety_reg_count_scope_mask(UID_A, SAFETY_ORIGIN_M_SIGNIN), 2);
  chk_int("合计 5 条",            safety_reg_count_scope_mask(UID_A, SAFETY_ORIGIN_M_ALL), 5);
  chk_int("重叠的 FLAGS 归属最早那次写入（ACTIVATE）",
          safety_reg_count_scope_mask(UID_A, SAFETY_ORIGIN_M_ACTIVATE), 3);

  chk_int("「仅登出」退掉 2 条登录态记录",
          safety_restore_scope_mask(UID_A, SAFETY_ORIGIN_M_SIGNOUT_ONLY), 2);
  read_str(E_EMAIL, s, 65);
  chk_str("EMAIL 已还原", s, EMAIL_BEFORE);
  chk_int("SIGNIN_FLAG 已还原", read_int(E_SIGNIN), 0);

  printf("  ── 关键：激活必须原封不动 ──\n");
  chk("ID 仍是激活写的值（**没被登出带走**）", read_id() == ID_AFTER, NULL);
  read_str(E_TYPE, s, 17);
  chk_str("TYPE 仍是 \"np\"", s, TYPE_AFTER);
  chk_int("FLAGS 仍是 0x1002", read_int(E_FLAGS), flags_on);
  chk_int("日志里剩 3 条（激活那 3 条）", safety_reg_count(), 3);
  chk_int("剩下的是 M_ACTIVATE", safety_reg_count_scope_mask(UID_A, SAFETY_ORIGIN_M_ACTIVATE), 3);

  /* ── C. 「完全还原」 ───────────────────────────────────────── */
  printf("\nC. 「完全还原」把激活也退掉\n");
  chk_int("退掉 3 条", safety_restore_scope_mask(UID_A, SAFETY_ORIGIN_M_ALL), 3);
  chk("ID 回到 0", read_id() == ID_BEFORE, NULL);
  read_str(E_TYPE, s, 17);
  chk_str("TYPE 回到初始值", s, TYPE_BEFORE);
  chk_int("FLAGS 回到 0", read_int(E_FLAGS), 0);
  chk_int("日志清空", safety_reg_count(), 0);

  /* ── D. 改名不被「仅登出」退掉 ─────────────────────────────── */
  printf("\nD. 改名不被「仅登出」退掉\n");
  safety_set_scope(UID_B);
  safety_set_origin(SAFETY_ORIGIN_RENAME);
  ren[0].ent = E_NAME; ren[0].type = SAFETY_STR; ren[0].size = 17;
  safety_backup_reg(ren, 1, 0);
  safety_set_str(E_NAME, NAME_AFTER, 17);

  chk_int("「仅登出」退掉 0 条", safety_restore_scope_mask(UID_B, SAFETY_ORIGIN_M_SIGNOUT_ONLY), 0);
  read_str(E_NAME, s, 17);
  chk_str("名字保持改后的值", s, NAME_AFTER);
  chk_int("改名记录仍挂在日志里", safety_reg_count(), 1);

  chk_int("「完全还原」退掉 1 条", safety_restore_scope_mask(UID_B, SAFETY_ORIGIN_M_ALL), 1);
  read_str(E_NAME, s, 17);
  chk_str("名字回到改前", s, NAME_BEFORE);
  chk_int("日志清空", safety_reg_count(), 0);

  /* ── E. 归还位（uid 隔离） ─────────────────────────────────── */
  printf("\nE. uid 隔离\n");
  safety_set_scope(UID_A);
  safety_set_origin(SAFETY_ORIGIN_SIGNIN);
  safety_backup_reg(sig, 2, 0);
  chk_int("A 名下 2 条", safety_reg_count_scope_mask(UID_A, SAFETY_ORIGIN_M_ALL), 2);
  chk_int("「仅登出」B 退不掉 A 的记录",
          safety_restore_scope_mask(UID_B, SAFETY_ORIGIN_M_SIGNOUT_ONLY), 0);
  chk_int("A 的记录还在", safety_reg_count_scope_mask(UID_A, SAFETY_ORIGIN_M_ALL), 2);

  (void)z;
  printf("\n──────── 汇总 ────────\n通过 %d / %d\n", g_pass, g_pass + g_fail);
  return g_fail ? 1 : 0;
}
