#ifndef GAME_MEDIA_H
#define GAME_MEDIA_H

/* [CN-FORK] v0.5.2.115 游戏管理 → 多媒体管理（截图 / 录屏）后端
 *
 * 前端契约定死在 frontend/src/utils/gameApi.js 的文件头（那一轮「只做前端 + mock」），
 * 本模块按同一份契约实现，六条端点 + 两条任务端点：
 *
 *   GET  /game_media?tid=              → {tid, count, items:[…], truncated}
 *   GET  /game_media_thumb?id=&w=320   → 图片（后端按需缩放 + 落盘缓存）
 *   GET  /game_media_file?id=          → 二进制（原图 / 原视频，fd 直出不进内存）
 *   POST /game_media_pack  {tid, ids}  → {job_id}
 *   GET  /game_media_pack_get?job=     → 二进制（zip，fd 直出）
 *   GET  /game_media_delete?tid=&id=   → {ok}
 *   GET  /game_job_status?job=         → {job_id, active, phase, percent, …}
 *   GET  /game_job_cancel?job=         → {ok}
 *
 * ── 真机取证：截图 / 录屏到底存在哪（2026-10-06 FTP 实测，别再靠猜）──
 *   原图   /user/av_contents/photo/<生成器>/<TID>/<3位hex>/<时间戳_随机>.<ext>
 *   缩略图 /user/av_contents/thumbnails/photo/<同上链路>/<原名>.<原ext>.<jxr|jpeg>
 *   副文件 同目录 <原名>.meta（JSON）与 <原名>.dat（1024B 二进制）
 *   录屏在 /user/av_contents/video/… 下同构（实机当时只有截图，见 §7 待验证）
 *
 * ── 为什么以 av_content.db 为准而不是扫目录 ──
 *   `/system_data/priv/mms/av_content.db` 的 tbl_PhotoVideo 一次给出
 *   content_path / icon_path / width / height / duration(_ms) / content_type / title，
 *   而 width/height 对 **HDR 截图（.jxr）** 从文件头根本读不出来（仓库里没有 JXR 解码器，
 *   stb_image 不支持）。同目录的 app.db 已经是本仓库读熟的只读对象，读法一致。
 *   库是**回滚日志模式**（file format write/read version = 1，非 WAL）、页 4096、UTF-8、
 *   整库 ~180 KB —— 整读一份进内存是安全的，不会读到半截事务。
 *
 * ── 两条铁律 ──
 *   ① **只读，绝不写 av_content.db**。SDK 里没有 sqlite3，仓库那套极简 SQLite 解析器
 *      只会读 b-tree；手写 b-tree 删除等于拿用户全部截图去赌系统库不被写坏。
 *      所以「删除」只删文件（原图 + 缩略图 + 两个副文件），库里的行留给媒体库自己清
 *      （它有 file_status 列，是否自动对账未验证，见 §7）。这条偏离已写进 HANDOVER。
 *   ② **绝不轮询**。列表是「打开弹窗拉一次 + 手动刷新」，缩略图靠落盘缓存，
 *      任务进度 1s 一次（gameApi.js 的 watchJob）。
 *
 * §7 待验证（下一轮拿真机取证，别当成已知事实）：
 *   a. 录屏目录层级是否与 photo 同构（/video/<生成器>/<TID>/<hex>/…）；
 *   b. 视频时长读 duration 还是 duration_ms（单位是秒还是毫秒）—— 实机还没有录屏；
 *   c. 只删文件后，媒体库再次打开是否会出现幽灵条目。
 */

#include <stddef.h>
#include <sys/types.h>

/* 五个路径全部可被 -D 覆写 —— 主机单测（test/media_scan_test.c）把它们指到 /tmp，
 * 与 test/run_host_tests.sh 里 -DSAFETY_DIR 那条同一个手法。别去掉 #ifndef。 */
/* 媒体根（相对它就是 content_path，/user 是它的前缀） */
#ifndef GAME_MEDIA_ROOT
#define GAME_MEDIA_ROOT "/user/av_contents"
#endif
/* 系统媒体库（只读） */
#ifndef GAME_MEDIA_DB_PATH
#define GAME_MEDIA_DB_PATH "/system_data/priv/mms/av_content.db"
#endif
/* 我们自己的落盘缓存与打包产物 */
#ifndef GAME_MEDIA_THUMB_DIR
#define GAME_MEDIA_THUMB_DIR "/data/nayeplmtools/media_thumbs"
#endif
#ifndef GAME_MEDIA_PACK_DIR
#define GAME_MEDIA_PACK_DIR "/data/nayeplmtools/media_packs"
#endif
/* content_path 的前缀：id → 绝对路径时拼上它 */
#ifndef GAME_MEDIA_USER_PREFIX
#define GAME_MEDIA_USER_PREFIX "/user"
#endif

/* 单条媒体。id 直接用 content_path（见下方 md_path_ok 的白名单校验）。 */
typedef struct {
    char  content[640]; /* /av_contents/photo/…  */
    char  icon[640];    /* /av_contents/thumbnails/photo/…，可为空 */
    char  name[256];    /* basename，用于标题与删除确认文案 */
    char  stamp[32];    /* 本地时区 "YYYY-MM-DD HH:MM"，原样给前端 */
    long long size;     /* stat 出来的实际字节数 */
    int   w, h;
    int   video;        /* 1=录屏 0=截图 */
    double duration;    /* 秒 */
} game_media_item_t;

/* 列表。成功返回写入长度（≥0），打不开库/参数非法返回 -1（调用方回 500）。 */
int game_media_list_json(const char *tid, char *buf, size_t size);

/* [CN-FORK] v0.5.2.116 /game_detail 的 media_count：只数不拼 JSON。
 * tid 不合法 / 库里没有这个游戏 → 0（不是错误，详情里那一格显示 0 即可）。 */
int game_media_count(const char *tid);

/* 按 id（content_path）取一条；找不到返回 -1。items_json 之外的路径解析都走它。 */
int game_media_lookup(const char *id, game_media_item_t *out);

/* 缩略图：按 want_w 缩放并缓存到磁盘。返回 0 成功（*out 需 free），
 * -1 = id 非法，-2 = 找不到或解不出图（HDR .jxr，浏览器也显示不了 → 404）。 */
int game_media_thumb(const char *id, int want_w, unsigned char **out,
                     size_t *out_len, const char **mime);

/* 原图/原视频：不读进内存，fd 交给 MHD（它会替我们 close）。返回 0 成功；
 * -1 = id 非法，-2 = 文件不存在。mime 为静态字符串。 */
int game_media_open(const char *id, int *out_fd, off_t *out_size, const char **mime);

/* 删除：原图 + 缩略图 + .meta + .dat + 本地缩略图缓存。0 成功。
 * 只动文件不动系统库（见头注释铁律①），msg 回一句给人看的话。 */
int game_media_delete(const char *tid, const char *id, char *msg, size_t msgsz);

/* ── 打包任务（单 worker，与删除/安装任务同构：只有一个在跑） ───────────── */
/* POST /game_media_pack 的请求体入口：{tid, ids:[content_path,…]|null}。
 * ids 缺省 / null / 空数组 = 全量（该 tid 下全部媒体）。
 * 0 成功并写出 job_id；-1 = 已有任务在跑（或分配失败）；-2 = 请求体不合法。
 * 收字符串 id 而不是 rowid —— 见头注释「id = content_path」那条。 */
int game_media_pack_start_json(const char *body, size_t len,
                               long long *job_id, char *msg, size_t msgsz);
/* 状态 JSON 写入 buf，返回长度；job 不存在返回 -1。 */
int game_media_pack_status(long long job_id, char *buf, size_t size);
/* 请求取消。0=已受理（或本来就没在跑），-1=job 不存在。 */
int game_media_pack_cancel(long long job_id);
/* 打开成品 zip 交给 MHD。0 成功，-1 job 不符，-2 文件不可读。 */
int game_media_pack_open(long long job_id, int *out_fd, off_t *out_size);

#endif
