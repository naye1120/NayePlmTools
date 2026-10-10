#ifndef GAME_SAVES_H
#define GAME_SAVES_H

/* [CN-FORK] v0.5.2.116 游戏管理 → 存档后端
 * 契约定死在 frontend/src/utils/gameApi.js 文件头（那一轮「只做前端 + mock」）：
 *
 *   GET  /game_saves?tid=        → {tid, local_path, auto, saves:[...], snapshots:[...]}
 *   POST /game_save_auto         → {ok}      （存配置，密码永不回传）
 *   POST /game_save_test         → {ok, latency_ms} | 400 EHOSTUNREACH
 *   GET  /game_save_download?tid=&save= → zip 二进制
 *   POST /game_save_upload?tid=&save=   （裸 body：zip/tar/gzip/单文件）→ {ok, files, size}
 *   GET  /game_save_delete?tid=&save=   → {ok}
 *   POST /game_save_snap         → {ok, ...} （v0.5.2.140 本地快照：立即备份 / 还原 / 删除）
 *
 * ── 存档落在哪（2026-10 真机实测修正）────────────────────────────────────
 *   /user/home/<uid>/savedata_prospero/<TID>/   ← PS5 存档真正在这里
 *   /user/home/<uid>/savedata/<TID>/            ← PS4 存档位
 *   /user/home/<uid>/savedata/sce_backup2|4/    ← 系统备份槽（空的），不是游戏存档
 *
 * UID 没有公开的「当前用户」API，所以枚举 /user/home 下所有目录去找。
 * **两个目录都要扫，savedata_prospero 优先。** 本文件早期版本只找 savedata/<TID>
 * —— 真机上那个位置只有两个空的 sce_backup*，于是每个游戏都 local_path=""、
 * saves=[]，整个存档功能等于没接上。路径规则取自 garlic-savemgr（earthonion）
 * 的 scan_saves，它也是同时扫这两个目录的。
 *
 * ── 粒度：一个用户一条 ────────────────────────────────────────────────────
 * 一个 <uid>/<TID> 目录 = 一份完整存档。里面是主机自己的记账（当前 sdimg_<名>、
 * sdimg_sce_bu_<名> 备份、sdimg_sce_sdmemory 系统存档、PS4 的 .bin 密封密钥），
 * 全部一起打包/还原 —— 少一样都可能让主机认不出来，用户也不必去分辨哪个是哪个。
 *
 * API 里的 save 参数就是**用户目录名**（真机形如 "1a1d3627"），不再是「槽名」。
 * 一台主机可能有多个用户都存过同一个游戏（实测 PPSA01650 在两个用户下都有），
 * 所以列表可能不止一条：每个用户各一条，互不覆盖。老版本「取体积最大的那个」
 * 正是为了绕开多用户问题，但那是把两个用户的存档拍扁成一个，会互相盖。
 *
 * kind（PS5/PS4）由命中目录决定，不再按 tid 前缀猜。
 *
 * ── 传输格式 ──────────────────────────────────────────────────────────────
 * 下载 = 自研 STORE zip（src/zip_io.c）。上传的文件选择器 accept 是
 * ".zip,.tar,.gz,.tgz"，四种都得吃：PK 头走 zip、1f 8b 走 gunzip（之后若是 tar 再解 tar，
 * 否则当单文件收下）、ustar 头直接解 tar、都不是就整包存成一个文件。
 * 只认 zip 的话，用户在 Windows 上右键压的 tar.gz 就传不进来。
 *
 * ── 本地快照（v0.5.2.140）────────────────────────────────────────────────
 * 远程同步（SMB/FTP/HTTP）要网络、要对面在线，是「多机之间搬存档」的活；
 * 本地快照是**不依赖任何外部条件**的兜底：守护线程按每款游戏的间隔，把
 *   <GS_HOME>/<uid>/savedata_prospero|savedata/<TID>/
 * 整份复制到
 *   GAME_SAVES_SNAP_DIR/<TID>/<uid>/<YYYYmmdd-HHMMSS>/
 * 每个 <TID>/<uid> 只留最近 keep 份（超了删最旧），另有一道全局字节预算兜底。
 *
 * 配置跟着**游戏**走（存在 GAME_SAVES_AUTO_PATH 的同一个 tid 对象里，键名 "snap"），
 * 和远程那套并排：用户「对着这个游戏一起配」的心智模型不变。
 *
 * 三个刻意为之的设计：
 *   ① **内容没变就不新增**。比 (files, bytes, 最大 mtime) 三元组 —— 主机的存档
 *      绝大部分时间是同一份，按 30 分钟一拷的话，一天下来 48 份一模一样的东西。
 *      三元组一样就直接跳过，省掉的是用户的 /data 空间。
 *   ② **源目录最近被写过就跳过本轮**。存档正写到一半时拷出来是撕裂的，还原回去
 *      可能让主机认不出来。宁可等下一轮。
 *   ③ 复制到临时目录、**成功才 rename 成时间戳目录**。半截的快照永远不出现。
 *
 * ── 栈预算（v0.5.2.141）──────────────────────────────────────────────────
 * 这条路径上的**大块内存一律走堆**，一个都不能留在栈上。原因见 src/game_saves.c
 * 里 gs_cp_t 那段：真机 MHD_USE_THREAD_PER_CONNECTION 的连接线程只有 64KB 栈
 * （宿主上量出来的临界点是 56KB），`.140` 的 `char buf[32*1024]` 加几个
 * gs_root_t[16] 数组叠起来就把面板打死了。
 * 约束由两处守着：test/game_saves_test.c 的 ⑩（32KB 线程栈跑完整条链）与
 * tools/check_stack_frames.sh（任何函数栈帧不得超 32KB）。
 */

#include <stddef.h>

/* 自动备份配置的持久化位置（和 autoload/quicklinks 一样放 /data 下的自有目录）。 */
#ifndef GAME_SAVES_AUTO_PATH
#define GAME_SAVES_AUTO_PATH "/data/nayeplmtools/saves_auto.json"
#endif

/* 下载时临时 zip 的落点。**不能放在存档目录里** —— 边下边把自己的源文件夹写脏了。 */
#ifndef GAME_SAVES_PACK_DIR
#define GAME_SAVES_PACK_DIR "/data/nayeplmtools/saves_packs"
#endif

/* 一次存档能装多少个文件 / 多大：防一个畸形包把主机拖死。 */
#define GAME_SAVES_MAX_ENTRIES 8192u
#define GAME_SAVES_MAX_BYTES (512ull << 20)
#define GAME_SAVES_MAX_TAR_FILE (256ull << 20)

/* ── 本地快照的落点与限额（v0.5.2.140）───────────────────────────────────── */
#ifndef GAME_SAVES_SNAP_DIR
#define GAME_SAVES_SNAP_DIR "/data/nayeplmtools/saves_snap"
#endif

/* 单份快照的上限：与「一次存档能装多少」同一量级。真机实测最大的存档
 * （PPSA21567）是 20 MB / 2 文件，512 MB 是给极端情况留的余量。 */
#define GAME_SAVES_SNAP_MAX_ENTRIES GAME_SAVES_MAX_ENTRIES
#define GAME_SAVES_SNAP_MAX_BYTES GAME_SAVES_MAX_BYTES
/* 全部快照的总预算。超了从**全局最旧**的那份开始删 —— 只按每款游戏的 keep
 * 裁剪的话，15 款 × 5 份 × 18 MB 就是 1.3 GB，主机 /data 顶不住。 */
#define GAME_SAVES_SNAP_TOTAL_BYTES (4ull << 30)

/* 间隔下限 1 分钟（再密就是拿 IO 换没意义的东西），默认 30 分钟；
 * 保留份数夹在 1~20。三者都会被 gs_snap_clamp_* 夹一遍再落盘。 */
#define GAME_SAVES_SNAP_INTERVAL_MIN 60
#define GAME_SAVES_SNAP_INTERVAL_MAX 86400
#define GAME_SAVES_SNAP_INTERVAL_DEF 1800
#define GAME_SAVES_SNAP_KEEP_MIN 1
#define GAME_SAVES_SNAP_KEEP_MAX 20
#define GAME_SAVES_SNAP_KEEP_DEF 5

/* 找这个 tid 的**第一个**存档根（<uid>/savedata_prospero|savedata/<TID>）。
 * 返回 0 有（out 已填），-1 找不到（主机上一个存档都还没生成时是正常的）。
 * 只想知道「存档在哪」的调用方用这个；要枚举全部用户请走列表接口。 */
int game_saves_root(const char *tid, char *out, size_t osz);

/* GET /game_saves：整份响应体（含 auto 与 saves）写进 buf。返回长度，<0 失败。
 * saves[] 里每条 = 一个存过这个游戏的用户：{id, name, kind, mtime, size, files}，
 * id 与 name 都是 uid（前端自己加「用户」前缀），kind 是 "PS5"/"PS4"。
 * auto 里多一个 snap 子对象 {enabled, interval, keep}（本地快照配置）。
 * snapshots[] 里每条 = 一份已存在的本地快照：{uid, ts, mtime, size, files}。 */
int game_saves_list_json(const char *tid, char *buf, size_t size);

/* /game_detail 要的存档条数 = 存过这个游戏的用户数（通常是 0 或 1）。
 * 不再数里面的 sdimg 个数 —— 那些是主机自己的记账（当前 + _bu_ 备份 + sdmemory），
 * 数出来只会让人以为有 4 份存档。 */
int game_saves_count(const char *tid);

/* POST /game_save_auto：body 是 JSON。0 成功（msg 给人话），-1 参数非法。 */
int game_saves_auto_set(const char *tid, const char *body, size_t len,
                        char *msg, size_t msgsz);

/* POST /game_save_test：body 是 JSON，**真的去连一次**（TCP connect，
 * ftp 还看 220 横幅、http 再发一个 HEAD 看状态行）。
 * 0 → buf 写 {"ok":true,"latency_ms":N,"note":"…"}；-1 → 调用方回 400 EHOSTUNREACH。 */
int game_saves_test(const char *body, size_t len, char *buf, size_t size);

/* GET /game_save_download：把 <uid>/<TID>/ 整份打成 STORE zip 写到 out_path。
 * save = uid；zip 内的顶层目录用 tid。0 成功（调用方负责流完之后 unlink）。
 * out_files 回实际打进 zip 的文件数（可为 NULL）。 */
int game_saves_pack(const char *tid, const char *save, const char *out_path,
                    long long *out_files, char *msg, size_t msgsz);

/* [CN-FORK] v0.5.2.145 GET /game_save_remote_info：取某用户远程存档的只读元数据
 * （大小 / 文件数 / 修改时间），供前端「远程游戏」标签展示。返回写入长度，<0 出错。
 * 配置解析全局优先 → per-game 回落。输出 JSON 见 game_saves.c 注释。 */
int game_saves_remote_info(const char *tid, const char *save, char *buf, size_t size);

/* [CN-FORK] v0.5.2.159 POST /game_save_remote_delete：删掉**远端**那一份存档。
 * 配置解析与 game_saves_remote_info 同一条口径（全局优先 → per-game 回落），
 * 传输层在 sr_delete（**只有 SMB 真删**，FTP/HTTP 回一句写明协议的人话）。
 * 主机上的本地存档与本地快照一个字节都不碰。
 * 0 成功（含「远端本来就没有」）；-1 失败，msg 里是人话。 */
int game_saves_remote_delete(const char *tid, const char *save, char *msg, size_t msgsz);

/* POST /game_save_upload：body 是用户挑的那个文件原样字节，save = uid
 * （空 = 落在第一个用户下）。**整份替换**：旧的先改名留一手，换成功才删。
 * 成功 0，*out_files = 写入的文件数，*out_size = 写入的总字节。 */
int game_saves_upload(const char *tid, const char *save,
                      const unsigned char *body, size_t len,
                      long long *out_files, long long *out_size,
                      char *msg, size_t msgsz);

/* [CN-FORK] v0.5.2.142 从**磁盘上的一个归档文件**整份导入。
 * 与 game_saves_upload 是同一份实现（同一条「旧的先留一手」的回退逻辑），
 * 只是来源不同 —— 自动同步的「拉取」下载完落到临时文件后走这里。 */
int game_saves_import_file(const char *tid, const char *save, const char *path,
                           long long *out_files, long long *out_size,
                           char *msg, size_t msgsz);

/* [CN-FORK] v0.5.2.142 这个游戏在主机上的全部用户标识（每个用户一条）。
 * 自动同步要**逐份**处理 —— 一台主机可能有多个用户都存过同一个游戏
 * （真机实测 PPSA01650 有两个），只处理第一个等于另一个永远不同步。
 * 返回条数（0 = 主机上还没有这个游戏的存档）。 */
int game_saves_uids(const char *tid, char out[][64], int max);

/* [CN-FORK] v0.5.2.142 主机上**全部用户目录名**（不限于有这款游戏存档的）。
 *
 * 为什么拉取需要它：`game_saves_uids` 是靠「<uid>/savedata_prospero/<TID> 这个
 * 目录在不在」来枚举的 —— 而「拉取」要救的恰恰是**本地那份已经没了**的情况
 * （换机、误删、重装）。那时候本地枚举必然是 0 条，用 game_saves_uids 就永远
 * 拉不回来，成了一条死路。用户目录本身是主机账号，不会因为删了游戏存档而消失，
 * 所以拿它当 uid 的候选集：逐个去远端试 <uid>.zip，远端没有的算「跳过」而不是
 * 「失败」（见 ga_do）。代价是主机上用户多时会多几次 404，但那是本地回环/局域网的
 * 一次往返，和「救不回存档」不是一个量级。
 *
 * 返回条数；名字已经过 gs_uid_ok 过滤，可以直接拼路径。 */
int game_saves_user_ids(char out[][64], int max);

/* [CN-FORK] v0.5.2.142 读一款游戏的自动备份配置（原始 JSON，**含明文密钥**）。
 * 给自动存档引擎解析用；不进任何响应体（对外那份只吐 has_password / token_set）。
 * 返回写入长度，<0 = 没有这款游戏的配置。 */
int game_saves_auto_json(const char *tid, char *buf, size_t size);

/* [CN-FORK] v0.5.2.142 注入「自动存档运行期状态」的提供者（由 game_auto 注册）。
 * 绕这一层是为了不让 game_saves 依赖 game_auto —— 后者要 curl 与 sceLncUtil，
 * 而 game_saves 的宿主测试只想要一个纯文件系统的关卡（不链 curl）。
 * 没注册时 /game_saves 里的 auto_state 就是 {}，列表照常可用。 */
typedef int (*gs_state_fn)(const char *tid, char *buf, size_t size);
void game_saves_set_state_provider(gs_state_fn fn);

/* GET /game_save_delete：删掉这个用户在这个游戏上的整份存档。0 成功。 */
int game_saves_remove(const char *tid, const char *save, char *msg, size_t msgsz);

/* ── 本地快照（v0.5.2.140 / 触发方式于 v0.5.2.154 改为「游戏退出时」）──────────
 * [CN-FORK] v0.5.2.154 **定时守护整条删除**（用户口径：「每次游戏结束后，在本地
 * 快照存一份存档，本地自动存储快照删除（定时的功能不要了）」）。触发点改到
 * game_auto.c 的扫描线程 —— 检测到某款游戏退出时调 game_saves_snap_run_now。
 * 于是这里不再有 start_daemon，也不再需要「按每款游戏各自的间隔判定到期」。 */

/* 立即给这个游戏做一份快照（游戏退出时自动触发 / 用户点「立即备份」）。
 * 返回已生成/跳过的份数（>=0），msg 回一句人话；<0 是参数或目录层面的硬错误。 */
int game_saves_snap_run_now(const char *tid, char *msg, size_t msgsz);

/* [CN-FORK] v0.5.2.154 这个游戏的本地快照开着没有（全局优先，与内部 gs_snap_cfg 同口径）。
 * 给 game_auto.c 的扫描线程用：游戏退出时先问一句，开着才备份。 */
int game_saves_snap_enabled(const char *tid);

/* POST /game_save_snap：body 是 JSON
 *   {tid, action:"run"}                       → 立即备份
 *   {tid, action:"restore", uid, ts}          → 把那份快照整份还原回主机
 *   {tid, action:"delete",  uid, ts}          → 删掉那份快照
 * 0 成功（buf 已填完整 JSON 响应体），-1 参数非法（调用方回 400）。 */
int game_saves_snap_action(const char *body, size_t len,
                           char *buf, size_t size, char *msg, size_t msgsz);

/* ── [CN-FORK] v0.5.2.154 ────────────────────────────────────────────────── */

/* 这个用户在这个游戏上的存档目录「最近被写过的时刻」（所有普通文件的最大 mtime）。
 * 给「拉取前先比新旧」用（判定是纯函数 game_auto.h 的 ga_pull_needed）。
 * 返回 1 = 本地有存档（*out_mtime 已填，unix 秒）；0 = 本地没有（换机/误删/从没存过）。 */
int game_saves_local_mtime(const char *tid, const char *uid, long long *out_mtime);

/* POST /game_save_import_garlic：导入 garlic-savemgr（earthonion）导出的**加密**存档。
 *   query: tid, save(=uid，可空 = 第一个用户), name(=原始文件名，裸镜像时必需)
 *   body : 裸的 `sdimg_<名>` 镜像，或 garlic 的 PS4 包（zip 内 `sdimg_<名>` + `<名>.bin`）
 *
 * 与 game_saves_upload 的语义差别是刻意的：上传是**整份替换**，这里是**逐文件合并**
 * —— 只覆盖同名的镜像，保留主机自己的记账（sdimg_sce_bu_* / sdimg_sce_sdmemory）。
 * 详细理由见 src/game_saves.c 里那段注释。
 * 0 成功（*out_files 回写入的文件数），-1 失败（msg 回人话）。 */
int game_saves_import_garlic(const char *tid, const char *save, const char *name,
                             const unsigned char *body, size_t len,
                             long long *out_files, char *msg, size_t msgsz);

/* [CN-FORK] v0.5.2.156 从 garlic 导出的文件名里认游戏（`<TID>_<存档目录名>` 那一段）。
 * 返回 1 = 认出来了（*out 写 9 位**大写** TID，缓冲至少 10 字节）；0 = 认不出来。
 *
 * 服务于「存档导入要有游戏判定，不是当前游戏的不让导入」：**认出来且不等于当前 tid
 * 才拒绝，认不出来一律放行** —— 用户可能把文件改过名，把「说不清」当「不匹配」
 * 会让正常导入整个失效。导出给宿主测试穷举边界（`test/game_saves_test.c` ⑬）。 */
int game_saves_tid_from_name(const char *s, char *out, size_t osz);

#endif
