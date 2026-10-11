#pragma once

/* [CN-FORK] v0.5.2.163 背景音乐接口契约。落点 = /data/nayeplmtools/music/<TID>.<ext>。 */

/* ⚠ 头文件必须自包含：`size_t` 来自这里，别指望包含者先引过。
 * （`.165` 出包时踩到 —— `http_server.c` 先于本头用到 `size_t`，编译期直接
 *  「call to undeclared function」，而且报错点全在调用方，很容易看成别的问题。） */
#include <stddef.h>

/* 把 (tid) 对应的当前生效音乐路径写到 tp，mime 写到 mime。
 * 0 = 找到了；1 = 没设过（tp / mime 会被清空成空串）。 */
int game_music_path(const char *tid, char *tp, size_t tpsz,
                    char *mime, size_t mimesz);

/* 写入 / 替换音乐。bytes_out = 实际写入字节数（成功才有意义）。
 * 0 = 成功；-1 = 校验（mib 上限 / 空文件）；1 = IO 失败。msg 写人话。 */
int game_music_set(const char *tid, const char *name,
                   const unsigned char *data, size_t len,
                   long long *bytes_out, char *msg, size_t msgsz);

/* 删掉主机上已有的（不存在也算成功，调用方不需要区分）。 */
void game_music_reset(const char *tid);

/* ── [CN-FORK] v0.5.2.166 把音乐**真正写进主机** ────────────────────────────
 *
 * `.163` 那一版只把文件落在 /data/nayeplmtools/music/ —— 那是**我们自己的**目录，
 * 主机压根不读。用户看到的就是「背景音乐改动没有生效」。真正的落点是
 * 游戏包里的 `<sce_sys>/snd0.at9`（与 icon0.png / pic0.* 同一个目录，
 * 也就是 app_installer.c 装自己的磁贴时写的那三个文件旁边）。
 *
 * ── 为什么必须是 ATRAC9 ───────────────────────────────────────────────────
 * PS5 主屏磁贴聚焦时播放的选中音乐是 **ATRAC9 流装在 RIFF/WAVE 里**
 * （规格见 blackbearreloaded/ps5-at9-converter 的 docs/atrac9-profile.md：
 *  WAVE_FORMAT_EXTENSIBLE + sub-format GUID 47E142D2-36BA-4D8D-88FC-61654F8C836C，
 *  fmt 52 / fact 12 / smpl 60 / data，上限 2,097,152 字节）。
 * 主机上没有任何 ATRAC9 编码器（Sony 的 at9tool 是私有二进制、ffmpeg 只有解码器），
 * 所以编码放在**浏览器**里做（frontend/src/utils/atrac9/），后端只负责搬文件。
 *
 * 因此这个入口**只接已经编码好的 .at9**：不是 ATRAC9 的一律拒收（-1），
 * 免得又出现「面板说成功了、主机放不出来」这种假成功。
 *
 * 顺带做两件小事（都是 best-effort，失败只记日志不影响主流程）：
 *   · 第一次覆盖前把原来的 snd0.at9 备份到 <GAME_TITLE_DIR>/<TID>/orig/；
 *   · 往 <sce_sys>/param.json 的 pubtools 里写 loudnessSnd0（响度元数据）。
 *
 * 返回 0 = 已经写进主机（bytes_out 有值）；-1 = 参数/内容非法（msg 给人话）；
 *      1 = 找到了 sce_sys 但写不进去（只读镜像、权限不足）；2 = 找不到 sce_sys
 *      （这游戏还没装进 /user/app，或是个没登记的裸 dump）—— 后两种都**不算失败**，
 *      面板那一层照样生效，msg 里说清楚为什么主机端没落上。 */
int game_music_set_host(const char *tid, const unsigned char *at9, size_t len,
                        const char *lufs, long long *bytes_out,
                        char *msg, size_t msgsz);

/* 把主机上的 <sce_sys>/snd0.at9 还原成我们覆盖之前的备份（没有备份就删掉）。
 * 永远返回 0 —— 主机端删不掉不该让「恢复默认」在界面上变成失败。 */
void game_music_reset_host(const char *tid);

/* 判定一段字节是不是 PS5 认的 .at9（RIFF/WAVE + WAVE_FORMAT_EXTENSIBLE + ATRAC9 GUID）。
 * 给 game_music_set_host 做入参校验，也导出给宿主测试用。 */
int game_music_is_at9(const unsigned char *data, size_t len);