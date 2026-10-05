/* accounts — 写前备份 / 写后还原层
 *
 * 这一层存在的理由都是原版留下的不可逆点：
 *   - npsignin.c 的 write_registry_state() 写 22 个键，但原版 signout() 只归零 3 个
 *   - write_np_files() 用 O_TRUNC 覆盖真实 config.dat，不留副本
 *   - 原版 signout() 的 unlink() 把真文件一并删掉
 *
 * 规则（由代码结构强制，不靠调用者自觉）：
 *   1. safety_set_*() 在找不到该 entity 的备份记录时**直接拒绝**，不写。
 *   2. safety_backup_file() 必须在打开任何系统文件前调用；不存在也要记成 ABSENT，
 *      这样"只删本次自造的文件"才有判据。
 *   3. 每条注册表记录都带**归属**（发起这次写入的 uid）与**来源**（哪一类动作写的）。
 *      "登出某个账号"只回滚它自己名下的记录，不能把别的账号攒下的原值一起清掉 ——
 *      见 safety_restore_scope()。来源则让"登出"能只退登录态、把离线激活留着 ——
 *      见 safety_restore_scope_mask()。
 *   4. dirty 状态由备份日志是否非空推出，只有全量还原把日志清空才算干净。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

/* 主机自测（test/safety_test.c）用 -DSAFETY_DIR 指向临时目录，其余保持一致 */
#ifndef SAFETY_DIR
#define SAFETY_DIR   "/data/accounts/safety"
#endif
#define SAFETY_REG_IDX   SAFETY_DIR "/reg.idx"
#define SAFETY_REG_DIR   SAFETY_DIR "/reg"
#define SAFETY_FILE_IDX  SAFETY_DIR "/files.idx"
#define SAFETY_FILE_DIR  SAFETY_DIR "/files"

/* 与 sceRegMgr 的三种类型一一对应 */
#define SAFETY_INT  1
#define SAFETY_STR  2
#define SAFETY_BIN  3

/* safety_restore_reg() 的"不看归属、全部回滚"哨兵。uid 是 int32 语义，
 * 0xFFFFFFFF(-1) 在 UserService 里恒为非法用户，绝不会被当成某个账号。 */
#define SAFETY_SCOPE_ALL  0xFFFFFFFFu

/* ------------------------------------------------------------------ *
 * 备份记录的**来源**（v0.5.2.107）                                      *
 *                                                                     *
 * 同一个 uid 名下会混着好几类写入：离线激活写 3 个键、登入写 22 个键 + *
 * 2 个文件、改名写若干键。而"登出"要的是「退掉登录态、把激活留着」，   *
 * 光按 uid 筛做不到，所以每条记录额外记下是谁写的。                    *
 *                                                                     *
 * NONE 必须被**两种**还原都管到：它是老日志（本字段出现之前记下的）和  *
 * 调用方没设来源时的取值。宁可多回滚（回滚 = 写回原值，只会更接近原始  *
 * 状态），也绝不能因为格式换代把原值丢掉 —— 那等于永久失去还原能力。   *
 * ------------------------------------------------------------------ */
#define SAFETY_ORIGIN_NONE      0   /* 未标注（老日志）/ 调用方没设 */
#define SAFETY_ORIGIN_ACTIVATE  1   /* offact_activate() / offact_clear() */
#define SAFETY_ORIGIN_SIGNIN    2   /* npsignin_signin() */
#define SAFETY_ORIGIN_RENAME    3   /* npsignin_rename() */

/* 按位选来源：mask = 1u << origin。不筛用 M_ALL。 */
#define SAFETY_ORIGIN_M_NONE       (1u << SAFETY_ORIGIN_NONE)
#define SAFETY_ORIGIN_M_ACTIVATE   (1u << SAFETY_ORIGIN_ACTIVATE)
#define SAFETY_ORIGIN_M_SIGNIN     (1u << SAFETY_ORIGIN_SIGNIN)
#define SAFETY_ORIGIN_M_RENAME     (1u << SAFETY_ORIGIN_RENAME)
#define SAFETY_ORIGIN_M_ALL        0xFFFFFFFFu

/* 「仅登出」的筛选范围：登录态 + 未标注的老记录。
 * 离线激活（M_ACTIVATE）与改名（M_RENAME）**留着** —— 它们不是这次登录的产物，
 * 退掉登录不该顺手把账号打回未激活，也不该把用户改过的名字弹回去。 */
#define SAFETY_ORIGIN_M_SIGNOUT_ONLY \
  (SAFETY_ORIGIN_M_NONE | SAFETY_ORIGIN_M_SIGNIN)

typedef struct {
  uint32_t ent;   /* 槽 1 的绝对 entity 编号；写别的槽时由调用方加偏移 */
  int      type;  /* SAFETY_INT / SAFETY_STR / SAFETY_BIN */
  int      size;  /* 声明长度；INT 固定 4 */
} safety_key_t;

/* 建目录并确认可写。返回 0 可用；-1 不可用——调用方必须因此拒绝执行任何写操作。 */
int safety_init(void);

/* 声明"接下来这批写入是谁发起的"。之后 safety_backup_reg() 记下的每条原值都会
 * 带上这个归属，登出时才能只回滚自己那部分。0 = 无归属（只有全量还原管它）。
 * 每个动作入口调用一次，不要在中间改。 */
void safety_set_scope(uint32_t uid);

/* 声明"接下来这批写入属于哪一类动作"（SAFETY_ORIGIN_*）。和 scope 一样是
 * 每个动作入口设一次；不设就是 SAFETY_ORIGIN_NONE。 */
void safety_set_origin(int origin);

/* 把 n 个键的原值记进备份日志（已记过的 entity 不重复记，保留最早的原值）。
 * off 是槽偏移（0 = 槽 1）。返回成功记录的条数，<0 出错。 */
int safety_backup_reg(const safety_key_t *keys, int n, uint32_t off);

/* 三个受控写入入口：没有备份记录就拒绝。返回 sceRegMgr 的错误码，-EPERM 语义给拒绝。 */
int safety_set_int(uint32_t ent, int32_t v);
int safety_set_str(uint32_t ent, const char *s, int size);
int safety_set_bin(uint32_t ent, const void *p, int size);

/* 有没有未还原的注册表写入。0 = 干净，>0 = 脏，<0 = 读不到日志。 */
int safety_reg_dirty(void);

/* 某个 uid 名下还欠着多少条未还原的记录（只读）。mask 见 SAFETY_ORIGIN_M_*。 */
int safety_reg_count_scope(uint32_t uid);
int safety_reg_count_scope_mask(uint32_t uid, uint32_t origin_mask);

/* 备份记录清单，给状态接口用。safety_reg_count() 给总数；safety_reg_at() 按
 * 下标取第 i 条（越界返回 -1）。scope / origin 传 NULL 表示不关心。 */
int safety_reg_count(void);
int safety_reg_at(int i, uint32_t *ent, int *type, int *size, int *len,
                  uint32_t *scope, int *origin);

/* 只回滚**指定 uid 名下**的记录，其余原样留在日志里（blob 不动）。
 * 这是"完全还原"的语义：退掉 A 不该把 B 攒的原值一起抹掉。
 * 返回成功回滚的条数；有任何一条比对不过则返回负的失败条数，成功的已从日志
 * 里摘掉、失败的留着供重试。 */
int safety_restore_scope(uint32_t uid);

/* 同上，但再按**来源**筛一层。safety_restore_scope_mask(uid, SAFETY_ORIGIN_M_SIGNOUT_ONLY)
 * 就是"仅登出"：退掉登录态，把离线激活与改名留着。 */
int safety_restore_scope_mask(uint32_t uid, uint32_t origin_mask);


/* 逆序写回**所有**原值（不分归属），逐条回读比对。
 * 返回还原成功的条数；有任何一条比对不过则保留失败那些并返回负数。 */
int safety_restore_reg(void);

/* 系统文件写入前的存在性登记：存在则整份复制到备份目录，不存在记成 ABSENT。
 * 必须在 open(O_TRUNC) 之前调用。 */
int safety_backup_file(const char *path);

/* 该路径在本次实验前是否本来就不存在（即"我们自造的"）。1 是，0 否，-1 未知。 */
int safety_file_was_absent(const char *path);

/* 从备份还原一个已登记的文件。ABSENT 的会删除（只删自造的）。 */
int safety_restore_file(const char *path);

/* 文件层是否脏。 */
int safety_file_dirty(void);

/* files.idx 里登记过的去重路径数。 */
int safety_file_count(void);

/* 还原 files.idx 里登记过的每一条路径（EXIST 回灌 blob，ABSENT 删自造文件）。
 * 有任何一路失败就保留日志，让人能重试。返回成功的路数，失败返回负条数。 */
int safety_restore_all_files(void);

/* 一行人类可读的状态，给 web UI 的顶部横幅用。 */
void safety_status_line(char *out, size_t cap);
