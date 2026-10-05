/* PS5 Account Tools — in-process NP fake sign-in / sign-out (PS5).
 *
 * Logic mirrors earthonion/np-fake-signin's PS5 (-DPS5) path verbatim:
 * config.dat offsets, the language->region table, build_config() and
 * write_registry_state(). The UserService init/terminate and all PS4-only
 * paths (account.dat/token.dat, HMAC signing, ShellCore memory patch) are
 * dropped — UserService is already up (sys_lite) and none of the PS4 bits
 * apply on PS5. Operations target an explicit user id + name.
 */

#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "npsignin.h"
#include "safety.h"

/* Template byte arrays (each defined in its own single-TU header). */
#include "np_templates/auth_dat.h"
#include "np_templates/config_dat.h"


/* ------------------------------------------------------------------ *
 * SDK imports                                                          *
 * ------------------------------------------------------------------ */

int32_t sceSystemServiceParamGetInt(int32_t paramId, int32_t *value);

int32_t sceRegMgrSetInt(uint32_t key, int32_t value);
int32_t sceRegMgrSetStr(uint32_t key, const char *value, size_t size);
int32_t sceRegMgrGetInt(uint32_t key, int32_t *value);
int32_t sceRegMgrGetStr(uint32_t key, char *value, size_t size);
int32_t sceRegMgrGetBin(uint32_t key, void *value, size_t size);

/* 改名要动两层：注册表（持久）+ UserService（壳层「切换用户」列表实际显示的那一份）。
 * SDK 头里没有这两个原型，但 libSceUserService 的两个导出符号都在（与 Get 同址的弱桩），
 * 项目里的 extern 声明与 accounts.c/sys_lite.c 的 Get 保持同一签名。 */
int sceUserServiceGetUserName(int32_t user_id, char *name, size_t max_size);
int sceUserServiceSetUserName(int32_t user_id, const char *name, size_t max_size);


/* ------------------------------------------------------------------ *
 * 干跑模式（A3 知情预览）                                              *
 *                                                                     *
 * 打开后 wset_* 只把 entity 记进内存清单，既不备份也不写注册表。这样   *
 * 预览用的是和真实登录完全同一条代码路径，键清单不会和实现脱节。       *
 * ------------------------------------------------------------------ */

static int      g_plan = 0;
static uint32_t g_plan_ents[64];
static int      g_plan_n = 0;

static void
plan_record(uint32_t ent) {
  if(g_plan_n < (int)(sizeof(g_plan_ents) / sizeof(g_plan_ents[0])))
    g_plan_ents[g_plan_n++] = ent;
}


/* ------------------------------------------------------------------ *
 * 备份优先的写入入口                                                    *
 *                                                                     *
 * 每个键单独「先记原值、再写」，而不是批量备份：原版一次登录会写 22 个    *
 * 键、且 base slot 和目标槽各写一遍，批量备份很容易漏掉某个 entity，漏掉  *
 * 的那个就没有原值可还原。                                              *
 * 备份失败一律拒绝写入并返回 -1 —— 宁可不干活，不能干活干到一半没法回头。  *
 * ------------------------------------------------------------------ */

static int
wset_int(uint32_t ent, int32_t v) {
  if(g_plan) {
    int32_t probe = 0;
    /* 读不到的键在真实写路径里会在备份那步失败、从而整个不写，预览必须给同样
       的答案，否则报出来的键数会比事后账本里的多。 */
    if(sceRegMgrGetInt(ent, &probe) != 0) return -1;
    plan_record(ent);
    return 0;
  }
  safety_key_t k = { ent, SAFETY_INT, 4 };
  if(safety_backup_reg(&k, 1, 0) < 0) return -1;
  return safety_set_int(ent, v);
}

static int
wset_str(uint32_t ent, const char *s, int size) {
  if(g_plan) {
    char probe[128];
    int n = size < (int)sizeof(probe) ? size : (int)sizeof(probe);
    if(sceRegMgrGetStr(ent, probe, (size_t)n) != 0) return -1;
    plan_record(ent);
    return 0;
  }
  safety_key_t k = { ent, SAFETY_STR, size };
  if(safety_backup_reg(&k, 1, 0) < 0) return -1;
  return safety_set_str(ent, s, size);
}


/* Registry keys (base, slot 1). Per-slot = key + (slot-1)*65536. */
#define REG_KEY_USERNAME      125829632   /* offset 0x04, str 17 */
#define REG_KEY_ACCOUNT_ID    125830400   /* offset 0x100, bin 8 */
#define REG_KEY_EMAIL         125830656   /* offset 0x108, str 65 */
#define REG_KEY_NP_ENV        125874183   /* offset 0x177, str 17 */
#define REG_KEY_ONLINE_ID     125874188   /* offset 0x1AD, str 17 */
#define REG_KEY_COUNTRY       125874190   /* offset 0x1BE, str 3 */
#define REG_KEY_LANGUAGE      125874191   /* offset 0x1C1, str 6 */
#define REG_KEY_LOCALE        125874192   /* offset 0x1C7, str 36 */
#define REG_KEY_FLAG_1        125830144   /* offset 0x48, int */
#define REG_KEY_FLAG_2        125831168   /* offset 0x50, int */
#define REG_KEY_FLAG_3        125831424   /* offset 0x4C, int */
#define REG_KEY_FLAG_5C       125832960   /* offset 0x5C, int */
#define REG_KEY_FIELD_1F4     125874194   /* offset 0x1F4, int */
#define REG_KEY_SIGNIN_FLAG   125874185   /* offset 0x1F8, int - CRITICAL */
#define REG_KEY_FIELD_1FC     125874186   /* offset 0x1FC, int */
#define REG_KEY_NP_0xA4       125830912   /* offset 0xA4, int */
#define REG_KEY_NP_0xB4       125831936   /* offset 0xB4, int */
#define REG_KEY_NP_0xD0       125832704   /* offset 0xD0, int */
#define REG_KEY_NP_0xD4       125882625   /* offset 0xD4, int */
#define REG_KEY_NP_0xDC       125854723   /* offset 0xDC, int */
#define REG_KEY_NP_0xF4       125833216   /* offset 0xF4, int */
#define REG_KEY_EXT_0x1100    125874189   /* offset 0x1100, str 65 */
#define REG_KEY_EXT_0x1141    125874193   /* offset 0x1141, str 11 */
#define REG_KEY_EXT_0x114C    125874195   /* offset 0x114C, str 65 */

/* 同一个 entity 的两个叫法：offact 模板里它是 flags（cfg 偏移 0x50），真机探针里
 * 它是 login_flag（槽基址 +0x800）。实测四态 0=空槽 / 65536=新建未激活 /
 * 4098=已激活未登录 / 6=已激活已登录，全部出自这一格。 */
#define REG_KEY_LOGIN_FLAG        REG_KEY_FLAG_2
#define REG_LOGIN_FLAG_OFFLINE    0x1002   /* 已激活、未登录（真机槽 4 实测稳定态） */

/* ------------------------------------------------------------------ *
 * Language -> region table (indices match sceSystemServiceParamGetInt  *
 * LANG values; anything out of range falls back to en-US).             *
 * ------------------------------------------------------------------ */

static const struct { const char *country, *lang, *locale; } REGIONS[] = {
  {"jp","ja","ja-JP"}, {"us","en","en-US"}, {"fr","fr","fr-FR"},
  {"es","es","es-ES"}, {"de","de","de-DE"}, {"it","it","it-IT"},
  {"nl","nl","nl-NL"}, {"pt","pt","pt-PT"}, {"ru","ru","ru-RU"},
  {"kr","ko","ko-KR"}, {"tw","zh","zh-TW"}, {"cn","zh","zh-CN"},
  {"fi","fi","fi-FI"}, {"se","sv","sv-SE"}, {"dk","da","da-DK"},
  {"no","no","no-NO"}, {"pl","pl","pl-PL"}, {"br","pt","pt-BR"},
  {"gb","en","en-GB"}, {"tr","tr","tr-TR"}, {"mx","es","es-MX"},
  {"sa","ar","ar-SA"}, {"ca","fr","fr-CA"}, {"cz","cs","cs-CZ"},
  {"hu","hu","hu-HU"}, {"gr","el","el-GR"}, {"ro","ro","ro-RO"},
  {"th","th","th-TH"}, {"vn","vi","vi-VN"}, {"id","id","id-ID"},
};
#define REGION_COUNT ((int)(sizeof(REGIONS)/sizeof(REGIONS[0])))


/* ------------------------------------------------------------------ *
 * Helpers (verbatim from upstream PS5 path)                            *
 * ------------------------------------------------------------------ */

static void
patch_str(unsigned char *buf, int offset, const char *str, int max_len) {
  memset(&buf[offset], 0, max_len);
  int len = (int)strlen(str);
  if(len > max_len - 1) len = max_len - 1;
  memcpy(&buf[offset], str, len);
}

static void
build_config(unsigned char *cfg, const char *username, const uint8_t *account_id) {
  /* cfg is pre-loaded with template + region overrides before this call. */
  patch_str(cfg, 0x04, username, 17);          /* username        */
  memcpy(&cfg[0x100], account_id, 8);           /* account_id      */
  patch_str(cfg, 0x1AD, username, 17);          /* online_id       */

  char country[3] = {0};
  memcpy(country, &cfg[0x1BE], 2);
  if(country[0] == 0) { country[0] = 'u'; country[1] = 's'; }

  char np_email[65] = {0};
  snprintf(np_email, sizeof(np_email),
           "%s@a8.%s.np.playstation.net", username, country);
  patch_str(cfg, 0x108,  np_email, 65);         /* email           */
  patch_str(cfg, 0x1100, np_email, 65);         /* np_email        */
}

/* 组装这一次登录要用的 config.dat 工作副本：模板 + 区域（按系统语言）+ 身份。
 * 真实登录和只读预览共用它，避免两条路径各写一份、清单和实现对不上。 */
static void
make_cfg(const npsignin_result_t *st, unsigned char *cfg) {
  int32_t sys_lang = -1;
  sceSystemServiceParamGetInt(1 /* LANG */, &sys_lang);
  int ridx = (sys_lang >= 0 && sys_lang < REGION_COUNT) ? sys_lang : 1;

  memcpy(cfg, config_dat, sizeof(config_dat));
  patch_str(cfg, 0x1BE, REGIONS[ridx].country, 3);
  patch_str(cfg, 0x1C1, REGIONS[ridx].lang, 6);
  patch_str(cfg, 0x1C7, REGIONS[ridx].locale, 36);

  uint8_t acct_id[8];
  memcpy(acct_id, &st->account_id, 8);
  build_config(cfg, st->user_name, acct_id);
}

static int
write_file(const char *path, const unsigned char *data, size_t len) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if(fd < 0) return -1;
  ssize_t written = write(fd, data, len);
  close(fd);
  return (written == (ssize_t)len) ? 0 : -1;
}

static int
write_np_files(const unsigned char *cfg, uint32_t userId) {
  char path[256];

  snprintf(path, sizeof(path), "/system_data/priv/home/%x/np", userId);
  mkdir(path, 0755);

  snprintf(path, sizeof(path), "/system_data/priv/home/%x/np/auth.dat", userId);
  /* 关键：write_file() 是 O_TRUNC，真账号的 auth.dat 一旦走到这里就没了。
     必须先登记/复制原件；登记失败（/data 不可写）就不允许继续。 */
  if(safety_backup_file(path) != 0) return -1;
  if(write_file(path, auth_dat, sizeof(auth_dat)) != 0) return -1;

  snprintf(path, sizeof(path), "/system_data/priv/home/%x/config.dat", userId);
  if(safety_backup_file(path) != 0) return -1;
  if(write_file(path, cfg, sizeof(config_dat)) != 0) return -1;

  return 0;
}

static void
write_registry_state(const unsigned char *cfg, uint32_t off) {
  int32_t val;

  if(cfg[0x108] != 0)
    wset_str(REG_KEY_EMAIL + off, (const char*)&cfg[0x108], 65);
  wset_str(REG_KEY_ONLINE_ID + off, (const char*)&cfg[0x1AD], 17);
  wset_str(REG_KEY_NP_ENV + off,    (const char*)&cfg[0x177], 17);
  wset_str(REG_KEY_COUNTRY + off,   (const char*)&cfg[0x1BE], 3);
  wset_str(REG_KEY_LANGUAGE + off,  (const char*)&cfg[0x1C1], 6);
  wset_str(REG_KEY_LOCALE + off,    (const char*)&cfg[0x1C7], 36);

  memcpy(&val, &cfg[0x48],  4); wset_int(REG_KEY_FLAG_1 + off, val);
  memcpy(&val, &cfg[0x4C],  4); wset_int(REG_KEY_FLAG_3 + off, val);
  memcpy(&val, &cfg[0x50],  4); wset_int(REG_KEY_FLAG_2 + off, val);
  memcpy(&val, &cfg[0x5C],  4); wset_int(REG_KEY_FLAG_5C + off, val);
  memcpy(&val, &cfg[0x1F4], 4); wset_int(REG_KEY_FIELD_1F4 + off, val);
  memcpy(&val, &cfg[0x1F8], 4); wset_int(REG_KEY_SIGNIN_FLAG + off, val);
  memcpy(&val, &cfg[0x1FC], 4); wset_int(REG_KEY_FIELD_1FC + off, val);
  memcpy(&val, &cfg[0xA4],  4); wset_int(REG_KEY_NP_0xA4 + off, val);
  memcpy(&val, &cfg[0xB4],  4); wset_int(REG_KEY_NP_0xB4 + off, val);
  memcpy(&val, &cfg[0xD0],  4); wset_int(REG_KEY_NP_0xD0 + off, val);
  memcpy(&val, &cfg[0xD4],  4); wset_int(REG_KEY_NP_0xD4 + off, val);
  memcpy(&val, &cfg[0xDC],  4); wset_int(REG_KEY_NP_0xDC + off, val);
  memcpy(&val, &cfg[0xF4],  4); wset_int(REG_KEY_NP_0xF4 + off, val);

  if(cfg[0x1100] != 0)
    wset_str(REG_KEY_EXT_0x1100 + off, (const char*)&cfg[0x1100], 65);
  if(cfg[0x1141] != 0)
    wset_str(REG_KEY_EXT_0x1141 + off, (const char*)&cfg[0x1141], 11);
  if(cfg[0x114C] != 0)
    wset_str(REG_KEY_EXT_0x114C + off, (const char*)&cfg[0x114C], 65);
}


/* ------------------------------------------------------------------ *
 * Resolve a user's activation slot + status.                          *
 * ------------------------------------------------------------------ */

static int
find_slot(const char *name) {
  if(!name || !name[0]) return 0;
  for(int n = 1; n <= 16; n++) {
    uint32_t off = (uint32_t)(n - 1) * 65536u;
    char slot_name[32] = {0};
    sceRegMgrGetStr(REG_KEY_USERNAME + off, slot_name, sizeof(slot_name));
    if(slot_name[0] && !strcmp(slot_name, name)) return n;
  }
  return 0;
}

static void
fill_status(npsignin_result_t *out) {
  out->slot = find_slot(out->user_name);
  out->account_id = 0;
  out->activated = 0;
  out->signed_in = 0;
  if(out->slot <= 0) return;

  uint32_t off = (uint32_t)(out->slot - 1) * 65536u;
  uint64_t id = 0;
  sceRegMgrGetBin(REG_KEY_ACCOUNT_ID + off, &id, sizeof(id));
  out->account_id = id;

  /* "Activated" means the slot has a non-zero account id. We deliberately do
     NOT key this off the flag at 0x50: offline activation sets it to 0x1002,
     but a successful NP sign-in legitimately overwrites the same key with the
     template's signed-in value (0x6), which would otherwise make an active,
     signed-in account read as "not activated". The account id is the stable
     indicator. */
  out->activated = (id != 0);

  int32_t sf = 0;
  sceRegMgrGetInt(REG_KEY_SIGNIN_FLAG + off, &sf);
  out->signed_in = (sf != 0);
}

static void
init_result(npsignin_result_t *out, uint32_t uid, const char *name) {
  memset(out, 0, sizeof(*out));
  out->user_id = uid;
  snprintf(out->user_name, sizeof(out->user_name), "%s", name ? name : "");
}


/* ------------------------------------------------------------------ *
 * Public API                                                           *
 * ------------------------------------------------------------------ */

int
npsignin_status(uint32_t uid, const char *name, npsignin_result_t *out) {
  init_result(out, uid, name);
  fill_status(out);
  out->ok = 1;
  if(out->slot == 0)
    snprintf(out->message, sizeof(out->message), "no registry slot");
  else if(!out->activated)
    snprintf(out->message, sizeof(out->message), "not activated");
  else
    snprintf(out->message, sizeof(out->message),
             out->signed_in ? "signed in" : "signed out");
  return 0;
}

int
npsignin_signin(uint32_t uid, const char *name, npsignin_result_t *out) {
  init_result(out, uid, name);
  fill_status(out);

  if(out->slot == 0 || out->account_id == 0) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message),
             "not activated — activate this account first");
    return -1;
  }

  /* 备份层不可用就一票否决：没有还原通道的写入等于不可撤销。 */
  if(safety_init() != 0) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message),
             "ABORTED: cannot write backup log under %s — refusing an "
             "irreversible change", SAFETY_DIR);
    return -1;
  }

  /* Local working copy of the template — no shared mutable state. */
  unsigned char cfg[sizeof(config_dat)];
  make_cfg(out, cfg);

  if(write_np_files(cfg, out->user_id) != 0) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message),
             "ABORTED while writing NP files — run Restore before retrying");
    return -1;
  }

  /* 只写目标槽。上游 np-fake-signin 在这里还无条件再写一遍 base 槽 1，于是一次
     对槽 3 的登录会连着改掉主账号那一格的身份（串槽），登出时也没法只退自己。
     现在每个账号只动自己那一格，归属才谈得上准确。 */
  write_registry_state(cfg, (uint32_t)(out->slot - 1) * 65536u);

  out->ok = 1;
  out->signed_in = 1;
  snprintf(out->message, sizeof(out->message),
           "signed in as %s (slot %d) — reboot to apply. %d key(s) backed up "
           "under this uid; Sign out rolls exactly those back.",
           out->user_name, out->slot, safety_reg_count_scope(out->user_id));
  return 0;
}

/* 基线登出用：把 NP 会话文件交给备份层，再从原位拿掉。
 * 返回 1 = 搬走了，0 = 本来就没有（不用处理），-1 = 抄不到原值。
 * 最后这种必须让调用方整笔否决——宁可留着登录文件，也不能做一次抄不回来的删除。 */
static int
np_file_to_baseline(const char *path) {
  struct stat st;

  if(stat(path, &st) != 0) return 0;
  if(safety_backup_file(path) != 0) return -1;
  if(unlink(path) != 0) return -1;
  return 1;
}


int
npsignin_signout(uint32_t uid, const char *name, npsignin_result_t *out) {
  int restored, left;

  init_result(out, uid, name);
  fill_status(out);

  /* 原版这里只把 3 个 int 归零，但 signin 写的是 22 个键 —— 剩下 19 个模板值
     永远留在注册表里。改成按备份日志逐键写回原值，并且只写回**这个 uid 名下**
     的记录：别的账号还在登录状态时，退掉一个不能把另一个的原值也抹了。
     auth.dat/config.dat 按 uid 定位，本来就是这一格的，照旧回灌。 */

  restored = safety_restore_scope(uid);
  left = safety_reg_count();
  if(restored < 0) {
    out->ok = 0;
    out->signed_in = (out->slot > 0);
    snprintf(out->message, sizeof(out->message),
             "RESTORE INCOMPLETE: %d record(s) failed readback — fix them "
             "manually from %s (%d record(s) still pending)",
             -restored, SAFETY_DIR, left);
    return -1;
  }

  {
    char path[256];
    int fbad = 0;

    /* 只有备份日志里登记过的路径才是我们动过的；没登记 = 这一版没碰过它，
     * 不能算失败，更不能删。 */
    snprintf(path, sizeof(path),
             "/system_data/priv/home/%x/np/auth.dat", out->user_id);
    if(safety_file_was_absent(path) >= 0 && safety_restore_file(path) != 0)
      fbad++;
    snprintf(path, sizeof(path),
             "/system_data/priv/home/%x/config.dat", out->user_id);
    if(safety_file_was_absent(path) >= 0 && safety_restore_file(path) != 0)
      fbad++;

    if(fbad) {
      out->ok = 0;
      snprintf(out->message, sizeof(out->message),
               "registry restored, but %d NP file(s) could not be restored "
               "(no backup record) — verify /system_data/priv/home/%x manually",
               fbad, out->user_id);
      return -1;
    }
  }

  if(restored == 0) {
    /* 名下没有记录 = 这个登录状态不是本工具写进去的（别的工具激活并登录的账号）。
     * 以前到这里就拒，等于「不是我们登的就退不掉」。现在改成按真机实测基线写回
     * 「已激活、未登录」：signin_flag 归零、login_flag 写 0x1002（槽 4 实测的稳定
     * 态），NP 会话文件先抄进备份层再删。三处都逐键记原值，所以 Restore 能把他
     * 那份登录状态原样放回去——底线仍然是可逆，不是「不许多写」。 */
    uint32_t off;
    char path[256];
    int n, bad = 0, dropped = 0;

    if(out->slot == 0) {
      out->ok = 0;
      snprintf(out->message, sizeof(out->message),
               "%s has no registry slot — nothing to sign out", out->user_name);
      return -1;
    }
    if(!out->signed_in) {
      out->ok = 1;
      snprintf(out->message, sizeof(out->message),
               "%s was already signed out — nothing to roll back",
               out->user_name);
      return 0;
    }

    if(safety_init() != 0) {
      out->ok = 0;
      snprintf(out->message, sizeof(out->message),
               "ABORTED: cannot write backup log under %s — refusing an "
               "irreversible change", SAFETY_DIR);
      return -1;
    }

    off = (uint32_t)(out->slot - 1) * 65536u;
    if(wset_int(REG_KEY_SIGNIN_FLAG + off, 0) != 0) bad++;
    if(wset_int(REG_KEY_LOGIN_FLAG + off, REG_LOGIN_FLAG_OFFLINE) != 0) bad++;

    snprintf(path, sizeof(path),
             "/system_data/priv/home/%x/np/auth.dat", out->user_id);
    n = np_file_to_baseline(path);
    if(n < 0) bad++; else dropped += n;
    snprintf(path, sizeof(path),
             "/system_data/priv/home/%x/config.dat", out->user_id);
    n = np_file_to_baseline(path);
    if(n < 0) bad++; else dropped += n;

    if(bad) {
      out->ok = 0;
      out->signed_in = 1;
      snprintf(out->message, sizeof(out->message),
               "BASELINE SIGN-OUT INCOMPLETE: %d write(s) refused (backup or "
               "Set failed) — %d record(s) are still pending under uid %u, run "
               "Restore before retrying", bad, safety_reg_count_scope(uid), uid);
      return -1;
    }

    out->ok = 1;
    out->signed_in = 0;
    snprintf(out->message, sizeof(out->message),
             "signed out %s (slot %d) — this sign-in was not written by this "
             "tool, so it was reset to the measured baseline: signin_flag=0, "
             "login_flag=0x1002, %d NP file(s) moved to backup. Restore puts "
             "them back; reboot to apply",
             out->user_name, out->slot, dropped);
    return 0;
  }

  out->ok = 1;
  out->signed_in = 0;
  snprintf(out->message, sizeof(out->message),
           "signed out %s — restored %d of its own value(s), %d other pending "
           "record(s) untouched; reboot to apply",
           out->user_name, restored, left);
  return 0;
}


/* ------------------------------------------------------------------ *
 * 改名                                                                 *
 * ------------------------------------------------------------------ */

/* SysReg 的 user_name 声明是 STR(17)，即 16 个可见字节 + NUL。上游 offact 传的是
 * OFFACT_NAME_MAX(32)，会连着覆盖后面的字段；这里不沿用，备份和写入都用 17，
 * 长度对称才谈得上还原。 */
#define RENAME_NAME_BYTES 17

static int
name_is_sane(const char *s) {
  size_t n;
  if(!s || !s[0]) return 0;
  n = strlen(s);
  if(n > RENAME_NAME_BYTES - 1) return 0;
  for(size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    /* 允许 UTF-8 多字节（壳层名字本身可以是中文），只挡控制字符。 */
    if(c < 0x20 || c == 0x7f) return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ *
 *  改名的第三、四层：壳层显示名的另外两个候选来源                      *
 *
 *  .100 补的第二层（UserService）实机回读是 "both verified"，可主界面用户按钮
 *  和「切换用户」列表仍是旧名 —— 那两处 UI 并不拿这份缓存当显示源。剩下的候选：
 *    ③ /system_data/priv/cache/profile/0x<UID大写>/online.json 的 firstName
 *       （换头像写回的就是这个目录、而且能生效，说明壳层确实读它）；
 *    ④ /user/home/<uid目录>/username.dat（activity 类工具给用户打标签读的是它）。
 *  两层的把握都不是 100%，所以各自把「读到什么、写没写、回读成不成」如实拼进
 *  message，让用户实测一轮就能判死真正的显示源。任何一层的格式对不上都只是
 *  原样不动 + 报一句话，不做没有把握的覆盖。
 *
 *  最后一个候选 /system_data/priv/home/user_list.dat（实测 72 字节二进制、A/B 双份）
 *  只当**只读探针**用：结构没有文档，写它等于拿全体账号名单去赌，所以只报
 *  「旧名/新名在不在里面、住在第几字节」，一个字节都不动。
 * ------------------------------------------------------------------ */

#define SHELL_NAME_MAX    64    /* 候选文件里名字的读出上限 */
#define PROFILE_JSON_MAX  65536 /* online.json 的大小上限，超过就不当它是 JSON */
#define USERNAME_DAT_MAX   4096

static char *
read_small_file(const char *path, long *len_out, long max_size) {
  struct stat sb;
  FILE *f;
  char *buf;
  size_t n;

  *len_out = 0;
  if(stat(path, &sb) != 0 || sb.st_size <= 0 || sb.st_size > max_size) return NULL;
  buf = malloc((size_t)sb.st_size + 1);
  if(!buf) return NULL;
  f = fopen(path, "rb");
  if(!f) { free(buf); return NULL; }
  n = fread(buf, 1, (size_t)sb.st_size, f);
  fclose(f);
  buf[n] = 0;
  *len_out = (long)n;
  return buf;
}

/* 在 online.json 的原始字节里定位 "firstName" 的值。三个硬性条件，任一不满足
 * 返回 0（宁可不写）：全文只出现一次这个键、值是非空字符串、值内不含反斜杠。
 * 成功时 vs 与 ve 之间框住值本身（不含两侧引号）。 */
static int
json_first_name_span(const char *raw, const char **vs, const char **ve) {
  const char *p, *q;

  *vs = *ve = NULL;
  p = strstr(raw, "\"firstName\"");
  if(!p) return 0;
  if(strstr(p + 11, "\"firstName\"")) return 0;
  q = p + 11;
  while(*q == ' ' || *q == '\t') q++;
  if(*q != ':') return 0;
  q++;
  while(*q == ' ' || *q == '\t') q++;
  if(*q != '"') return 0;
  q++;
  *vs = q;
  while(*q && *q != '"') {
    if(*q == '\\') return 0;
    q++;
  }
  if(*q != '"' || q == *vs) return 0;
  *ve = q;
  return 1;
}

static void
rename_profile_cache(uint32_t uid, const char *new_name, char *note, size_t n) {
  char path[160];
  char old[SHELL_NAME_MAX];
  char *raw, *body;
  const char *vs, *ve;
  long len, vlen, nlen, blen, cplen;

  snprintf(path, sizeof(path),
           "/system_data/priv/cache/profile/0x%08X/online.json", uid);
  raw = read_small_file(path, &len, PROFILE_JSON_MAX);
  if(!raw) {
    snprintf(note, n, "profile cache: no readable online.json for 0x%08X", uid);
    return;
  }
  if(!json_first_name_span(raw, &vs, &ve)) {
    snprintf(note, n, "profile cache: online.json has no plain \"firstName\" "
                      "value — left alone");
    free(raw);
    return;
  }

  vlen = ve - vs;
  nlen = (long)strlen(new_name);
  cplen = (vlen < (long)sizeof(old) - 1) ? vlen : (long)sizeof(old) - 1;
  memcpy(old, vs, (size_t)cplen);
  old[cplen] = 0;

  if(vlen == nlen && !memcmp(vs, new_name, (size_t)nlen)) {
    snprintf(note, n, "profile cache: already \"%s\"", new_name);
    free(raw);
    return;
  }

  blen = len + nlen - vlen;
  body = malloc((size_t)blen + 1);
  if(!body) {
    snprintf(note, n, "profile cache: out of memory");
    free(raw);
    return;
  }
  memcpy(body, raw, (size_t)(vs - raw));
  memcpy(body + (vs - raw), new_name, (size_t)nlen);
  memcpy(body + (vs - raw) + nlen, ve, (size_t)(len - vlen));
  free(raw);

  /* 只在「这条路径还没进过备份日志」时登记：safety_backup_file() 每次调用都会
   * 用当前内容覆盖已存的 blob，改过之后再登记就会把新名当成原值。 */
  if(safety_file_was_absent(path) < 0 && safety_backup_file(path) != 0) {
    snprintf(note, n, "profile cache: backup refused — left alone");
    free(body);
    return;
  }
  if(write_file(path, (unsigned char *)body, (size_t)blen) != 0) {
    snprintf(note, n, "profile cache \"%s\" -> \"%s\": write refused",
             old, new_name);
    free(body);
    return;
  }
  free(body);

  raw = read_small_file(path, &len, PROFILE_JSON_MAX);
  if(raw && json_first_name_span(raw, &vs, &ve) &&
     ve - vs == nlen && !memcmp(vs, new_name, (size_t)nlen))
    snprintf(note, n, "profile cache \"%s\" -> \"%s\" verified", old, new_name);
  else
    snprintf(note, n, "profile cache: wrote \"%s\" but readback disagrees",
             new_name);
  free(raw);
}

/* /user/home/<目录>/username.dat 按纯文本名字读（对齐 activitydb 的读法）。
 * 等值闸门：内容去掉尾部 \r\n\0 之后必须正好等于旧名才改写，其余一律不动 ——
 * 这个文件没有权威格式文档，猜不得。行尾字节原样保留。 */
static void
rename_username_dat(const char *old_name, const char *new_name,
                    char *note, size_t n) {
  DIR *d;
  struct dirent *de;
  long olen = (long)strlen(old_name);
  long nlen = (long)strlen(new_name);
  int scanned = 0, hit = 0;
  char where[48] = "";

  d = opendir("/user/home");
  if(!d) {
    snprintf(note, n, "user home: /user/home unreadable");
    return;
  }

  while((de = readdir(d)) != NULL) {
    char path[256];
    char *raw, *body;
    long len, name_len, tail;

    if(de->d_name[0] == '.') continue;
    snprintf(path, sizeof(path), "/user/home/%s/username.dat", de->d_name);
    raw = read_small_file(path, &len, USERNAME_DAT_MAX);
    if(!raw) continue;
    scanned++;

    name_len = len;
    while(name_len > 0 && (raw[name_len-1] == '\n' || raw[name_len-1] == '\r' ||
                           raw[name_len-1] == 0)) name_len--;
    if(name_len != olen || (olen > 0 && memcmp(raw, old_name, (size_t)olen) != 0)) {
      free(raw);
      continue;
    }

    tail = len - name_len;
    body = malloc((size_t)(nlen + tail + 1));
    if(!body) { free(raw); hit = -1; break; }
    memcpy(body, new_name, (size_t)nlen);
    memcpy(body + nlen, raw + name_len, (size_t)tail);
    snprintf(where, sizeof(where), "%s", de->d_name);
    free(raw);

    if(safety_file_was_absent(path) < 0 && safety_backup_file(path) != 0) {
      snprintf(note, n, "user home: %s/username.dat found but backup refused "
                        "— left alone", where);
      hit = -1;
    } else if(write_file(path, (unsigned char *)body,
                         (size_t)(nlen + tail)) == 0) {
      hit = 1;
    } else {
      snprintf(note, n, "user home: %s/username.dat write refused", where);
      hit = -1;
    }
    free(body);
    break;   /* 名字全局唯一，改到一份就够 */
  }
  closedir(d);

  if(hit > 0)
    snprintf(note, n, "user home: %s/username.dat -> \"%s\"", where, new_name);
  else if(hit == 0)
    snprintf(note, n, "user home: scanned %d dir(s), none holds \"%s\"",
             scanned, old_name);
}

static long
mem_find(const char *hay, long hlen, const char *needle) {
  long nlen = (long)strlen(needle), i;

  if(nlen <= 0 || hlen < nlen) return -1;
  for(i = 0; i + nlen <= hlen; i++)
    if(!memcmp(hay + i, needle, (size_t)nlen)) return i;
  return -1;
}

/* 只读探针。user_list.dat 是壳层用户名单的最后一个候选，实测只有 72 字节、
 * 结构没有文档，写它等于拿全体账号名单去赌 —— 所以这一层只查「旧名/新名的字节
 * 有没有出现在里面、在第几个字节」，把结论当证据报出去，一个字节都不动。 */
static void
probe_user_list_dat(const char *old_name, const char *new_name,
                    char *note, size_t n) {
  const char *path = "/system_data/priv/home/user_list.dat";
  char oa[32], na[32];
  char *raw;
  long len, oidx, nidx;

  raw = read_small_file(path, &len, PROFILE_JSON_MAX);
  if(!raw) {
    snprintf(note, n, "user_list.dat: absent or unreadable");
    return;
  }
  oidx = mem_find(raw, len, old_name);
  nidx = mem_find(raw, len, new_name);
  free(raw);

  if(oidx >= 0) snprintf(oa, sizeof(oa), "at 0x%lx", (unsigned long)oidx);
  else          snprintf(oa, sizeof(oa), "not found");
  if(nidx >= 0) snprintf(na, sizeof(na), "at 0x%lx", (unsigned long)nidx);
  else          snprintf(na, sizeof(na), "not found");
  snprintf(note, n, "user_list.dat %ld B: old %s / new %s (read-only probe)",
           len, oa, na);
}

int
npsignin_rename(uint32_t uid, const char *old_name, const char *new_name,
                npsignin_result_t *out) {
  char now[RENAME_NAME_BYTES + 1];
  uint32_t off;
  int taken;

  init_result(out, uid, old_name);
  fill_status(out);

  if(!name_is_sane(new_name)) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message),
             "rejected: new name must be 1..%d bytes, no control chars",
             RENAME_NAME_BYTES - 1);
    return -1;
  }
  if(out->slot == 0) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message),
             "rejected: no registry slot for \"%s\" — renaming would have "
             "nowhere to write", old_name);
    return -1;
  }
  if(!strcmp(old_name, new_name)) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message), "already named \"%s\"",
             new_name);
    return -1;
  }

  /* 查重：名字是 uid↔槽的唯一关联（slot_for_name 靠它），撞名会让两个账号
   * 在同一格里互相顶替，激活/登录状态立刻错乱。 */
  taken = find_slot(new_name);
  if(taken != 0) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message),
             "rejected: \"%s\" already occupies slot %d", new_name, taken);
    return -1;
  }

  off = (uint32_t)(out->slot - 1) * 65536u;
  if(wset_str(REG_KEY_USERNAME + off, new_name, RENAME_NAME_BYTES) != 0) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message),
             "rename failed: backup or registry write refused (slot %d)",
             out->slot);
    return -1;
  }

  /* 回读比对：只信读回来的字节，不信 Set 的返回值。 */
  memset(now, 0, sizeof(now));
  if(sceRegMgrGetStr(REG_KEY_USERNAME + off, now, RENAME_NAME_BYTES) != 0 ||
     strcmp(now, new_name) != 0) {
    out->ok = 0;
    snprintf(out->message, sizeof(out->message),
             "written but NOT verified: slot %d reads back \"%s\" — "
             "use Restore", out->slot, now[0] ? now : "(empty)");
    return -1;
  }

  /* 第二层：UserService 才是壳层「切换用户」列表实际显示的那一份。只写注册表会出现
   * 「简介里改了、切换列表和重启后都还是旧名」，所以这里补一次 Set 再回读。
   * 用定长零填充缓冲 + max_size=17（与 SysReg 的 STR(17) 对称）：Set 的第三个参数
   * 无论按「容量」还是按「拷贝长度」解释，都不会读到越界字节。 */
  {
    char buf[RENAME_NAME_BYTES];
    char shell[24] = {0};
    char base[256];
    char l3[128] = "", l4[256] = "", l5[160] = "";
    int set_rc, get_rc;

    memset(buf, 0, sizeof(buf));
    snprintf(buf, sizeof(buf), "%s", new_name);
    set_rc = sceUserServiceSetUserName((int32_t)uid, buf, sizeof(buf));
    get_rc = sceUserServiceGetUserName((int32_t)uid, shell, sizeof(shell));

    if(set_rc == 0 && get_rc == 0 && !strcmp(shell, new_name))
      snprintf(base, sizeof(base),
               "slot %d renamed %s -> %s (registry + UserService both verified)",
               out->slot, old_name, new_name);
    else if(set_rc != 0)
      snprintf(base, sizeof(base),
               "slot %d renamed %s -> %s in registry (verified), but UserService "
               "Set refused (0x%x) — the shell user list may keep the old name; "
               "original value is in the backup log",
               out->slot, old_name, new_name, (unsigned)set_rc);
    else
      snprintf(base, sizeof(base),
               "slot %d renamed %s -> %s in registry (verified); UserService "
               "reads \"%s\" (get 0x%x) — shell layer NOT confirmed, use Restore "
               "if it looks wrong",
               out->slot, old_name, new_name,
               (get_rc == 0 && shell[0]) ? shell : "(unreadable)",
               (unsigned)get_rc);

    /* 第三、四层：写不写得动都只报一句话，绝不因此判失败。这两条加上末尾那条
     * 只读探针，就是本轮的取证手段 ——「主界面和切换用户列表到底读哪一份」，
     * 由它们的结果来回答。 */
    rename_profile_cache(uid, new_name, l3, sizeof(l3));
    rename_username_dat(old_name, new_name, l4, sizeof(l4));
    probe_user_list_dat(old_name, new_name, l5, sizeof(l5));
    snprintf(out->message, sizeof(out->message),
             "%s | %s | %s | %s", base, l3, l4, l5);
  }

  out->ok = 1;
  snprintf(out->user_name, sizeof(out->user_name), "%s", new_name);
  return 0;
}


/* ------------------------------------------------------------------ *
 * 知情预览（只读）                                                      *
 * ------------------------------------------------------------------ */

int
npsignin_signin_plan(uint32_t uid, const char *name,
                     uint32_t *ents, int cap, int *n_out) {
  npsignin_result_t st;
  unsigned char cfg[sizeof(config_dat)];
  int i, n;

  init_result(&st, uid, name);
  fill_status(&st);
  if(st.slot <= 0 || st.account_id == 0) return -1;

  make_cfg(&st, cfg);

  /* 和 npsignin_signin() 末尾那次 write_registry_state() 一模一样的调用顺序，
   * 所以清单里出现的就是真会写的：只有目标槽，不再连带 base 槽 1。 */
  g_plan_n = 0;
  g_plan = 1;
  write_registry_state(cfg, (uint32_t)(st.slot - 1) * 65536u);
  g_plan = 0;

  n = g_plan_n;
  if(n > cap) n = cap;
  for(i = 0; i < n; i++) ents[i] = g_plan_ents[i];
  if(n_out) *n_out = n;
  return st.slot;
}
