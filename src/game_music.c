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

#include "cJSON.h"
#include "game_mgr.h"     /* game_mgr_sce_sys_dir：落到主机上要的那份在哪 */
#include "json_helpers.h" /* ensure_dir_recursive */
#include "pldmgr.h"       /* pldmgr_log */

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

/* ══════════════════════════════════════════════════════════════════════════
 * [CN-FORK] v0.5.2.166 把音乐真正写进主机：<sce_sys>/snd0.at9
 *
 * `.163` 只写 /data/nayeplmtools/music/ —— 那是我们自己的目录，主机不读。
 * 用户看到的现象就是「背景音乐改动没有生效」。契约与格式理由见
 * include/game_music.h 的头注释。
 * ══════════════════════════════════════════════════════════════════════════ */

/* 覆盖前的原件备份目录：与 game_title.c 的两层图共用同一个 `orig/` 子目录，
 * 一款游戏一个目录，谁都不会去动别人的叶子名（那边是 icon0.png / pic0.*，
 * 这里是 snd0.at9）。 */
#ifndef GAME_MUSIC_ORIG_BASE
#define GAME_MUSIC_ORIG_BASE "/data/nayeplmtools/titles"
#endif

#define GM_HOST_LEAF "snd0.at9"
#define GM_PARAM_LEAF "param.json"
/* PS5 对 snd0.at9 的硬上限（参考工程实测值：2,097,152 字节）。 */
#define GM_AT9_MAX (2097152u)
/* param.json 的读取上限。真机那些是几 KB，256KB 是防呆。 */
#define GM_PARAM_MAX (256u * 1024u)

/* ATRAC9 的 WAVE sub-format GUID：47E142D2-36BA-4D8D-88FC-61654F8C836C，
 * 在 WAVEFORMATEXTENSIBLE 里按 Data1(LE u32) / Data2(LE u16) / Data3(LE u16) /
 * Data4(大端 8 字节) 存放，所以落到字节流上是这 16 个。 */
static const unsigned char GM_AT9_GUID[16] = {
    0xD2, 0x42, 0xE1, 0x47, 0xBA, 0x36, 0x8D, 0x4D,
    0x88, 0xFC, 0x61, 0x65, 0x4F, 0x8C, 0x83, 0x6C
};

static unsigned gm_rd16(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static unsigned gm_rd32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
           ((unsigned)p[3] << 24);
}

int game_music_is_at9(const unsigned char *d, size_t n) {
    if (!d || n < 12 + 8 + 52) return 0;
    if (memcmp(d, "RIFF", 4) != 0 || memcmp(d + 8, "WAVE", 4) != 0) return 0;
    size_t off = 12;
    while (off + 8 <= n) {
        const unsigned char *id = d + off;
        unsigned sz = gm_rd32(d + off + 4);
        if (memcmp(id, "fmt ", 4) == 0) {
            if (off + 8 + 52 > n) return 0;
            const unsigned char *f = d + off + 8;
            if (gm_rd16(f) != 0xFFFE) return 0; /* 必须是 WAVE_FORMAT_EXTENSIBLE */
            return memcmp(f + 24, GM_AT9_GUID, 16) == 0;
        }
        off += 8 + (size_t)sz + (sz & 1); /* chunk 按偶数字节对齐 */
    }
    return 0;
}

/* 小文件拷贝（备份用）。缓冲放堆上 —— 真机连接线程只有 64KB 栈。 */
static int gm_copy_file(const char *src, const char *dst) {
    FILE *in = fopen(src, "rb");
    if (!in) return -1;
    FILE *out = fopen(dst, "wb");
    if (!out) { fclose(in); return -1; }
    unsigned char buf[8192];
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    }
    if (ferror(in)) rc = -1;
    fclose(in);
    if (fclose(out) != 0) rc = -1;
    if (rc != 0) remove(dst); /* 半截备份比没有备份更糟 */
    return rc;
}

/* 第一次覆盖前把主机上的原件留一份。**已有就绝不覆盖** —— 覆盖掉的会变成
 * 「上一次被我改过的版本」，真出事时反而没有可回滚的原件（与 tile_db.c 同一条理由）。 */
static void gm_backup_host_leaf(const char *tid, const char *sce_sys, const char *leaf) {
    char src[768], dir[768], dst[900];
    struct stat st;
    snprintf(src, sizeof(src), "%s/%s", sce_sys, leaf);
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) return;
    snprintf(dir, sizeof(dir), "%s/%s/orig", GAME_MUSIC_ORIG_BASE, tid);
    if (ensure_dir_recursive(dir) != 0) return;
    snprintf(dst, sizeof(dst), "%s/%s", dir, leaf);
    if (stat(dst, &st) == 0) return; /* 已经留过原件 */
    if (gm_copy_file(src, dst) != 0)
        pldmgr_log("[PLDMGR] music: %s 备份 %s 失败，继续\n", tid, leaf);
}

/* ── param.json 的响度元数据 ────────────────────────────────────────────────
 * PS5 从 `pubtools.loudnessSnd0` 读这条音乐的响度（参考工程的口径）。我们的编码器
 * 已经归一化到 -28 LUFS，所以写进去的是同一个值。
 *
 * 为什么走**文本插入**而不是 cJSON 反序列化再整份重写：param.json 是游戏自己的
 * 元数据文件，整份重写会把原来的缩进、CRLF、键序全换掉 —— 而这个仓库在
 * `.132`（改名写 param.json）上已经吃过一次「格式被踩」的教训，那边专门做了
 * 保格式的定向改写。这里沿用同一策略：只在 pubtools 对象里插/改一个键。
 *
 * 三道保险：① 只在文件已存在时动；② 改完先 cJSON_Parse 验一遍，解析不过就放弃；
 * ③ 原子写（.tmp + rename），并且写之前已由调用方备份进 orig/。 */
static int gm_patch_loudness(const char *param_path, const char *lufs) {
    struct stat st;
    if (stat(param_path, &st) != 0 || !S_ISREG(st.st_mode)) return 0; /* 没有这个文件，跳过 */
    if (st.st_size <= 0 || (size_t)st.st_size > GM_PARAM_MAX) return 0;

    FILE *f = fopen(param_path, "rb");
    if (!f) return 0;
    size_t cap = (size_t)st.st_size;
    char *buf = (char *)malloc(cap + 512); /* 多留 512 给插入的键 */
    if (!buf) { fclose(f); return 0; }
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    buf[n] = 0;

    /* 定位 "loudnessSnd0"。已经有过就替换它的字符串值，没有就在 pubtools 的 `{` 后插入。 */
    char *key = strstr(buf, "\"loudnessSnd0\"");
    char *out = NULL;
    size_t out_len = 0;
    if (key) {
        char *colon = strchr(key, ':');
        char *q1 = colon ? strchr(colon, '"') : NULL;
        char *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
        if (!q1 || !q2) { free(buf); return 0; }
        out_len = (size_t)(q1 - buf) + 1 + strlen(lufs) + (n - (size_t)(q2 - buf));
        out = (char *)malloc(out_len + 1);
        if (!out) { free(buf); return 0; }
        size_t pre = (size_t)(q1 - buf) + 1;
        memcpy(out, buf, pre);
        memcpy(out + pre, lufs, strlen(lufs));
        memcpy(out + pre + strlen(lufs), q2, n - (size_t)(q2 - buf));
        out[out_len] = 0;
    } else {
        char *pt = strstr(buf, "\"pubtools\"");
        char *brace = pt ? strchr(pt, '{') : NULL;
        if (!brace) { free(buf); return 0; } /* 没有 pubtools：不硬造，跳过 */
        size_t at = (size_t)(brace - buf) + 1;
        char ins[64];
        int il = snprintf(ins, sizeof(ins), "\"loudnessSnd0\":\"%s\",", lufs);
        if (il <= 0) { free(buf); return 0; }
        out_len = n + (size_t)il;
        out = (char *)malloc(out_len + 1);
        if (!out) { free(buf); return 0; }
        memcpy(out, buf, at);
        memcpy(out + at, ins, (size_t)il);
        memcpy(out + at + (size_t)il, buf + at, n - at);
        out[out_len] = 0;
    }
    free(buf);

    /* ② 改完必须还是合法 JSON —— 解析不过就当没改过（宁可响度不对，也不能写坏元数据）。 */
    cJSON *chk = cJSON_Parse(out);
    if (!chk) {
        pldmgr_log("[PLDMGR] music: param.json 改完不是合法 JSON，放弃写入\n");
        free(out);
        return 0;
    }
    cJSON_Delete(chk);

    /* ③ 原子写。 */
    char tmp[900];
    snprintf(tmp, sizeof(tmp), "%s.tmp", param_path);
    FILE *w = fopen(tmp, "wb");
    if (!w) { free(out); return 0; }
    int ok = (fwrite(out, 1, out_len, w) == out_len) && (fclose(w) == 0);
    free(out);
    if (!ok) { unlink(tmp); return 0; }
    if (rename(tmp, param_path) != 0) { unlink(tmp); return 0; }
    return 1;
}

int game_music_set_host(const char *tid, const unsigned char *at9, size_t len,
                        const char *lufs, long long *bytes_out,
                        char *msg, size_t msgsz) {
    if (bytes_out) *bytes_out = 0;
    if (msg && msgsz) msg[0] = 0;
    if (!tid || !*tid) { if (msg) snprintf(msg, msgsz, "缺少 tid"); return -1; }
    if (!at9 || !len) { if (msg) snprintf(msg, msgsz, "音频是空的"); return -1; }
    if (len > GM_AT9_MAX) {
        if (msg) snprintf(msg, msgsz, "ATRAC9 文件超过 2MB 上限（PS5 的硬限制）");
        return -1;
    }
    if (!game_music_is_at9(at9, len)) {
        if (msg) snprintf(msg, msgsz,
                          "这不是 ATRAC9（.at9）文件 —— PS5 磁贴音乐只认这个格式");
        return -1;
    }

    char sce_sys[768];
    if (!game_mgr_sce_sys_dir(tid, sce_sys, sizeof(sce_sys))) {
        if (msg) snprintf(msg, msgsz,
                          "找不到这个游戏的 sce_sys（没装进 /user/app，或是个没登记的转储），"
                          "音乐只存在面板里");
        return 2;
    }

    /* 备份 + 原子写。写临时文件再 rename —— 写到一半崩了不至于留半截 snd0.at9
     * （Shell 会去读它，半截文件比没有更糟）。 */
    gm_backup_host_leaf(tid, sce_sys, GM_HOST_LEAF);
    char dst[900], tmp[900];
    snprintf(dst, sizeof(dst), "%s/%s", sce_sys, GM_HOST_LEAF);
    snprintf(tmp, sizeof(tmp), "%s/.%s.tmp", sce_sys, GM_HOST_LEAF);
    FILE *f = fopen(tmp, "wb");
    if (!f) {
        if (msg) snprintf(msg, msgsz, "写不进主机（%s）—— 只读镜像或权限不足", strerror(errno));
        return 1;
    }
    size_t w = fwrite(at9, 1, len, f);
    int ferr = ferror(f);
    if (fclose(f) != 0 || w != len || ferr) {
        unlink(tmp);
        if (msg) snprintf(msg, msgsz, "写入 snd0.at9 失败（%s）", strerror(errno));
        return 1;
    }
    if (rename(tmp, dst) != 0) {
        unlink(tmp);
        if (msg) snprintf(msg, msgsz, "发布 snd0.at9 失败（%s）", strerror(errno));
        return 1;
    }

    /* 响度元数据：best-effort，失败只记日志（没有它音乐照样放，只是音量按主机默认）。 */
    if (lufs && lufs[0]) {
        char param[900];
        snprintf(param, sizeof(param), "%s/%s", sce_sys, GM_PARAM_LEAF);
        gm_backup_host_leaf(tid, sce_sys, GM_PARAM_LEAF);
        if (gm_patch_loudness(param, lufs) == 0)
            pldmgr_log("[PLDMGR] music: %s param.json 的 loudnessSnd0 没写上（跳过）\n", tid);
    }

    if (bytes_out) *bytes_out = (long long)len;
    pldmgr_log("[PLDMGR] music: %s snd0.at9 已写进主机（%zu 字节）\n", tid, len);
    return 0;
}

void game_music_reset_host(const char *tid) {
    if (!tid || !*tid) return;
    char sce_sys[768];
    if (!game_mgr_sce_sys_dir(tid, sce_sys, sizeof(sce_sys))) return;
    char dst[900], bak[900];
    snprintf(dst, sizeof(dst), "%s/%s", sce_sys, GM_HOST_LEAF);
    snprintf(bak, sizeof(bak), "%s/%s/orig/%s", GAME_MUSIC_ORIG_BASE, tid, GM_HOST_LEAF);
    struct stat st;
    if (stat(bak, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0) {
        /* 有原件就还原（而不是删掉）—— 有些游戏自带 snd0.at9，删了等于把它弄丢了。 */
        if (gm_copy_file(bak, dst) != 0)
            pldmgr_log("[PLDMGR] music: %s 还原 snd0.at9 失败\n", tid);
        else
            pldmgr_log("[PLDMGR] music: %s snd0.at9 已还原成原件\n", tid);
    } else {
        unlink(dst);
        pldmgr_log("[PLDMGR] music: %s snd0.at9 已删除（本来就没有原件）\n", tid);
    }
}