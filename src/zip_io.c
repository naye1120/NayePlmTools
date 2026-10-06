/* [CN-FORK] v0.5.2.116 自研 zip 读写。头注释见 include/zip_io.h。
 *
 * 写这一半是从 src/game_media.c 原样搬过来的（连注释一起），只把「取消 / 进度」
 * 三个静态调用换成了 zip_hooks_t 回调 —— 那三个原本直接打任务槽，存档下载是
 * 同步请求、没有任务可挂，硬绑在一起就得为它伪造一个任务。 */

#include "zip_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "game_job.h" /* gj_crc32 / gj_crc_init */
#include "inflate.h"

/* ══════════════ 写 ══════════════ */

static int zx_write_all(int fd, const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    while (n > 0) {
        ssize_t w = write(fd, b, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        /* ⚠️ w == 0 必须显式挡住。POSIX 允许 write() 在 n>0 时返回 0（不是错误，
         * 只是「这一轮一个字节都没写出去」），而下面 `b += w; n -= w;` 在 w==0 时
         * **两个变量都不动** —— 于是这是一个纯自旋的死循环，CPU 打满、一个字节不落盘、
         * 任务槽永远停在「已开始、0 字节」。
         * 这正是用户报的「打包一直卡在 0 B / 58.7 MB」最像的形态：常规文件上罕见，
         * 但真机上是索尼自家的文件系统，不能拿 ext4 的行为当保证。
         * 返回 -1 让上层报「写入失败」，好过在这里空转到天荒地老。 */
        if (w == 0) return -1;
        b += w;
        n -= (size_t)w;
    }
    return 0;
}
static int zx_put(int fd, const void *p, size_t n) { return zx_write_all(fd, p, n); }

static void zx_p16(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}
static void zx_p32(unsigned char *p, uint32_t v) {
    zx_p16(p, v & 0xFFFF);
    zx_p16(p + 2, v >> 16);
}
static void zx_p64(unsigned char *p, uint64_t v) {
    zx_p32(p, (uint32_t)(v & 0xFFFFFFFFu));
    zx_p32(p + 4, (uint32_t)(v >> 32));
}

static void zx_dos_time(time_t t, uint16_t *dt, uint16_t *dd) {
    struct tm tmv;
    if (!localtime_r(&t, &tmv)) { *dt = 0; *dd = 0x21; return; }
    int y = tmv.tm_year + 1900;
    if (y < 1980) y = 1980;
    *dt = (uint16_t)((tmv.tm_hour << 11) | (tmv.tm_min << 5) | (tmv.tm_sec / 2));
    *dd = (uint16_t)(((y - 1980) << 9) | (tmv.tm_mon << 5) | tmv.tm_mday);
}

/* 整包写盘。返回 0 成功，1 已取消，-1 失败（*err 里给人话）。 */
int zip_write(const char *out, zip_ent_t *e, int n, const zip_hooks_t *h, const char **err) {
    gj_crc_init();
    /* unlink 再 O_EXCL：新 inode。已经在下载上一包的连接还拿着旧 inode，POSIX 下不受影响 */
    unlink(out);
    int fd = open(out, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) { *err = "无法创建打包文件"; return -1; }

    uint64_t off = 0;
    int rc = 0;
    const char *why = "写入失败";

    for (int i = 0; i < n && rc == 0; i++) {
        struct stat sb;
        if (stat(e[i].abs, &sb) != 0 || !S_ISREG(sb.st_mode) || sb.st_size <= 0) {
            *err = "有文件在打包前被移走了";
            rc = -1;
            break;
        }
        e[i].size = (long long)sb.st_size;
        e[i].z64 = (e[i].size >= 0xFFFFFFFFLL);
        e[i].offset = off;

        size_t nl = strlen(e[i].zname);
        unsigned char lex[20];
        size_t lexl = 0;
        if (e[i].z64) {
            zx_p16(lex + 0, 0x0001);
            zx_p16(lex + 2, 16);
            zx_p64(lex + 4, (uint64_t)e[i].size);
            zx_p64(lex + 12, (uint64_t)e[i].size);
            lexl = 20;
        }

        /* 本地头：CRC 先写 0 占位，数据流完再 lseek 回去补 4 字节 —— 源文件因此只读一遍。
         * 录屏几百 MB，为了「先算出 CRC 再写头」而整读一遍等于把 IO 直接翻倍。 */
        unsigned char lh[30];
        zx_p32(lh + 0, 0x04034b50);
        zx_p16(lh + 4, e[i].z64 ? 45 : 20);
        zx_p16(lh + 6, 0x0800); /* general purpose bit 11 = UTF-8 文件名 */
        zx_p16(lh + 8, 0);      /* method 0 = STORE */
        uint16_t dt, dd;
        zx_dos_time(sb.st_mtime, &dt, &dd);
        zx_p16(lh + 10, dt);   /* last mod time @10 */
        zx_p16(lh + 12, dd);   /* last mod date @12 */
        zx_p32(lh + 14, 0);    /* crc-32 @14，占位 */
        zx_p32(lh + 18, e[i].z64 ? 0xFFFFFFFFu : (uint32_t)e[i].size); /* compressed @18 */
        zx_p32(lh + 22, e[i].z64 ? 0xFFFFFFFFu : (uint32_t)e[i].size); /* uncompressed @22 */
        zx_p16(lh + 26, (uint16_t)nl);   /* file name length @26 */
        zx_p16(lh + 28, (uint16_t)lexl); /* extra field length @28 */

        if (lseek(fd, (off_t)e[i].offset, SEEK_SET) < 0 ||
            zx_put(fd, lh, 30) != 0 ||
            zx_put(fd, e[i].zname, nl) != 0 ||
            (lexl && zx_put(fd, lex, lexl) != 0)) {
            *err = why;
            rc = -1;
            break;
        }

        int in = open(e[i].abs, O_RDONLY);
        if (in < 0) { *err = "源文件打不开"; rc = -1; break; }
        size_t cp = 256 * 1024;
        unsigned char *b = (unsigned char *)malloc(cp);
        if (!b) { close(in); *err = "内存不足"; rc = -1; break; }

        uint32_t crc = 0;
        long long left = e[i].size;
        while (left > 0 && rc == 0) {
            if (h && h->cancelled && h->cancelled(h->user)) { rc = 1; break; }
            size_t want = left > (long long)cp ? cp : (size_t)left;
            ssize_t r = read(in, b, want);
            if (r <= 0) { *err = "读源文件失败"; rc = -1; break; }
            crc = gj_crc32(crc, b, (size_t)r);
            if (zx_put(fd, b, (size_t)r) != 0) { *err = why; rc = -1; break; }
            left -= r;
            if (h && h->on_bytes) h->on_bytes(h->user, r);
        }
        free(b);
        close(in);
        if (rc != 0) break;

        e[i].crc = crc;

        /* 回填 CRC，再把写指针挪回条目末尾 */
        unsigned char cs[4];
        zx_p32(cs, e[i].crc);
        off_t after = (off_t)e[i].offset + 30 + (off_t)nl + (off_t)lexl + (off_t)e[i].size;
        if (lseek(fd, (off_t)e[i].offset + 14, SEEK_SET) < 0 ||
            zx_put(fd, cs, 4) != 0 ||
            lseek(fd, after, SEEK_SET) < 0) {
            *err = why;
            rc = -1;
            break;
        }
        off = (uint64_t)after;
        if (h && h->on_file) h->on_file(h->user);
    }

    if (rc == 0) {
        /* ── 中央目录 ── */
        uint64_t cd_off = off;
        for (int i = 0; i < n && rc == 0; i++) {
            size_t nl = strlen(e[i].zname);
            int need_off = e[i].offset >= 0xFFFFFFFFULL;
            size_t payload = (e[i].z64 ? 16u : 0u) + (need_off ? 8u : 0u);
            size_t el = payload ? 4 + payload : 0;
            unsigned char ch[46];
            zx_p32(ch + 0, 0x02014b50);
            zx_p16(ch + 4, 0x031E);                 /* version made by: UNIX 3.0 */
            zx_p16(ch + 6, el ? 45 : 20);           /* version needed */
            zx_p16(ch + 8, 0x0800);                 /* UTF-8 */
            zx_p16(ch + 10, 0);                     /* STORE */
            zx_p16(ch + 12, 0);
            zx_p16(ch + 14, 0);                     /* 时间随后按原文件重算 */
            zx_p32(ch + 16, e[i].crc);
            zx_p32(ch + 20, e[i].z64 ? 0xFFFFFFFFu : (uint32_t)e[i].size);
            zx_p32(ch + 24, e[i].z64 ? 0xFFFFFFFFu : (uint32_t)e[i].size);
            zx_p16(ch + 28, (uint16_t)nl);
            zx_p16(ch + 30, (uint16_t)el);
            zx_p16(ch + 32, 0);                     /* comment len */
            zx_p16(ch + 34, 0);                     /* disk start */
            zx_p16(ch + 36, 0);                     /* internal attrs */
            zx_p32(ch + 38, 0x81A40000u);           /* external attrs: 0100644 << 16 */
            zx_p32(ch + 42, need_off ? 0xFFFFFFFFu : (uint32_t)e[i].offset);

            struct stat sb;
            uint16_t dt = 0, dd = 0x21;
            if (stat(e[i].abs, &sb) == 0) zx_dos_time(sb.st_mtime, &dt, &dd);
            zx_p16(ch + 12, dt);
            zx_p16(ch + 14, dd);

            unsigned char ex[32];
            size_t exl = 0;
            if (payload) {
                zx_p16(ex + 0, 0x0001);
                zx_p16(ex + 2, (uint16_t)payload);
                exl = 4;
                if (e[i].z64) {
                    zx_p64(ex + exl, (uint64_t)e[i].size);
                    zx_p64(ex + exl + 8, (uint64_t)e[i].size);
                    exl += 16;
                }
                if (need_off) {
                    zx_p64(ex + exl, e[i].offset);
                    exl += 8;
                }
            }

            if (zx_put(fd, ch, 46) != 0 ||
                zx_put(fd, e[i].zname, nl) != 0 ||
                (exl && zx_put(fd, ex, exl) != 0)) {
                *err = why;
                rc = -1;
            }
            off += 46 + nl + exl;
        }

        if (rc == 0) {
            uint64_t cd_size = off - cd_off;
            uint64_t eocd64_off = off;

            unsigned char z[56];
            zx_p32(z + 0, 0x06064b50);
            zx_p64(z + 4, 44);
            zx_p16(z + 12, 0x031E);
            zx_p16(z + 14, 45);
            zx_p32(z + 16, 0);
            zx_p32(z + 20, 0);
            zx_p64(z + 24, (uint64_t)n);
            zx_p64(z + 32, (uint64_t)n);
            zx_p64(z + 40, cd_size);
            zx_p64(z + 48, cd_off);

            unsigned char loc[20];
            zx_p32(loc + 0, 0x07064b50);
            zx_p32(loc + 4, 0);
            zx_p64(loc + 8, eocd64_off);
            zx_p32(loc + 16, 1);

            unsigned char eo[22];
            zx_p32(eo + 0, 0x06054b50);
            zx_p16(eo + 4, 0);
            zx_p16(eo + 6, 0);
            uint16_t ne = n >= 0xFFFF ? 0xFFFFu : (uint16_t)n;
            zx_p16(eo + 8, ne);
            zx_p16(eo + 10, ne);
            zx_p32(eo + 12, cd_size >= 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)cd_size);
            zx_p32(eo + 16, cd_off >= 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)cd_off);
            zx_p16(eo + 20, 0);

            if (zx_put(fd, z, 56) != 0 || zx_put(fd, loc, 20) != 0 || zx_put(fd, eo, 22) != 0) {
                *err = why;
                rc = -1;
            }
        }
    }

    if (close(fd) != 0 && rc == 0) { *err = why; rc = -1; }
    if (rc != 0) unlink(out);
    return rc;
}

/* ══════════════ 读 ══════════════ */

static uint16_t zx_r16(const unsigned char *p) {
    return (uint16_t)(p[0] | ((unsigned)p[1] << 8));
}
static uint32_t zx_r32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint64_t zx_r64(const unsigned char *p) {
    return (uint64_t)zx_r32(p) | ((uint64_t)zx_r32(p + 4) << 32);
}

static void zx_err(char *err, size_t errsz, const char *msg) {
    if (err && errsz) snprintf(err, errsz, "%s", msg);
}

/* 单条目解压。local 指向本地头。 */
int zip_entry_data(const unsigned char *local, size_t local_len,
                   unsigned method, uint64_t csize, uint64_t usize,
                   unsigned char *out, size_t out_cap, size_t *out_len,
                   char *err, size_t errsz) {
    if (out_len) *out_len = 0;
    if (!local || local_len < 30 || zx_r32(local) != 0x04034b50u) {
        zx_err(err, errsz, "zip 本地头损坏");
        return -1;
    }
    size_t nl = zx_r16(local + 26), el = zx_r16(local + 28);
    size_t doff = 30 + nl + el;
    if (doff > local_len || (uint64_t)(local_len - doff) < csize) {
        zx_err(err, errsz, "zip 条目数据被截断");
        return -1;
    }
    if (csize > (uint64_t)out_cap || usize > (uint64_t)out_cap) {
        zx_err(err, errsz, "解压缓冲不够");
        return -1;
    }

    if (method == 0) { /* STORE */
        memcpy(out, local + doff, (size_t)csize);
        if (out_len) *out_len = (size_t)csize;
        return 0;
    }
    if (method != 8) {
        if (err && errsz) snprintf(err, errsz, "不支持的压缩方式（%u）", method);
        return -1;
    }
    size_t n = 0;
    if (inflate_raw(local + doff, (size_t)csize, out, out_cap, &n) != 0) {
        zx_err(err, errsz, "解压失败（数据损坏，或这个 zip 用了不支持的特性）");
        return -1;
    }
    if (usize && (uint64_t)n != usize) {
        zx_err(err, errsz, "解压长度与声明不符");
        return -1;
    }
    if (out_len) *out_len = n;
    return 0;
}

/* zip slip：包里给的相对路径不许越界。 */
static int zx_name_ok(const char *name) {
    if (!name || !*name) return 0;
    if (name[0] == '/' || name[0] == '\\') return 0;
    if (strchr(name, ':')) return 0;              /* 盘符 / ADS */
    if (strchr(name, '\\')) return 0;             /* Windows 风格分隔符一律当不合法 */
    const char *s = name;
    while (*s) {
        const char *e = s;
        while (*e && *e != '/') e++;
        size_t len = (size_t)(e - s);
        if (len == 2 && s[0] == '.' && s[1] == '.') return 0;
        if (!*e) break;
        s = e + 1;
    }
    return 1;
}

int zip_extract(const unsigned char *buf, size_t len,
                zip_entry_cb cb, void *user, char *err, size_t errsz) {
    if (!buf || !cb) return -1;
    if (len < 22) { zx_err(err, errsz, "文件太小，不是 zip"); return -1; }

    /* ── EOCD：从尾部往前找（zip 注释最长 65535 字节） ── */
    size_t eocd = (size_t)-1;
    size_t stop = len > (22u + 65535u) ? len - (22u + 65535u) : 0;
    for (size_t i = len - 22; ; i--) {
        if (zx_r32(buf + i) == 0x06054b50u) { eocd = i; break; }
        if (i == stop) break;
    }
    if (eocd == (size_t)-1) { zx_err(err, errsz, "找不到 zip 结尾记录（不是 zip？）"); return -1; }

    uint64_t total = zx_r16(buf + eocd + 10);
    uint64_t cd_size = zx_r32(buf + eocd + 12);
    uint64_t cd_off = zx_r32(buf + eocd + 16);

    /* ── zip64：定位器固定在 EOCD 前 20 字节 ──
     * 我们自己写的包**永远带**定位器（见 zip_write），所以只要它在就以 64 位记录为准，
     * 不去看 classic EOCD 里那些哨兵值 —— 两个都判反而会让「哨兵是 0xFFFF 的正常小包」
     * 被误当成 zip64。 */
    if (eocd >= 20 && zx_r32(buf + eocd - 20) == 0x07064b50u) {
        uint64_t z64 = zx_r64(buf + eocd - 12);
        if (z64 + 56 <= len && zx_r32(buf + z64) == 0x06064b50u) {
            total = zx_r64(buf + z64 + 32);
            cd_size = zx_r64(buf + z64 + 40);
            cd_off = zx_r64(buf + z64 + 48);
        }
    }
    if (total == 0xFFFF || cd_size == 0xFFFFFFFFull || cd_off == 0xFFFFFFFFull) {
        zx_err(err, errsz, "zip64 尾记录缺失");
        return -1;
    }
    if (cd_off + cd_size > (uint64_t)len) {
        zx_err(err, errsz, "zip 中央目录越界");
        return -1;
    }
    if (total > 4096) { zx_err(err, errsz, "zip 条目太多"); return -1; }

    uint64_t p = cd_off;
    uint64_t end = cd_off + cd_size;
    char nm[512];
    for (uint64_t i = 0; i < total; i++) {
        if (p + 46 > end) { zx_err(err, errsz, "zip 中央目录被截断"); return -1; }
        const unsigned char *ch = buf + p;
        if (zx_r32(ch) != 0x02014b50u) { zx_err(err, errsz, "zip 中央目录损坏"); return -1; }
        unsigned method = zx_r16(ch + 10);
        uint32_t crc = zx_r32(ch + 16);
        uint64_t csize = zx_r32(ch + 20);
        uint64_t usize = zx_r32(ch + 24);
        size_t nl = zx_r16(ch + 28), el = zx_r16(ch + 30), cl = zx_r16(ch + 32);
        uint64_t loff = zx_r32(ch + 42);

        const unsigned char *extra = ch + 46 + nl;
        if (p + 46 + nl + el + cl > end) { zx_err(err, errsz, "zip 中央目录越界"); return -1; }
        /* zip64 extra：按 usize → csize → offset 的顺序补 0xFFFFFFFF 的字段 */
        size_t xo = 0;
        while (xo + 4 <= el) {
            uint16_t id = zx_r16(extra + xo), xsz = zx_r16(extra + xo + 2);
            if (xo + 4 + xsz > el) break;
            if (id == 0x0001) {
                const unsigned char *d = extra + xo + 4;
                size_t left = xsz;
                if (usize == 0xFFFFFFFFull && left >= 8) { usize = zx_r64(d); d += 8; left -= 8; }
                if (csize == 0xFFFFFFFFull && left >= 8) { csize = zx_r64(d); d += 8; left -= 8; }
                if (loff == 0xFFFFFFFFull && left >= 8) { loff = zx_r64(d); }
                break;
            }
            xo += 4 + xsz;
        }

        if (nl == 0 || nl >= sizeof(nm)) { zx_err(err, errsz, "zip 条目名非法"); return -1; }
        memcpy(nm, ch + 46, nl);
        nm[nl] = 0;

        p += 46 + nl + el + cl;

        /* 目录条目只记路径，不回调 */
        if (nl > 0 && ch[46 + nl - 1] == '/') continue;
        if (!zx_name_ok(nm)) {
            if (err && errsz) snprintf(err, errsz, "包里的路径不合法（%s）", nm);
            return -1;
        }
        if (usize > (512u << 20)) {
            if (err && errsz) snprintf(err, errsz, "单个条目过大（%s）", nm);
            return -1;
        }

        if (loff + 30 > (uint64_t)len) { zx_err(err, errsz, "zip 本地头越界"); return -1; }
        const unsigned char *lh = buf + loff;
        size_t lnl = zx_r16(lh + 26), lel = zx_r16(lh + 28);
        if (loff + 30 + lnl + lel + csize > (uint64_t)len) {
            zx_err(err, errsz, "zip 条目数据越界");
            return -1;
        }

        /* 条目内容先整块解出来再回调：回调要建目录、写文件，
         * 中间不能让调用方一边写一边等着我们继续读包。 */
        size_t cap = (size_t)(csize > usize ? csize : usize);
        if (cap == 0) cap = 1;
        unsigned char *data = (unsigned char *)malloc(cap);
        if (!data) { zx_err(err, errsz, "内存不足"); return -1; }
        size_t got = 0;
        int rc = zip_entry_data(lh, (size_t)(len - loff), method, csize, usize,
                                data, cap, &got, err, errsz);
        if (rc != 0) { free(data); return -1; }
        if (crc) {
            uint32_t c = gj_crc32(0, data, got);
            if (c != crc) { free(data); zx_err(err, errsz, "CRC 校验失败（数据损坏）"); return -1; }
        }

        /* mtime 从 DOS 时间还原（够用了：列表上显示的就是它） */
        uint16_t dos_t = zx_r16(lh + 10), dos_d = zx_r16(lh + 12);
        struct tm tmv;
        memset(&tmv, 0, sizeof(tmv));
        tmv.tm_sec = (int)(dos_t & 0x1F) * 2;
        tmv.tm_min = (int)((dos_t >> 5) & 0x3F);
        tmv.tm_hour = (int)((dos_t >> 11) & 0x1F);
        tmv.tm_mday = (int)(dos_d & 0x1F);
        tmv.tm_mon = (int)(((dos_d >> 5) & 0x0F) - 1);
        tmv.tm_year = (int)(((dos_d >> 9) & 0x7F) + 80);
        tmv.tm_isdst = -1;
        time_t mt = mktime(&tmv);

        int stop = cb(user, nm, data, got, mt);
        free(data);
        if (stop) return -2;
    }
    return 0;
}
