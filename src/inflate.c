/* [CN-FORK] v0.5.2.116 raw DEFLATE 解压（zip 的 method=8）
 * 结构是教科书式的：位读取器 → 码表（canonical Huffman）→ 三种块。
 * 参考 RFC 1951；没有抄任何现成实现的代码，写完拿 python zipfile 造的包逐条验。 */

#include "inflate.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game_job.h" /* gj_crc32：gzip 尾部的 CRC 要校验 */

/* ── 位读取器：LSB 优先（DEFLATE 的位序就是这个） ─────────────────────── */
typedef struct {
    const unsigned char *b;
    size_t n;
    size_t pos;       /* 下一个还没进 hold 的字节 */
    unsigned long hold;
    int bits;         /* hold 里当前有效位数 */
} br_t;

static int br_need(br_t *r, int k) {
    while (r->bits < k) {
        if (r->pos >= r->n) return -1;
        r->hold |= (unsigned long)r->b[r->pos++] << r->bits;
        r->bits += 8;
    }
    return 0;
}

static unsigned br_get(br_t *r, int k) {
    if (k <= 0) return 0;
    unsigned v = (unsigned)(r->hold & ((1ul << k) - 1));
    r->hold >>= k;
    r->bits -= k;
    return v;
}

/* 丢掉不足 1 字节的余位，并把已读进 hold 的整字节退回流里。
 * 退回是关键：stored 块要在**字节边界**上取 LEN/NLEN，而那些字节已经被
 * hold 吃掉了 —— 不退回去就得在两套读法之间来回搬数据。 */
static void br_align(br_t *r) {
    int drop = r->bits & 7;
    r->hold >>= drop;
    r->bits -= drop;
    r->pos -= (size_t)(r->bits >> 3);
    r->bits = 0;
    r->hold = 0;
}

/* ── canonical Huffman（puff 那套 count/symbol 表示） ─────────────────── */
typedef struct {
    short count[16];   /* count[len] = 码长为 len 的码字个数 */
    short symbol[288]; /* 按码长分段排好的符号表 */
} huff_t;

/* length[] 是每个符号的码长（0 = 不用）。返回 0 可用，<0 过度订阅（非法）。 */
static int huff_build(huff_t *h, const short *length, int n) {
    int symbol, len, left;
    short offs[16];

    for (len = 0; len <= 15; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0; /* 一个码都没有 */

    left = 1;
    for (len = 1; len <= 15; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left; /* 过度订阅 */
    }

    offs[1] = 0;
    for (len = 1; len < 15; len++) offs[len + 1] = (short)(offs[len] + h->count[len]);
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol]) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left; /* >0 表示码表不完整；单个不完整码字是允许的（ZIP 里真实存在） */
}

/* 返回符号；-1 表示读到不完整的码（输入被截断或码表非法）。 */
static int huff_decode(br_t *r, const huff_t *h) {
    int len, code = 0, first = 0, index = 0;
    for (len = 1; len <= 15; len++) {
        if (br_need(r, 1) != 0) return -1;
        code |= (int)br_get(r, 1);
        int cnt = h->count[len];
        if (code - first < cnt) return h->symbol[index + (code - first)];
        index += cnt;
        first += cnt;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

/* ── 长度 / 距离码表（RFC 1951 表 6、表 7） ───────────────────────────── */
static const unsigned short LBASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
    67, 83, 99, 115, 131, 163, 195, 227, 258};
static const unsigned char LEXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
    4, 4, 4, 4, 5, 5, 5, 5, 0};
static const unsigned short DBASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513,
    769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const unsigned char DEXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8,
    9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
static const unsigned char CLEN_ORDER[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

/* 把字节拷进输出窗口。dist 是回看距离，所以拷贝与源可能重叠 ——
 * 逐字节拷（而不是 memcpy）才能正确展开 RLE 式的重复。 */
static int emit_match(unsigned char *out, size_t *op, size_t cap,
                      unsigned dist, unsigned len) {
    if (dist == 0 || dist > *op) return -1;
    if (*op + len > cap) return -1;
    size_t from = *op - dist;
    for (unsigned i = 0; i < len; i++) out[(*op)++] = out[from + i];
    return 0;
}

static int inflate_block(br_t *r, unsigned char *out, size_t cap, size_t *op,
                         const huff_t *lit, const huff_t *dist) {
    for (;;) {
        int sym = huff_decode(r, lit);
        if (sym < 0) return -1;
        if (sym < 256) {
            if (*op >= cap) return -1;
            out[(*op)++] = (unsigned char)sym;
            continue;
        }
        if (sym == 256) return 0; /* 块结束 */
        if (sym < 257 || sym > 285) return -1;

        unsigned li = (unsigned)sym - 257;
        if (br_need(r, LEXTRA[li]) != 0) return -1;
        unsigned len = LBASE[li] + br_get(r, LEXTRA[li]);

        int dsym = huff_decode(r, dist);
        if (dsym < 0 || dsym >= 30) return -1;
        if (br_need(r, DEXTRA[dsym]) != 0) return -1;
        unsigned d = DBASE[dsym] + br_get(r, DEXTRA[dsym]);

        if (emit_match(out, op, cap, d, len) != 0) return -1;
    }
}

static int inflate_fixed(br_t *r, unsigned char *out, size_t cap, size_t *op) {
    short ll[288], dd[32];
    for (int i = 0; i < 144; i++) ll[i] = 8;
    for (int i = 144; i < 256; i++) ll[i] = 9;
    for (int i = 256; i < 280; i++) ll[i] = 7;
    for (int i = 280; i < 288; i++) ll[i] = 8;
    for (int i = 0; i < 32; i++) dd[i] = 5;

    huff_t lh, dh;
    if (huff_build(&lh, ll, 288) < 0) return -1;
    if (huff_build(&dh, dd, 32) < 0) return -1;
    return inflate_block(r, out, cap, op, &lh, &dh);
}

static int inflate_dynamic(br_t *r, unsigned char *out, size_t cap, size_t *op) {
    if (br_need(r, 5 + 5 + 4) != 0) return -1;
    int hlit = (int)br_get(r, 5) + 257;
    int hdist = (int)br_get(r, 5) + 1;
    int hclen = (int)br_get(r, 4) + 4;
    if (hlit > 288 || hdist > 32) return -1;

    short cl_len[19];
    memset(cl_len, 0, sizeof(cl_len));
    for (int i = 0; i < hclen; i++) {
        if (br_need(r, 3) != 0) return -1;
        cl_len[CLEN_ORDER[i]] = (short)br_get(r, 3);
    }
    huff_t cl;
    if (huff_build(&cl, cl_len, 19) < 0) return -1;

    short lens[320];
    int n = hlit + hdist;
    memset(lens, 0, sizeof(lens));
    int i = 0;
    while (i < n) {
        int sym = huff_decode(r, &cl);
        if (sym < 0) return -1;
        if (sym < 16) {
            lens[i++] = (short)sym;
        } else if (sym == 16) {
            if (i == 0) return -1;
            short prev = lens[i - 1];
            if (br_need(r, 2) != 0) return -1;
            int rep = 3 + (int)br_get(r, 2);
            if (i + rep > n) return -1;
            while (rep--) lens[i++] = prev;
        } else if (sym == 17) {
            if (br_need(r, 3) != 0) return -1;
            int rep = 3 + (int)br_get(r, 3);
            if (i + rep > n) return -1;
            while (rep--) lens[i++] = 0;
        } else if (sym == 18) {
            if (br_need(r, 7) != 0) return -1;
            int rep = 11 + (int)br_get(r, 7);
            if (i + rep > n) return -1;
            while (rep--) lens[i++] = 0;
        } else {
            return -1;
        }
    }
    /* literal/length 表里必须有 256（EOB），否则块永远收不了尾 */
    if (lens[256] == 0) return -1;

    huff_t lh, dh;
    if (huff_build(&lh, lens, hlit) < 0) return -1;
    if (huff_build(&dh, lens + hlit, hdist) < 0) return -1;
    return inflate_block(r, out, cap, op, &lh, &dh);
}

int inflate_raw(const unsigned char *in, size_t in_len,
                unsigned char *out, size_t out_cap, size_t *out_len) {
    if (!in || !out || !out_len) return -1;
    *out_len = 0;
    br_t r;
    r.b = in;
    r.n = in_len;
    r.pos = 0;
    r.hold = 0;
    r.bits = 0;
    size_t op = 0;

    for (;;) {
        if (br_need(&r, 3) != 0) return -1;
        int bfinal = (int)br_get(&r, 1);
        unsigned btype = br_get(&r, 2);

        int rc;
        if (btype == 0) {
            br_align(&r);
            if (r.pos + 4 > r.n) return -1;
            unsigned len = (unsigned)r.b[r.pos] | ((unsigned)r.b[r.pos + 1] << 8);
            unsigned nlen = (unsigned)r.b[r.pos + 2] | ((unsigned)r.b[r.pos + 3] << 8);
            r.pos += 4;
            if ((len ^ 0xFFFFu) != nlen) return -1;
            if (r.pos + len > r.n) return -1;
            if (op + len > out_cap) return -1;
            memcpy(out + op, r.b + r.pos, len);
            op += len;
            r.pos += len;
            r.hold = 0;
            r.bits = 0;
            rc = 0;
        } else if (btype == 1) {
            rc = inflate_fixed(&r, out, out_cap, &op);
        } else if (btype == 2) {
            rc = inflate_dynamic(&r, out, out_cap, &op);
        } else {
            return -1; /* BTYPE=3 保留 */
        }
        if (rc != 0) return -1;
        if (bfinal) break;
    }

    *out_len = op;
    return 0;
}

/* ── gzip 容器 ────────────────────────────────────────────────────────────
 * 头是固定的 10 字节，后面四个可选字段按 FLG 走；载荷就是上面的 raw DEFLATE，
 * 末尾 8 字节 = crc32(解压后内容) + ISIZE(解压长度 mod 2^32)。
 * 我们**按 ISIZE 预分配**再校验 CRC —— 存档都在几百 MB 以内，mod 2^32 不会绕回来，
 * 于是「边解边猜容量」和「解完再重算长度」这两件麻烦事都省了，还能顺手抓到截断的包。 */
#define GUNZIP_MAX (512u << 20) /* 512MB：存档远到不了这条线，纯粹防一个畸形包把内存吃穿 */

int gunzip_alloc(const unsigned char *in, size_t in_len,
                 unsigned char **out, size_t *out_len, char *err, size_t errsz) {
    if (out) *out = NULL;
    if (out_len) *out_len = 0;
    if (err && errsz) err[0] = 0;

    if (!in || in_len < 18) { /* 10 头 + 最小的空块 + 8 尾 */ return -1; }
    if (in[0] != 0x1F || in[1] != 0x8B || in[2] != 0x08) { return -1; }

    unsigned flg = in[3];
    size_t p = 10;
    if (flg & 0x04) { /* FEXTRA */
        if (p + 2 > in_len) return -1;
        unsigned xlen = (unsigned)in[p] | ((unsigned)in[p + 1] << 8);
        p += 2 + xlen;
    }
    if (flg & 0x08) { /* FNAME */ while (p < in_len && in[p]) p++; p++; }
    if (flg & 0x10) { /* FCOMMENT */ while (p < in_len && in[p]) p++; p++; }
    if (flg & 0x02) { /* FHCRC */ p += 2; }
    if (p + 8 >= in_len) return -1;

    uint32_t isize = (uint32_t)in[in_len - 4] | ((uint32_t)in[in_len - 3] << 8) |
                     ((uint32_t)in[in_len - 2] << 16) | ((uint32_t)in[in_len - 1] << 24);
    if (isize > GUNZIP_MAX) {
        if (err && errsz) snprintf(err, errsz, "解压后太大（%u 字节）", (unsigned)isize);
        return -1;
    }

    size_t cap = isize ? isize : 1;
    unsigned char *buf = (unsigned char *)malloc(cap);
    if (!buf) return -1;

    size_t n = 0;
    if (inflate_raw(in + p, in_len - p - 8, buf, cap, &n) != 0) {
        free(buf);
        if (err && errsz) snprintf(err, errsz, "解压失败（文件损坏，或不是有效的 gzip）");
        return -1;
    }
    if (n != (size_t)isize) {
        free(buf);
        if (err && errsz) snprintf(err, errsz, "解压长度与声明不符（文件被截断）");
        return -1;
    }
    uint32_t want = (uint32_t)in[in_len - 8] | ((uint32_t)in[in_len - 7] << 8) |
                    ((uint32_t)in[in_len - 6] << 16) | ((uint32_t)in[in_len - 5] << 24);
    if (want && gj_crc32(0, buf, n) != want) {
        free(buf);
        if (err && errsz) snprintf(err, errsz, "CRC 校验失败（文件损坏）");
        return -1;
    }

    if (out) *out = buf; else free(buf);
    if (out_len) *out_len = n;
    return out ? 0 : -1;
}
