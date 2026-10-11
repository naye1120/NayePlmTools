/* [CN-FORK] v0.5.2.163 背景音乐：磁贴在主屏预览时会播放。
 *
 * 接口契约：
 *   game_music_path(tid, tp, tpsz, mime, mimesz)  → 0/1
 *     把 (tid) 对应的当前生效音乐路径写到 tp，mime 写到 mime。
 *     「没设过」回 1，调用方转 404 + ENOENT。
 *
 *   game_music_set(tid, name, data, len, bytes_out, msg, msgsz) → 0/-1/1
 *     0 = 成功，bytes_out = 实际写入字节数
 *     -1 = 校验失败（mime/大小） —— 400 + EBAD
 *     1 = IO 失败（写 / 索引更新失败） —— 500 + EIO
 *
 *   game_music_reset(tid)
 *     删主机上已有的（不存在也是 0）。
 *
 * 落点：/data/nayeplmtools/music/<TID>.<ext>
 *     扩展名按 Content-Type（audio/mpeg→mp3、audio/ogg→ogg、audio/wav|wav→wav）
 *     兜底：name 里有 .mp3/.ogg/.wav 就用扩展名，否则用 .bin。
 *
 * 体积上限：30MB（与前端一致；超过则 -1）。 */

#include "game_music.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef GAME_MUSIC_DIR
#define GAME_MUSIC_DIR "/data/nayeplmtools/music"
#endif
#define MUSIC_MAX_BYTES (30u * 1024u * 1024u)

/* [CN-FORK] 扩展名只认**原始文件名**：调用方给的是 garlic / 浏览器的原始文件名，
 * 主机侧的 mime 不可靠（部分手机会把 mp3 标成 octet-stream）。名字里认不出就退到 mp3。
 * ⚠ 曾经这里还有一个按 mime 反推扩展名的 `ext_from_mime`，但它没有消费者
 * （`game_music_set` 的签名里根本不带 mime）—— `-Wall` 会报 unused-function，已删。 */
static const char *ext_from_name(const char *name) {
    const char *dot = strrchr(name ? name : "", '.');
    if (!dot || dot == name || strlen(dot) > 5) return NULL;
    /* 跳过 . 自身，转小写比对 */
    char b[8];
    size_t n = 0;
    for (const char *p = dot + 1; *p && n < sizeof(b) - 1; p++) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c += 32;
        b[n++] = c;
    }
    b[n] = 0;
    if (!strcmp(b, "mp3")) return "mp3";
    if (!strcmp(b, "ogg")) return "ogg";
    if (!strcmp(b, "wav")) return "wav";
    return NULL;
}

static void path_for_tid(const char *tid, const char *ext, char *out, size_t osz) {
    snprintf(out, osz, "%s/%s.%s", GAME_MUSIC_DIR, tid, ext);
}

static void mime_for_ext(const char *ext, char *out, size_t osz) {
    if (!strcmp(ext, "mp3")) snprintf(out, osz, "audio/mpeg");
    else if (!strcmp(ext, "ogg")) snprintf(out, osz, "audio/ogg");
    else if (!strcmp(ext, "wav")) snprintf(out, osz, "audio/wav");
    else snprintf(out, osz, "application/octet-stream");
}

static int ensure_dir(const char *dir) {
    struct stat st;
    if (stat(dir, &st) == 0) return S_ISDIR(st.st_mode) ? 0 : -1;
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

int game_music_path(const char *tid, char *tp, size_t tpsz,
                    char *mime, size_t mimesz) {
    if (!tid || !*tid || !tp || tpsz < 16) return 1;
    DIR *d = opendir(GAME_MUSIC_DIR);
    if (!d) { tp[0] = 0; if (mime && mimesz) mime[0] = 0; return 1; }
    char want[64];
    snprintf(want, sizeof(want), "%s.", tid);
    size_t wlen = strlen(want);
    struct dirent *de2;
    int found = 0;
    while ((de2 = readdir(d))) {
        if (strncmp(de2->d_name, want, wlen) != 0) continue;
        const char *ext = de2->d_name + wlen;
        path_for_tid(tid, ext, tp, tpsz);
        mime_for_ext(ext, mime, mimesz);
        found = 1;
        break;
    }
    closedir(d);
    if (!found) { tp[0] = 0; if (mime && mimesz) mime[0] = 0; return 1; }
    return 0;
}

int game_music_set(const char *tid, const char *name,
                   const unsigned char *data, size_t len,
                   long long *bytes_out, char *msg, size_t msgsz) {
    if (!tid || !*tid) { if (msg) snprintf(msg, msgsz, "缺少 tid"); return -1; }
    if (!data || !len) { if (msg) snprintf(msg, msgsz, "音频是空的"); return -1; }
    if (len > MUSIC_MAX_BYTES) {
        if (msg) snprintf(msg, msgsz, "音频超过 30MB 上限");
        return -1;
    }
    const char *ext = ext_from_name(name);
    if (!ext) ext = "mp3";  /* 兜底：原文件名不规范就用 mp3，浏览器照样能播 */
    if (ensure_dir(GAME_MUSIC_DIR) != 0) {
        if (msg) snprintf(msg, msgsz, "无法创建音乐目录");
        return 1;
    }
    /* 先写临时文件再 rename —— 写到一半崩了不至于半截文件占位。 */
    char tmp[1024], dst[1024];
    snprintf(tmp, sizeof(tmp), "%s/.%s.%s.tmp", GAME_MUSIC_DIR, tid, ext);
    path_for_tid(tid, ext, dst, sizeof(dst));
    FILE *f = fopen(tmp, "wb");
    if (!f) { if (msg) snprintf(msg, msgsz, "无法写入临时文件"); return 1; }
    size_t w = fwrite(data, 1, len, f);
    int err = ferror(f);
    if (fclose(f) != 0 || w != len || err) {
        unlink(tmp);
        if (msg) snprintf(msg, msgsz, "写入失败");
        return 1;
    }
    if (rename(tmp, dst) != 0) {
        unlink(tmp);
        if (msg) snprintf(msg, msgsz, "无法发布音频文件");
        return 1;
    }
    if (bytes_out) *bytes_out = (long long)len;
    return 0;
}

void game_music_reset(const char *tid) {
    if (!tid || !*tid) return;
    DIR *d = opendir(GAME_MUSIC_DIR);
    if (!d) return;
    char want[64];
    snprintf(want, sizeof(want), "%s.", tid);
    size_t wlen = strlen(want);
    struct dirent *de2;
    while ((de2 = readdir(d))) {
        if (strncmp(de2->d_name, want, wlen) != 0) continue;
        char p[1024];
        snprintf(p, sizeof(p), "%s/%s", GAME_MUSIC_DIR, de2->d_name);
        unlink(p);
    }
    closedir(d);
}