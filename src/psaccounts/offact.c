/* Elf Arsenal — offline account activation.
   Registry plumbing vendored from ps5-payload-dev/offact (GPL-3.0).
   Only the sceRegMgr* side-effects are kept; the SDL2 UI is dropped
   in favour of the existing web Settings panel. */

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "offact.h"
#include "safety.h"

int sceRegMgrGetInt(int, int*);
int sceRegMgrGetStr(int, char*, size_t);
int sceRegMgrGetBin(int, void*, size_t);
int sceRegMgrSetInt(int, int);
int sceRegMgrSetBin(int, const void*, size_t);
int sceRegMgrSetStr(int, const char*, size_t);


/* 写入前记原值。size 一律沿用调用方传给 Set 的那个值，而不是注册表声明长度：
 * user_name 在 regmgr.h 里是 STR(17)，但上游 offact 传的是 OFFACT_NAME_MAX(32)。
 * 备份与还原必须用同一个长度才对称，所以这里跟着 Set 走；这个越界读/写本身
 * 作为已知风险记录在 PLAN.md，不在这一版里改动上游语义。 */
static int note(int ent, int type, int size) {
  safety_key_t k = { (uint32_t)ent, type, size };
  return safety_backup_reg(&k, 1, 0);
}


static int
slot_key(int slot, int base, int stride, int fallback) {
  if(slot < 1 || slot > OFFACT_SLOT_COUNT) return fallback;
  return (slot - 1) * stride + base;
}


#define KEY_NAME(s)   slot_key((s), 125829632, 65536, 127140352)
#define KEY_ID(s)     slot_key((s), 125830400, 65536, 127141120)
#define KEY_TYPE(s)   slot_key((s), 125874183, 65536, 127184903)
#define KEY_FLAGS(s)  slot_key((s), 125831168, 65536, 127141888)


int
offact_get_name(int slot, char out[OFFACT_NAME_MAX]) {
  *out = 0;
  return sceRegMgrGetStr(KEY_NAME(slot), out, OFFACT_NAME_MAX);
}


/* 这里原本有个 offact_set_name()，按上游那样用 OFFACT_NAME_MAX(32) 往声明为
 * STR(17) 的 user_name 里写 —— 会顺带覆盖后面 15 字节。本工程里没有任何调用方
 * （改名走 npsignin_rename()，备份和写入都用声明长度 17），所以直接删掉，
 * 不给这条越界写留入口。 */


int
offact_get_id(int slot, uint64_t *out) {
  *out = 0;
  return sceRegMgrGetBin(KEY_ID(slot), out, sizeof(uint64_t));
}


int
offact_set_id(int slot, uint64_t id) {
  if(note(KEY_ID(slot), SAFETY_BIN, (int)sizeof(uint64_t)) < 0) return -1;
  return sceRegMgrSetBin(KEY_ID(slot), &id, sizeof(uint64_t));
}


int
offact_get_type(int slot, char out[OFFACT_TYPE_MAX]) {
  *out = 0;
  return sceRegMgrGetStr(KEY_TYPE(slot), out, OFFACT_TYPE_MAX);
}


int
offact_set_type(int slot, const char *type) {
  if(!type) return -1;
  if(note(KEY_TYPE(slot), SAFETY_STR, OFFACT_TYPE_MAX) < 0) return -1;
  return sceRegMgrSetStr(KEY_TYPE(slot), type, OFFACT_TYPE_MAX);
}


int
offact_get_flags(int slot, int *out) {
  *out = 0;
  return sceRegMgrGetInt(KEY_FLAGS(slot), out);
}


int
offact_set_flags(int slot, int flags) {
  if(note(KEY_FLAGS(slot), SAFETY_INT, 4) < 0) return -1;
  return sceRegMgrSetInt(KEY_FLAGS(slot), flags);
}


uint64_t
offact_gen_id(const char *name) {
  uint64_t h = 0x5EAF00D / 0xCA7F00D;  /* upstream constant */
  if(name && *name) {
    while(*name) {
      h = 0x100000001B3ULL * (h ^ (uint8_t)*name);
      name++;
    }
  }
  return h;
}


/* [CN-FORK] v0.5.2.111 按**本地用户 id** 派生默认账号 id，见 offact.h 的说明。
 * 种子是 uid 的 4 个小端字节 —— 与注册表里存的那 4 字节同序，任何机器上算出来都一样。
 * 注意 offact_gen_id() 的起始值 0x5EAF00D / 0xCA7F00D 是整数除法，结果就是 0，
 * 所以那套 FNV-1a 的 offset basis 实际是 0；这里沿用同一个起点，两套哈希才同源。 */
uint64_t
offact_gen_id_for_user(uint32_t uid) {
  uint8_t b[4];
  uint64_t h = 0;

  b[0] = (uint8_t)(uid      );
  b[1] = (uint8_t)(uid >>  8);
  b[2] = (uint8_t)(uid >> 16);
  b[3] = (uint8_t)(uid >> 24);
  for(int i = 0; i < 4; i++) h = 0x100000001B3ULL * (h ^ b[i]);
  return h;
}


int
offact_activate(int slot, uint64_t id) {
  char name[OFFACT_NAME_MAX];
  if(offact_get_name(slot, name) || !name[0]) return -1;
  /* [CN-FORK] v0.5.2.111 这里原来有一句 `if(!id) id = offact_gen_id(name);` —— 按名字派生。
   * 那正是「改名把账号 ID 一起改了」的来源，而且它还会让**界面预览的 id 和实际写入的 id
   * 走两套算法**（预览已改成按 uid 派生）。默认值一律由调用方（accounts.c）算好后显式传进来，
   * id==0 视为调用方漏算，直接拒绝 —— 宁可不写，也不能写进去一个改名就会变的 id。 */
  if(!id) return -1;
  /* 这一批写入的来源是「离线激活」。登出时按来源筛，就能只退登录态、把这三条留住
   * —— 见 safety.h 的 SAFETY_ORIGIN_M_SIGNOUT_ONLY。 */
  safety_set_origin(SAFETY_ORIGIN_ACTIVATE);
  if(offact_set_id(slot, id))      return -1;
  if(offact_set_type(slot, "np"))  return -1;
  if(offact_set_flags(slot, OFFACT_DEFAULT_FLAGS)) return -1;
  return 0;
}


int
offact_clear(int slot) {
  safety_set_origin(SAFETY_ORIGIN_ACTIVATE);
  if(offact_set_id(slot, 0))   return -1;
  if(offact_set_flags(slot, 0)) return -1;
  return 0;
}
