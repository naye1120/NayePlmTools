/* PS5 Account Tools — unified account view + actions. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include "accounts.h"
#include "npsignin.h"
#include "offact.h"
#include "safety.h"


/* ------------------------------------------------------------------ *
 * SceUserService                                                       *
 * ------------------------------------------------------------------ */

int sceUserServiceGetUserName(int32_t user_id, char *name, size_t max_size);

/* 只读：自动登录账号判定源。sceUserServiceGetInitialUser 在真机 payload 权限下是失败
   的（0x80940004），所以只能读注册表全局键。现在只用它标"★自动登录"，不再用来
   拦写入。 */
int sceRegMgrGetInt(uint32_t key, int32_t *value);
int sceRegMgrGetStr(uint32_t key, char *value, size_t size);

/* 只读：当前前台账号（界面右上角那个）。它不在注册表里，是 UserService 的运行时
   状态，只能这样取。 */
int sceUserServiceGetForegroundUser(uint32_t *user_id);

/* 远程游玩配对 PIN：这三个是系统库 libSceRemoteplay 的真接口，SDK 只给符号不给头，
   所以自己声明（参考实现 ActRemoteLink 也一样）。GeneratePinCode 出的 8 位数字就是
   主机菜单里显示的那一个，不是我们编的。 */
int sceRemoteplayInitialize(void *arg, size_t size);
int sceRemoteplayGeneratePinCode(uint32_t *pin);
int sceRemoteplayNotifyPinCodeError(int code);
/* 用完必须收尾：参考实现（ActRemoteLink）从不 Terminate，结果是我们上一次进程没了、
   服务被死进程占住，之后 Initialize 稳定返回 0x80fc0003，直到重启主机才恢复（已实测）。
   原型没有公开头文件可比对；按 Initialize 的形状传 (NULL, 0) —— 若真实原型是 (void)，
   arm64 下多带的零值寄存器会被忽略，比反过来"少传参数"要安全。 */
int sceRemoteplayTerminate(void *arg, size_t size);
/* 配对的"落账"接口：出完码之后必须反复问它，stat==2 才算这台 Chiaki 真被主机
   批准了。参考实现（ActRemoteLink）出码后就是靠每秒轮询它过 300 秒的；我们上一版
   只出码就收尾、从没调过它，结果是"能添加主机但连不上"—— Chiaki 存下的是一个没有
   批准记录的主机。stat==3 = 配对失败（err 0x80FC1047 = PIN 错，0x80FC1040 =
   Account ID 错）。原型没有公开头文件，按参考实现的 (int*, int*) 形状声明。 */
int sceRemoteplayConfirmDeviceRegist(int *stat, int *err);

#define REG_ENT_AUTO_LOGIN_USER  0x07010000u
#define REG_SLOT_BASE            125829120u   /* 0x07800000，USER 槽 1 的基址 */
#define REG_SLOT_STRIDE          65536u       /* 0x10000 */
#define REG_ENT_SLOT_USER_ID     (REG_SLOT_BASE + 0x100u)   /* INT   */
#define REG_ENT_SLOT_NAME        (REG_SLOT_BASE + 0x200u)   /* STR17 */
#define REG_ENT_SLOT_LOGIN_FLAG  (REG_SLOT_BASE + 0x800u)   /* INT   */
#define REG_ENT_SLOT_ACCOUNT_ID  (REG_SLOT_BASE + 0x500u)   /* BIN8  */
#define REG_NAME_BYTES           17


/* ------------------------------------------------------------------ *
 * 展示用的两个全局判据（只读）                                         *
 * ------------------------------------------------------------------ */

uint32_t
accounts_main_uid(void) {
  int32_t v = 0;
  if(sceRegMgrGetInt(REG_ENT_AUTO_LOGIN_USER, &v) != 0) return 0;
  if(v <= 0) return 0;
  return (uint32_t)v;
}

static uint32_t
foreground_uid(void) {
  uint32_t uid = 0;
  if(sceUserServiceGetForegroundUser(&uid) != 0) return 0;
  return uid;
}

/* login_flag（槽基址 +0x800）的实测四态，来自真机只读探针：
 *   0 = 空槽、65536 = 本地新建未激活、4098 = 已激活未登录、6 = 已激活已登录。
 * 激活与否仍然只认 account_id != 0，这里只是把状态说成人话。 */
static const char *
slot_state(uint64_t account_id, int32_t login_flag) {
  if(!account_id) return login_flag == 0 ? "empty" : "created";
  return login_flag == 6 ? "signed-in" : "activated-offline";
}

/* ------------------------------------------------------------------ *
 * Helpers                                                              *
 * ------------------------------------------------------------------ */

static const char b64tab[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void
base64_8(const uint8_t *in, char *out) {
  size_t i, j;
  for(i = 0, j = 0; i < 8; ) {
    uint32_t o0 = in[i++];
    uint32_t o1 = i < 8 ? in[i++] : 0;
    uint32_t o2 = i < 8 ? in[i++] : 0;
    uint32_t v = (o0 << 16) | (o1 << 8) | o2;
    out[j++] = b64tab[(v >> 18) & 63];
    out[j++] = b64tab[(v >> 12) & 63];
    out[j++] = (i >= 9) ? '=' : b64tab[(v >> 6) & 63];
    out[j++] = (i >= 8) ? '=' : b64tab[(v) & 63];
  }
  out[j] = '\0';
}

/* Append a JSON-escaped string. Returns chars written. */
static int
esc_json(char *dst, size_t cap, const char *src) {
  int o = 0;
  for(int i = 0; src[i] && o < (int)cap - 2; i++) {
    char c = src[i];
    if(c == '"' || c == '\\') { dst[o++] = '\\'; dst[o++] = c; }
    else if((unsigned char)c < 0x20) { /* skip control chars */ }
    else dst[o++] = c;
  }
  dst[o] = 0;
  return o;
}

static int
slot_for_name(const char *name) {
  if(!name || !name[0]) return 0;
  for(int s = 1; s <= OFFACT_SLOT_COUNT; s++) {
    char b[OFFACT_NAME_MAX] = {0};
    if(offact_get_name(s, b) == 0 && b[0] && !strcmp(b, name)) return s;
  }
  return 0;
}


/* Emit one account object into buf. Returns chars written (excluding NUL). */
static int
account_obj(char *buf, size_t cap, uint32_t uid, const char *name) {
  npsignin_result_t st;
  int32_t login_flag = 0;
  uint32_t fg, m;
  npsignin_status(uid, name, &st);

  /* 前台账号和自动登录账号都只是"标一下"，供界面显示，不再决定谁能被写。 */
  fg = foreground_uid();
  m = accounts_main_uid();
  if(st.slot > 0)
    sceRegMgrGetInt(REG_ENT_SLOT_LOGIN_FLAG
                    + (uint32_t)(st.slot - 1) * REG_SLOT_STRIDE, &login_flag);

  char ename[OFFACT_NAME_MAX * 2];
  esc_json(ename, sizeof(ename), name[0] ? name : "");

  char idhex[24] = "", idb64[16] = "", iddec[24] = "";
  if(st.account_id) {
    uint8_t raw[8];
    memcpy(raw, &st.account_id, 8);
    snprintf(idhex, sizeof(idhex), "0x%016llx", (unsigned long long)st.account_id);
    base64_8(raw, idb64);
    snprintf(iddec, sizeof(iddec), "%llu", (unsigned long long)st.account_id);
  }

  /* The id that "Activate" would assign if none is set yet. */
  char derived[24] = "";
  if(!st.account_id && name[0]) {
    uint64_t d = offact_gen_id(name);
    snprintf(derived, sizeof(derived), "0x%016llx", (unsigned long long)d);
  }

  return snprintf(buf, cap,
    "{\"userId\":%u,\"userIdHex\":\"0x%08X\",\"name\":\"%s\",\"slot\":%d,"
    "\"activated\":%s,\"signedIn\":%s,"
    "\"current\":%s,\"autoLogin\":%s,\"loginFlag\":%d,\"state\":\"%s\","
    "\"pendingRecords\":%u,"
    "\"accountId\":\"%s\",\"accountIdB64\":\"%s\",\"accountIdDec\":\"%s\","
    "\"derivedId\":\"%s\"}",
    uid, uid, ename, st.slot,
    st.activated ? "true" : "false",
    st.signed_in ? "true" : "false",
    (fg && uid == fg) ? "true" : "false",
    (m && uid == m) ? "true" : "false",
    login_flag, slot_state(st.account_id, login_flag),
    (unsigned int)safety_reg_count_scope(uid),
    idhex, idb64, iddec, derived);
}


/* ------------------------------------------------------------------ *
 * Public API                                                          *
 * ------------------------------------------------------------------ */

char *
accounts_list_json(void) {
  size_t cap = 8192;
  char *buf = malloc(cap);
  int len, slot, first = 1;
  if(!buf) return NULL;

  len = snprintf(buf, cap,
                 "{\"ok\":true,\"mainUserId\":%u,\"currentUserId\":%u,"
                 "\"accounts\":[",
                 accounts_main_uid(), foreground_uid());

  /* 列表来源是扫 16 个注册表槽，不是 sceUserServiceGetLoginUserIdList —— 后者只
     报"当前在登录列表里"的账号，切走前台的账号就从界面上消失了（实测：靶账号
     造出来之后点不到）。槽里的名字 + user_id 才是常驻事实。 */
  for(slot = 1; slot <= OFFACT_SLOT_COUNT; slot++) {
    uint32_t off = (uint32_t)(slot - 1) * REG_SLOT_STRIDE;
    char name[REG_NAME_BYTES + 1] = {0};
    int32_t uid = 0;
    char obj[1024];
    int n;

    if(sceRegMgrGetStr(REG_ENT_SLOT_NAME + off, name, REG_NAME_BYTES) != 0)
      continue;
    if(!name[0]) continue;                       /* 空槽 */
    if(sceRegMgrGetInt(REG_ENT_SLOT_USER_ID + off, &uid) != 0 || uid <= 0)
      continue;

    n = account_obj(obj, sizeof(obj), (uint32_t)uid, name);
    if(n <= 0) continue;
    if(len + n + 4 >= (int)cap) break;
    len += snprintf(buf + len, cap - len, "%s%s", first ? "" : ",", obj);
    first = 0;
  }

  snprintf(buf + len, cap - len, "]}");
  return buf;
}

char *
accounts_action(const char *action, uint32_t user_id, uint64_t custom_id,
                const char *arg) {
  char name[24] = {0};
  sceUserServiceGetUserName((int32_t)user_id, name, sizeof(name));

  int ok = 0;
  char message[sizeof(((npsignin_result_t *)0)->message)] = "";
  npsignin_result_t st;

  memset(&st, 0, sizeof(st));

  /* 这批写入的归属：备份日志里每条原值都会带上这个 uid，"登出"才能力求只回滚
     它自己名下的记录。preview 只读，记不记都一样。 */
  safety_set_scope(user_id);
  /* 来源先归零：g_origin 是常驻状态，不重置的话"这一批没设来源"会继承上一次请求的
     值，把记录错误地标成 SIGNIN/ACTIVATE。各动作在自己的写入点再设具体来源。 */
  safety_set_origin(SAFETY_ORIGIN_NONE);

  /* 写操作的前置条件：备份目录必须可写——抄不到原值就不许动。六个动作对所有槽
     一视同仁，不再有"某个账号是禁区"。 */
  if(safety_init() != 0) {
    snprintf(message, sizeof(message),
             "ABORTED: cannot write backup log under %s — refusing an "
             "irreversible change", SAFETY_DIR);
  } else if(!strcmp(action, "activate")) {
    int slot = slot_for_name(name);
    if(slot <= 0) {
      snprintf(message, sizeof(message), "no registry slot for this user");
    } else if(offact_activate(slot, custom_id) == 0) {
      ok = 1;
      snprintf(message, sizeof(message),
               "activated %s — you can sign in now", name);
    } else {
      snprintf(message, sizeof(message), "activation failed");
    }
    npsignin_status(user_id, name, &st);
  } else if(!strcmp(action, "deactivate")) {
    int slot = slot_for_name(name);
    if(slot > 0 && offact_clear(slot) == 0) {
      ok = 1;
      snprintf(message, sizeof(message), "deactivated %s", name);
    } else {
      snprintf(message, sizeof(message), "deactivate failed");
    }
    npsignin_status(user_id, name, &st);
  } else if(!strcmp(action, "signin")) {
    npsignin_signin(user_id, name, &st);
    ok = st.ok;
    snprintf(message, sizeof(message), "%s", st.message);
  } else if(!strcmp(action, "signout")) {
    /* [CN-FORK] v0.5.2.107 「登出」= 只退登录态，离线激活留着。
     * 以前这里会把激活一起退掉，用户点「登出」却发现账号被打回未激活 —— 现在分成
     * 两条路：keep_activation=1 是「登出」，=0 是「登出并反激活」（signout-full）。 */
    npsignin_signout(user_id, name, &st, 1);
    ok = st.ok;
    snprintf(message, sizeof(message), "%s", st.message);
  } else if(!strcmp(action, "signout-full")) {
    /* 「完全还原」：不分来源全退，账号回到未激活。仍是一键清账的唯一入口。 */
    npsignin_signout(user_id, name, &st, 0);
    ok = st.ok;
    snprintf(message, sizeof(message), "%s", st.message);
  } else if(!strcmp(action, "rename")) {
    if(!arg || !*arg) {
      snprintf(message, sizeof(message),
               "rejected: rename needs a new name (?user=&to=)");
    } else {
      npsignin_rename(user_id, name, arg, &st);
      ok = st.ok;
      snprintf(message, sizeof(message), "%s", st.message);
      /* 改名成功后，后续展示一律用新名字（user_name 由 npsignin 回填）。 */
      if(st.ok) snprintf(name, sizeof(name), "%s", st.user_name);
    }
  } else {
    npsignin_status(user_id, name, &st);
    snprintf(message, sizeof(message), "unknown action");
  }

  char obj[1024];
  account_obj(obj, sizeof(obj), user_id, name);

  /* [CN-FORK] v0.5.2.101 改名 message 加长到 640（逐层回报），这里必须跟着放大：
   * emsg 小了会截掉后半句，cap 小了 snprintf 直接吐出半条 JSON。 */
  char emsg[896];
  esc_json(emsg, sizeof(emsg), message);

  size_t cap = 4096;
  char *buf = malloc(cap);
  if(!buf) return NULL;
  snprintf(buf, cap,
    "{\"ok\":%s,\"message\":\"%s\",\"account\":%s}",
    ok ? "true" : "false", emsg, obj);
  return buf;
}


/* ------------------------------------------------------------------ *
 * 备份层状态 / 一键还原                                                *
 * ------------------------------------------------------------------ */

static const char *
type_name(int t) {
  if(t == SAFETY_INT) return "int";
  if(t == SAFETY_BIN) return "bin";
  return "str";
}

/* 备份记录的来源名，给 /api/safety 的诊断清单用。
 * "none" 同时意味着"老日志（本字段出现之前记的）"，两种还原都会管到它。 */
static const char *
origin_name(int o) {
  switch(o) {
  case SAFETY_ORIGIN_ACTIVATE: return "activate";
  case SAFETY_ORIGIN_SIGNIN:   return "signin";
  case SAFETY_ORIGIN_RENAME:   return "rename";
  default:                     return "none";
  }
}

char *
accounts_safety_json(const char *mode) {
  int restore = (mode && !strcmp(mode, "restore"));
  char line[160];
  char emsg[384];
  size_t cap;
  char *buf;
  int n, i, len;
  int reg_done = 0, file_done = 0;
  char rmsg[256] = "";

  if(restore) {
    int r = safety_restore_reg();
    int f = safety_restore_all_files();
    if(r < 0 || f < 0) {
      snprintf(rmsg, sizeof(rmsg),
               "RESTORE INCOMPLETE: reg failures=%d file failures=%d — the "
               "backup log is kept so you can retry",
               r < 0 ? -r : 0, f < 0 ? -f : 0);
    } else {
      snprintf(rmsg, sizeof(rmsg),
               "restored %d registry value(s) and %d file(s); backup log "
               "cleared — reboot to apply", r, f);
    }
    reg_done = r > 0 ? r : 0;
    file_done = f > 0 ? f : 0;
  }

  safety_status_line(line, sizeof(line));
  n = safety_reg_count();
  esc_json(emsg, sizeof(emsg),
           restore ? rmsg : line);

  /* 每条记录 112 字节：多了 "origin":"activate" 这一段（v0.5.2.107）。
   * 原来按 80 算，加上来源后单条最长约 105 字节，会挤爆尾部的收尾括号。 */
  cap = 1024 + (size_t)n * 112;
  buf = malloc(cap);
  if(!buf) return NULL;

  len = snprintf(buf, cap,
    "{\"ok\":true,\"mode\":\"%s\",\"dir\":\"%s\",\"line\":\"%s\","
    "\"writable\":%s,\"regBackups\":%d,\"fileBackups\":%d,"
    "\"regRestored\":%d,\"filesRestored\":%d,\"message\":\"%s\","
    "\"entries\":[",
    restore ? "restore" : "status", SAFETY_DIR, line,
    safety_init() == 0 ? "true" : "false",
    n, safety_file_count(), reg_done, file_done, emsg);

  for(i = 0; i < n; i++) {
    uint32_t ent, scope = 0;
    int t, sz, ln, org = SAFETY_ORIGIN_NONE;
    if(safety_reg_at(i, &ent, &t, &sz, &ln, &scope, &org) != 0) continue;
    if(len + 112 >= (int)cap) break;
    len += snprintf(buf + len, cap - len, "%s{\"ent\":\"0x%08X\","
                    "\"entity\":%u,\"type\":\"%s\",\"declared\":%d,"
                    "\"bytes\":%d,\"owner\":%u,\"origin\":\"%s\"}",
                    i ? "," : "", ent, ent, type_name(t), sz, ln, scope,
                    origin_name(org));
  }

  snprintf(buf + len, cap - len, "]}");
  return buf;
}


/* ------------------------------------------------------------------ *
 * 知情预览（A3，只读）                                                 *
 * ------------------------------------------------------------------ */

char *
accounts_preview_json(uint32_t user_id) {
  uint32_t ents[64];
  char name[24] = {0};
  char *buf;
  size_t cap;
  int i, n = 0, len, slot, base = 0, mirror = 0;
  uint32_t m;

  sceUserServiceGetUserName((int32_t)user_id, name, sizeof(name));
  slot = npsignin_signin_plan(user_id, name, ents,
                              (int)(sizeof(ents)/sizeof(ents[0])), &n);
  m = accounts_main_uid();

  cap = 512 + (size_t)n * 16;
  buf = malloc(cap);
  if(!buf) return NULL;

  if(slot <= 0) {
    snprintf(buf, cap, "{\"ok\":false,\"action\":\"signin\",\"regKeys\":0,"
             "\"message\":\"nothing to preview: no registry slot or not "
             "activated — signin would be refused anyway\"}");
    return buf;
  }

  /* 分类：entity 属于哪个槽。去掉"无条件写 base 槽 1"之后，正常目标槽的计数全在
     targetSlotKeys 里、baseSlotKeys 只会在目标本身就是槽 1 时非零。留着这两个字段
     是为了让界面能直接证实"这次没有串到别人的格子"。 */
  for(i = 0; i < n; i++) {
    int s = (int)((ents[i] - REG_SLOT_BASE) / REG_SLOT_STRIDE) + 1;
    if(s == 1) base++; else mirror++;
  }

  len = snprintf(buf, cap,
    "{\"ok\":true,\"action\":\"signin\",\"slot\":%d,\"mainUserId\":%u,"
    "\"owner\":%u,\"pendingForOwner\":%u,"
    "\"targetIsMain\":%s,\"writesBaseSlot\":%s,\"baseSlotKeys\":%d,"
    "\"targetSlotKeys\":%d,\"regKeys\":%d,\"npFiles\":2,\"ents\":[",
    slot, m, user_id, (unsigned int)safety_reg_count_scope(user_id),
    (m && user_id == m) ? "true" : "false",
    base > 0 ? "true" : "false",
    base, mirror, n);

  for(i = 0; i < n && len < (int)cap - 24; i++)
    len += snprintf(buf + len, cap - len, "%s%u", i ? "," : "", ents[i]);
  snprintf(buf + len, cap - len, "]}");
  return buf;
}


/* ------------------------------------------------------------------ *
 * 远程游玩配对 PIN（只读，不写注册表、不碰文件）                        *
 * ------------------------------------------------------------------ */

/* 系统给 PIN 的有效期。参考实现按 300 秒轮询配对结果，这里照它的窗口来。
 * [CN-FORK] v0.5.2.99 用户要求压到 180 秒：这是我们自己收尾的截止时刻（看门狗到点
 * Terminate 会话、接口回 expired、界面倒计时按它走），不是固件给码的时长——系统那边
 * 的码仍按它自己的窗口存在，只是我们不再替它守着会话，180 秒后这条 PIN 就用不了了。 */
#define PIN_TTL_SEC 180

/* 这个进程里远程游玩会话的状态。三条都只服务于一条原则：**不要把会话留在半途**。
   上一次就是 Initialize 成功后进程直接没了，服务被死进程占住，之后每次调用都吃
   0x80fc0003，只能重启主机解锁（已实测）。出码之后不能立刻收尾（那样 Chiaki 能
   添加主机却连不上 —— 配对要靠 ConfirmDeviceRegist 轮询落账），所以会话保持打开，
   直到配对成功 / 失败 / 超时才关：
     rp_session_open —— 已经 Initialize 成功、还没 Terminate；
     rp_deadline     —— 这条 PIN 的到期时刻，到点由看门狗收尾（不看网页还在不在）；
     rp_wedged       —— Terminate 也失败了，会话多半仍卡着，此时再点只会重复失败，
                        直接拒掉并说明要重启，不让他反复按出一个更难查的状态。
   rp_mu 保护以上全部（含看门狗线程与网页轮询的并发）。 */
static pthread_mutex_t rp_mu = PTHREAD_MUTEX_INITIALIZER;
static int rp_session_open = 0;
static int rp_wedged = 0;
static time_t rp_deadline = 0;
static int rp_watchdog_running = 0;

/* 当前这条配对的样子，状态接口原样回给界面（页面刷新后还能接着看）。 */
static struct {
  int have;
  uint32_t pin, uid;
  int slot, activated, signed_in;
  char ename[48];
  char idhex[24];
  char idb64[16];
} rp_pair = {0};

/* 收尾：调用方必须持有 rp_mu。返回系统的原始码，0 = 干净退出。 */
static int
rp_close_locked(void) {
  int rc;
  if(!rp_session_open) return 0;
  rc = sceRemoteplayTerminate(NULL, 0);
  rp_session_open = 0;
  rp_deadline = 0;
  if(rc != 0) rp_wedged = 1;
  return rc;
}

/* 看门狗：超时收尾不能依赖网页还在轮询。上一次事故正是因为没人收尾，服务被死进程
   占住 —— 所以到期必须由进程自己关掉，浏览器关没关都一样。 */
static void *
rp_watchdog(void *arg) {
  (void)arg;
  for(;;) {
    pthread_mutex_lock(&rp_mu);
    if(rp_session_open && time(NULL) >= rp_deadline) rp_close_locked();
    if(!rp_session_open) {
      rp_watchdog_running = 0;
      pthread_mutex_unlock(&rp_mu);
      return NULL;
    }
    pthread_mutex_unlock(&rp_mu);
    sleep(1);
  }
}

/* 会话已卡住时的统一答复。rc_term 非 NULL = 这一次刚把会话弄卡，附上返回码；
   NULL = 之前就已经卡住了。注意错误码 0x80xxxxxx 在 int 里是**负数**，所以不能
   拿 >=0 当"有没有码"用。 */
static char *
pin_refuse_json(uint32_t uid, const char *ename, int slot, const int *rc_term) {
  size_t cap = 704;
  char *buf = malloc(cap);
  if(!buf) return NULL;
  if(rc_term)
    snprintf(buf, cap,
      "{\"ok\":false,\"userId\":%u,\"name\":\"%s\",\"slot\":%d,"
      "\"termRc\":\"0x%x\","
      "\"message\":\"上一次收尾 Terminate 失败 0x%x —— 会话多半还卡在远程游玩服务"
      "里，再点只会吃同一个错。请重启主机解锁，或者重新推一次 ELF（换新进程）再点\"}",
      uid, ename, slot, *rc_term, *rc_term);
  else
    snprintf(buf, cap,
      "{\"ok\":false,\"userId\":%u,\"name\":\"%s\",\"slot\":%d,"
      "\"message\":\"这个 payload 进程之前把远程游玩会话留在了半途，而且收尾"
      "也没成功 —— 再点只会继续吃同一个错误。请重启主机解锁服务，或者重新推一"
      "次 ELF（换新进程）再点\"}", uid, ename, slot);
  return buf;
}

/* 当前这条配对的样子，原样回给界面（页面刷新后还能接着看配到哪一步了）。 */
static int
pin_pair_json(char *dst, size_t cap, int remaining) {
  char ps[16], pr[16];
  snprintf(ps, sizeof(ps), "%04u %04u", rp_pair.pin / 10000, rp_pair.pin % 10000);
  snprintf(pr, sizeof(pr), "%08u", rp_pair.pin);
  return snprintf(dst, cap,
    "\"pin\":\"%s\",\"pinRaw\":\"%s\",\"expiresIn\":%d,\"remaining\":%d,"
    "\"userId\":%u,\"name\":\"%s\",\"slot\":%d,"
    "\"activated\":%s,\"signedIn\":%s,"
    "\"accountId\":\"%s\",\"accountIdB64\":\"%s\"",
    ps, pr, PIN_TTL_SEC, remaining, rp_pair.uid, rp_pair.ename, rp_pair.slot,
    rp_pair.activated ? "true" : "false", rp_pair.signed_in ? "true" : "false",
    rp_pair.idhex, rp_pair.idb64);
}

/* 生成 PIN（调用方必须持有 rp_mu）。会话保持打开，交给状态接口去轮询落账。 */
static char *
pin_generate_locked(uint32_t uid, const char *ename, const npsignin_result_t *st) {
  uint32_t pin = 0;
  char emsg[320];
  char idhex[24] = "", idb64[16] = "";
  char ps[16], mid[640];
  int rc_init, rc_gen, rc_term;
  size_t cap = 1536;
  char *buf;

  if(rp_wedged) return pin_refuse_json(uid, ename, st->slot, NULL);

  /* 任何时刻只允许一条会话：上一条还没走完就再点，先把上一条收掉。
     收不掉 = 会话已经卡住，立刻拒，不往里塞第二条。 */
  if(rp_session_open) {
    rc_term = rp_close_locked();
    if(rp_wedged) return pin_refuse_json(uid, ename, st->slot, &rc_term);
  }

  rc_init = sceRemoteplayInitialize(NULL, 0);
  if(rc_init != 0) {
    snprintf(emsg, sizeof(emsg),
             "sceRemoteplayInitialize 失败 0x%x —— 远程游玩服务没起来（多半是上一个"
             "进程留下的占用）。这一步之后没有生成 PIN", rc_init);
    buf = malloc(cap);
    if(!buf) return NULL;
    snprintf(buf, cap, "{\"ok\":false,\"userId\":%u,\"name\":\"%s\",\"slot\":%d,"
             "\"initRc\":\"0x%x\",\"message\":\"%s\"}", uid, ename, st->slot,
             rc_init, emsg);
    return buf;
  }
  rp_session_open = 1;

  /* 先作废上一条还没用完的 PIN，再生成新的（和主机菜单的行为一致）。 */
  sceRemoteplayNotifyPinCodeError(1);
  rc_gen = sceRemoteplayGeneratePinCode(&pin);
  if(rc_gen != 0) {
    /* 生成失败就立刻收尾：没有码可填，会话留着只是白占服务。 */
    rc_term = rp_close_locked();
    snprintf(emsg, sizeof(emsg),
             "sceRemoteplayGeneratePinCode 失败 0x%x（Initialize 是通的）。已尝试收尾"
             "，返回 0x%x", rc_gen, rc_term);
    buf = malloc(cap);
    if(!buf) return NULL;
    snprintf(buf, cap, "{\"ok\":false,\"userId\":%u,\"name\":\"%s\",\"slot\":%d,"
             "\"genRc\":\"0x%x\",\"termRc\":\"0x%x\",\"message\":\"%s\"}",
             uid, ename, st->slot, rc_gen, rc_term, emsg);
    return buf;
  }

  if(st->account_id) {
    uint8_t raw[8];
    memcpy(raw, &st->account_id, 8);
    snprintf(idhex, sizeof(idhex), "0x%016llx", (unsigned long long)st->account_id);
    base64_8(raw, idb64);
  }

  rp_pair.have = 1;
  rp_pair.pin = pin;
  rp_pair.uid = uid;
  rp_pair.slot = st->slot;
  rp_pair.activated = st->activated;
  rp_pair.signed_in = st->signed_in;
  snprintf(ps, sizeof(ps), "%04u %04u", pin / 10000, pin % 10000);
  snprintf(rp_pair.ename, sizeof(rp_pair.ename), "%s", ename);
  snprintf(rp_pair.idhex, sizeof(rp_pair.idhex), "%s", idhex);
  snprintf(rp_pair.idb64, sizeof(rp_pair.idb64), "%s", idb64);

  rp_deadline = time(NULL) + PIN_TTL_SEC;
  if(!rp_watchdog_running) {
    pthread_t th;
    if(pthread_create(&th, NULL, rp_watchdog, NULL) == 0) {
      rp_watchdog_running = 1;
      pthread_detach(th);
    }
  }

  pin_pair_json(mid, sizeof(mid), PIN_TTL_SEC);
  buf = malloc(cap);
  if(!buf) return NULL;
  snprintf(buf, cap,
    "{\"ok\":true,\"status\":\"waiting\",%s,"
    "\"message\":\"配对要填 PIN=%s + Account ID=%s（%s，槽 %d）。这一步只读："
    "没有写注册表也没有改文件；会话保持打开，等你在 Chiaki 里填完码，"
    "配对成功 / 配对失败 / 超时三条路都会收尾 Terminate\"}",
    mid, ps, idb64[0] ? idb64 : "（这个账号还没激活，没有 account_id）",
    ename, st->slot);
  return buf;
}

char *
accounts_pin_json(void) {
  uint32_t uid = foreground_uid();
  char name[24] = {0};
  char ename[48] = "";
  npsignin_result_t st;
  char *buf;
  size_t cap = 1536;

  if(!uid) {
    buf = malloc(cap);
    if(!buf) return NULL;
    snprintf(buf, cap, "{\"ok\":false,\"message\":\"读不到前台账号（"
             "sceUserServiceGetForegroundUser 失败），没有可配对的账号\"}");
    return buf;
  }

  sceUserServiceGetUserName((int32_t)uid, name, sizeof(name));
  memset(&st, 0, sizeof(st));
  npsignin_status(uid, name, &st);
  esc_json(ename, sizeof(ename), name);

  pthread_mutex_lock(&rp_mu);
  buf = pin_generate_locked(uid, ename, &st);
  pthread_mutex_unlock(&rp_mu);
  return buf;
}

/* ------------------------------------------------------------------ *
 * 配对状态：出完码之后由网页每秒问一次，轮询到落账为止                  *
 * ------------------------------------------------------------------ */

char *
accounts_pin_status_json(void) {
  size_t cap = 1536;
  char *buf = malloc(cap);
  char mid[640];
  int rc, stat = 0, err = 0, rc_term, remain;
  time_t now;

  if(!buf) return NULL;

  pthread_mutex_lock(&rp_mu);

  if(rp_wedged) {
    char *r = pin_refuse_json(rp_pair.uid, rp_pair.ename, rp_pair.slot, NULL);
    pthread_mutex_unlock(&rp_mu);
    free(buf);
    return r;
  }

  if(!rp_session_open) {
    pthread_mutex_unlock(&rp_mu);
    snprintf(buf, cap, "{\"ok\":false,\"status\":\"idle\","
             "\"message\":\"当前没有进行中的配对 —— 点「生成 PIN」重新来一条\"}");
    return buf;
  }

  now = time(NULL);
  remain = (int)(rp_deadline - now);
  if(remain < 0) remain = 0;

  /* 到点了就收，即使网页已经不问了（看门狗也会做同一件事，这里只是不再往下问）。 */
  if(now >= rp_deadline) {
    rc_term = rp_close_locked();
    pin_pair_json(mid, sizeof(mid), 0);
    pthread_mutex_unlock(&rp_mu);
    snprintf(buf, cap, "{\"ok\":true,\"status\":\"expired\",%s,"
             "\"termRc\":\"0x%x\",\"message\":\"PIN 已过期，会话已收尾（0x%x）。"
             "重新点「生成 PIN」\"}", mid, rc_term, rc_term);
    return buf;
  }

  rc = sceRemoteplayConfirmDeviceRegist(&stat, &err);
  if(rc != 0) {
    /* 问不出来就继续等：超时兜底在上面，不会把会话漏掉。 */
    pin_pair_json(mid, sizeof(mid), remain);
    pthread_mutex_unlock(&rp_mu);
    snprintf(buf, cap, "{\"ok\":true,\"status\":\"waiting\",%s,"
             "\"confirmRc\":\"0x%x\",\"remaining\":%d,"
             "\"message\":\"ConfirmDeviceRegist 返回 0x%x，继续等配对\"}",
             mid, rc, remain, rc);
    return buf;
  }

  if(stat == 2) {
    rc_term = rp_close_locked();
    pin_pair_json(mid, sizeof(mid), remain);
    pthread_mutex_unlock(&rp_mu);
    snprintf(buf, cap, "{\"ok\":true,\"status\":\"paired\",%s,"
             "\"termRc\":\"0x%x\",\"message\":\"配对成功（ConfirmDeviceRegist 的 "
             "stat=2），会话已收尾（0x%x）。现在可以连远程游玩了\"}",
             mid, rc_term, rc_term);
    return buf;
  }

  if(stat == 3) {
    const char *why = (err == (int)0x80FC1047) ? "PIN 填错了" :
                      (err == (int)0x80FC1040) ? "Account ID 填错了" : "系统报了别的错";
    rc_term = rp_close_locked();
    pin_pair_json(mid, sizeof(mid), remain);
    pthread_mutex_unlock(&rp_mu);
    snprintf(buf, cap, "{\"ok\":true,\"status\":\"failed\",%s,"
             "\"pairErr\":\"0x%x\",\"termRc\":\"0x%x\","
             "\"message\":\"配对失败：%s（0x%x），会话已收尾（0x%x）。核对 Account "
             "ID 后重新点「生成 PIN」\"}",
             mid, err, rc_term, why, err, rc_term);
    return buf;
  }

  pin_pair_json(mid, sizeof(mid), remain);
  pthread_mutex_unlock(&rp_mu);
  snprintf(buf, cap, "{\"ok\":true,\"status\":\"waiting\",%s,"
           "\"stat\":%d,\"remaining\":%d,"
           "\"message\":\"正在等你在 Chiaki 里填 PIN + Account ID（还剩 %d 秒）\"}",
           mid, stat, remain, remain);
  return buf;
}

/* 进程退出前把还开着的会话关掉（/api/exit 那条路）。不关的话服务会被死进程占住，
   下次 Initialize 稳定吃 0x80fc0003 —— 这正是上一次事故的成因。 */
void
accounts_pin_shutdown(void) {
  pthread_mutex_lock(&rp_mu);
  if(rp_session_open) rp_close_locked();
  pthread_mutex_unlock(&rp_mu);
}
