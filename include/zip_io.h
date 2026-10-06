#ifndef ZIP_IO_H
#define ZIP_IO_H

/* [CN-FORK] v0.5.2.116 自研 zip 读写（SDK 里没有 zlib，引第三方要动构建链）
 *
 * ── 写：只用 STORE ────────────────────────────────────────────────────────
 * 截图(png/jxr)、录屏(mp4/webm)、存档(sdimg*)本身就是压缩过的，再 deflate 一遍只多花
 * CPU 不省多少空间，而 STORE 是零依赖的。同一段论证见 include/game_media.h。
 * 凡是 size 或本地头偏移 ≥ 4GiB 就给该条目挂 0x0001 extra；EOCD64 + locator **永远写**
 * （classic EOCD 里该变哨兵的字段才变哨兵），这样两种读法拿到的值都一致。
 * 产出可用 python -c "import zipfile;zipfile.ZipFile(p).testzip()" 直接验。
 *
 * ── 读：STORE 与 DEFLATE 都吃 ────────────────────────────────────────────
 * 上传存档时收到的包是**别人压的**（Windows 资源管理器右键就是 DEFLATE），只认 STORE
 * 等于「上传存档」这功能残废。inflate 见 src/inflate.c。
 *
 * 这套读写从 game_media.c 原样搬出来（那边只留一个薄壳调用），存档下载/上传共用，
 * 免得仓库里出现两份必须逐字节一致的 zip 编码器。 */

#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* 一个待打包条目。字段与原 md_ent_t 逐一同义。 */
typedef struct {
    char abs[700];   /* 磁盘上的源文件 */
    char zname[512]; /* zip 内路径（UTF-8，general purpose bit 11 已置位） */
    long long size;
    uint32_t crc;
    uint64_t offset;
    int z64;
} zip_ent_t;

/* 进度与取消回调。全都是**可选**的：传 NULL 就是不回报（存档下载是同步请求，
 * 没有后台任务可挂，进度没有意义）。 */
typedef struct {
    void *user;
    int (*cancelled)(void *user);          /* 非 0 → 中止，zip_write 返回 1 */
    void (*on_bytes)(void *user, long long n);
    void (*on_file)(void *user);
} zip_hooks_t;

/* 写一个 STORE zip。返回 0 成功、1 已取消、-1 失败（*err 指向静态中文原因）。
 * 会**重新 stat 每个源文件并把 size 写回条目** —— 打包前那一遍 size 只是用来算进度的，
 * 以这里读到的为准才不会写出一个长度对不上的本地头。 */
int zip_write(const char *out, zip_ent_t *e, int n, const zip_hooks_t *h, const char **err);

/* 读侧：把内存里的 zip 解开，逐条目回调（name/data/len/mt）。
 * 返回 0 全部成功；-2 回调要求中止；-1 格式/校验失败（err 里给人话）。
 * cb 返回非 0 表示中止。
 * **路径安全**：绝对路径、`..`、盘符、反斜杠一律拒绝 —— 这是解别人传上来的包，
 * 名字里带 ../../../../etc/passwd 就是经典的 zip slip。 */
typedef int (*zip_entry_cb)(void *user, const char *name, const unsigned char *data,
                            size_t len, time_t mt);
int zip_extract(const unsigned char *buf, size_t len,
                zip_entry_cb cb, void *user, char *err, size_t errsz);

/* 单条目解压到调用方缓冲（STORE 直接切片，DEFLATE 走 inflate_raw）。
 * 0 成功。out/out_cap 必须给够 (csize 对应的 ucsz)。 */
int zip_entry_data(const unsigned char *local, size_t local_len,
                   unsigned method, uint64_t csize, uint64_t usize,
                   unsigned char *out, size_t out_cap, size_t *out_len,
                   char *err, size_t errsz);

#endif
