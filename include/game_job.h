#ifndef GAME_JOB_H
#define GAME_JOB_H

/* [CN-FORK] v0.5.2.116 游戏管理板块的**后台任务槽**（全局单槽，两种任务共用）
 *
 * 抽出来的理由：`.115` 这个槽长在 game_media.c 里，而游戏删除（/game_delete）也必须
 * 走同一条 `/game_job_status` / `/game_job_cancel`（前端只有这两个端点，gameApi.js
 * 明写「后端只有**一个**后台 worker，与 ShadowMountPlus 同构」）。把槽搬进独立模块，
 * 打包与删除各自填自己的字段，谁也不用去 include 谁。
 *
 * 一个槽 = 同一时刻只能有一个任务，这是**契约**不是偷懒：
 *   · 第二个任务必须被拒绝（409 EBUSY），否则两个 worker 抢同一个槽，进度互相踩；
 *   · 前端 1s 轮询一次 `/game_job_status`，两个任务就要两份状态，槽里得带 kind
 *     才能分清给谁 —— 于是有了 gj_status_json 与 kind 一起工作的那套约定。
 *
 * 字段尺寸比 `.115` 的原版放大过：error/reason/phase 三段都要 JSON 转义后写进同一个
 * 缓冲，最坏情况已经贴近 1000 字节，状态缓冲是 2048（见 http_server.c 那条注释）。
 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define GJ_KIND_PACK   0 /* 打包（媒体 zip） */
#define GJ_KIND_DELETE 1 /* 删除游戏源 */

typedef struct {
    long long id;
    int kind;
    int active, done, ok, cancel;
    char phase[40];    /* idle|preparing|measuring|packing|deleting|finalizing|done|failed|cancelled */
    char error[560];   /* 给人看的失败原因（已转义后才进 JSON） */
    char reason[160];  /* 机器码：failed/cancelled/… */
    long long bytes_done, bytes_total;
    long long files_done, files_total;
    time_t started;
    char aux[700];     /* PACK: 产出 zip 路径；DELETE: 要删的源路径 */
    char tid[32];
} gj_state_t;

/* ── 槽的互斥访问 ─────────────────────────────────────────────────────────
 * gj_state() 返回槽本体，**调用方必须已经持锁**。这是刻意的「细粒度锁」：
 * 读状态 JSON 时要连续读十几个字段，锁里包一次比每字段取一次少得多的往返。 */
void gj_lock(void);
void gj_unlock(void);
gj_state_t *gj_state(void);

/* 领一个空闲槽并初始化。0 = 成功，**返回时仍持锁**，调用方填完字段后必须 gj_unlock()；
 * -1 = 已有任务在跑（或自旋分配 id 失败）。msg 一律回一句人话。 */
int gj_begin(int kind, long long *out_id, char *msg, size_t msgsz);

/* 收尾用（都自带锁） */
void gj_set_phase(const char *ph);
void gj_fail(const char *reason, const char *msg);
void gj_ok_done(void);
void gj_set_totals(long long bytes, long long files);
/* 只写 error（不动 phase / reason / ok / done）。用来在**成功收尾之后**补一条
 * 「完成了，但有几个文件不见了」这类**成功但有保留**的提示 —— 顺序必须是
 * gj_ok_done() 之后再调它：原版把这两步写反了，ok_done 会把 error 清成空串，
 * 于是那句提醒从来没显示出来过。 */
void gj_set_error(const char *msg);
int  gj_cancelled(void);
void gj_add_bytes(long long n);
void gj_add_file(void);

/* /game_job_status 的响应体。0..n = 写入长度，-1 = 没有这个任务（前端据此 404）。 */
int gj_status_json(long long id, char *buf, size_t size);

/* /game_job_cancel：0 = 已受理（或本来就没在跑），-1 = 任务不存在。 */
int gj_cancel(long long id, char *msg, size_t msgsz);

/* 取某个 id 的 aux（zip 路径 / 删除目标），并顺带校验 kind：
 * 0 = 匹配，-1 = 任务不存在或 kind 不符。删除任务不该能靠 job id 取到打包产物。 */
int gj_aux(long long id, int kind, char *out, size_t osz);

/* 当前任务种类；没有任务返回 -1。 */
int gj_kind_now(void);

/* ── CRC32（zip 与存档都要用，抽到一处免得两份表不同步） ─────────────────── */
void     gj_crc_init(void);
uint32_t gj_crc32(uint32_t c, const unsigned char *b, size_t n);

#endif
