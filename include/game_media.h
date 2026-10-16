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
 *   录屏   /user/av_contents/video/<生成器>/<TID>/<3位hex>/<时间戳_随机>.webm
 *   缩略图 …/thumbnails/video/<同一链路>/<原名>.webm.jxr
 *   —— 录屏链路已实测与 photo **同构**（`.125` 真机取证）。但录屏缩略图是
 *      **JPEG XR**（头 `49 49 BC 01`），stb_image 解不了，所以视频封面**不走本模块**，
 *      改由前端 `<video preload="metadata">` 从 webm 取首帧（见 §7a 与 HANDOVER §7）。
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
 * §7 真机取证结果（`.125` 走 FTP `192.168.1.214:2121` 实测，别再当成待验证）：
 *   a. **已结案：录屏目录层级与 photo 同构。** /video/<生成器>/<TID>/<3位hex>/<时间戳_随机>.webm，
 *      缩略图在 /thumbnails/video/<同一链路>/<原名>.webm.jxr（`icon_path` 也直接指向它）。
 *      当年猜错的方向**不是「路径」而是「格式」**：录屏封面是 **JPEG XR**（头 49 49 BC 01，
 *      1920×1080 24bppRGB），stb_image 解不了 —— 这才是三轮封面都只能回占位图的原因；
 *      照片封面是 `.png.jpeg`（普通 JPEG），所以只有录屏这一路坏。
 *   b. **已结案：`duration` 是整秒、`duration_ms` 是毫秒。** 实测两条录屏分别是
 *      duration=7 / duration_ms=7123、duration=14 / duration_ms=14531。本模块按
 *      `duration_ms > 0 ? /1000 : duration` 取值，**是对的**。
 *   c. **仍未验证**：只删文件后，媒体库再次打开是否会出现幽灵条目
 *      （tbl_PhotoVideo 有 file_status 列，是否自动对账未知）。
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
    char  name[256];    /* 显示名：游戏名称-截图时间（没改过名则是文件名） */
    char  dlname[300];  /* **下载**用的文件名：游戏名称-YYYYMMDD_HHMMSS.<ext>
                         *  与 name 刻意不同 —— 见 src/game_media.c 的 md_dlname：
                         *  文件名里不能有空格与冒号，而显示名要给人看。 */
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

/* [CN-FORK] v0.6.0.177 遍历某个 tid 下的全部媒体条目（与 /game_media 列表同一条
 * md_scan_locked，只是回调换成调用方的 —— 远程同步要的是「文件路径 + 大小」，
 * 而不是几百 KB 再解析回去的 JSON，理由与 game_media_count 一样）。
 * cb 返回 0 继续、非 0 立刻中止（外部想停就停，别为一个「够了」扫到底）。
 * 返回 0 = 扫完；-1 = 参数非法 / 媒体库读不了。回调在媒体库的锁里被调用，
 * **回调内不要再去调本模块的其它接口**（会自锁），做纯计算与传输即可。 */
typedef int (*game_media_item_cb)(const game_media_item_t *it, void *user);
int game_media_each(const char *tid, game_media_item_cb cb, void *user);

/* 缩略图：按 want_w 缩放并缓存到磁盘。返回 0 成功（*out 需 free），
 * -1 = id 非法，-2 = 找不到或解不出图（HDR .jxr，浏览器也显示不了 → 404）。 */
int game_media_thumb(const char *id, int want_w, unsigned char **out,
                     size_t *out_len, const char **mime);

/* 原图/原视频：不读进内存，fd 交给 MHD（它会替我们 close）。返回 0 成功；
 * -1 = id 非法，-2 = 文件不存在。mime 为静态字符串。 */
int game_media_open(const char *id, int *out_fd, off_t *out_size, const char **mime);

/* [CN-FORK] v0.5.2.122 下载文件名（「游戏名称-YYYYMMDD_HHMMSS.<ext>」）。
 * 给 /game_media_file?dl=1 下 Content-Disposition 用 —— 前端 <a download> 的
 * download 属性在旧 WebKit 上不一定被认，服务端把名字一并给出去才稳。
 * tid 用来取游戏名（改过名就用新名）；取不到就退回原文件名。
 * 0 成功；-1 = 参数非法 / 文件不在。 */
int game_media_download_name(const char *tid, const char *id, char *out, size_t osz);

/* 删除：原图 + 缩略图 + .meta + .dat + 本地缩略图缓存。0 成功。
 * 只动文件不动系统库（见头注释铁律①），msg 回一句给人看的话。 */
int game_media_delete(const char *tid, const char *id, char *msg, size_t msgsz);

/* ── 打包任务（单 worker，与删除/安装任务同构：只有一个在跑） ───────────── */
/* [CN-FORK] v0.5.2.126 压缩包内的布局（用户口径：「压缩包下级文件夹是 游戏编码-游戏名称，
 * 这个文件夹里面直接就是截图和视频」）：
 *
 *   PPSA21567-宇宙机器人/
 *     ├─ 宇宙机器人-20261006_125910.png
 *     ├─ 宇宙机器人-20261006_130015.webm
 *     └─ …
 *
 * **只有一层目录**，截图/录屏直接躺在里面 —— 不再复刻
 * /av_contents/photo/<生成器>/<TID>/<hex>/ 那套磁盘层级（那是实现细节，对用户是噪音，
 * 还会把同一批图拆进不同目录）。文件名那一段与「单独下载」那份 dlname **同源**
 * （同一个 md_dlname），压缩包里外两套名字只会让人对不上号。
 *
 * 两条必须守的性质：
 *   ① 文件夹名要按**文件名规则**清一遍（游戏名里带冒号是常态：「蜘蛛侠：迈尔斯」），
 *      并按 UTF-8 边界兜长度上限 —— 不能把一个汉字劈成半个，也不能撑爆
 *      zip_ent_t.zname[512]。
 *   ② **必须去重**。下载名只精确到秒，原文件名里那截随机数被丢掉了，所以同一秒连拍
 *      两张必然同名；而 zip 里两条同名条目在 Windows 资源管理器里解压是**互相覆盖**
 *      ——那是静默丢文件，比名字难看严重得多。撞了就补 -2 / -3 …。
 *   条目名带 UTF-8 标志位（general purpose bit 11，见 src/zip_io.c），
 *   否则中文目录名在 Windows 上是乱码。
 */
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

/* [CN-FORK] v0.5.2.122 成品 zip 的**文件名**（含 .zip），给 Content-Disposition 用。
 * 名字在 md_pack_start 里就定好了（游戏名称-打包时间戳.zip），这里只是把 aux 的
 * basename 取出来 —— 两边各算一次必然漂移，所以只有一处算。0 成功，-1 没这个任务。 */
int game_media_pack_name(long long job_id, char *out, size_t osz);

#endif
