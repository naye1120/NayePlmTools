#ifndef INFLATE_H
#define INFLATE_H

/* [CN-FORK] v0.5.2.116 自带的极简 raw DEFLATE 解压
 *
 * 为什么自己写：SDK 的 target/lib 里没有 zlib（curl/mbedtls/microhttpd/pthread 有，
 * -lz 没有 —— 见 include/game_media.h 同一段论证），而**存档上传**必须能吃下别人
 * 用系统压缩工具压出来的 zip，那些默认是 DEFLATE。只支持 STORE 的话，用户在 Windows
 * 上右键压个存档就传不上来，「上传存档」这功能等于残废。
 *
 * 依赖关系倒过来也是成立的：PNG 解码（stb_image）自带一份 inflate，但它是 static，
 * 拿不出来。与其在 stb 头里开洞，不如老老实实写一份 —— DEFLATE 是完全确定的格式，
 * 250 行够了，而且能拿 python 生成的 zip 在主机上逐条验。
 *
 * 只做解压。压缩一律走 STORE（见 zip_io.c 的头注释：截图/录屏/存档本身都压缩过）。 */

#include <stddef.h>

/* 解一段 raw DEFLATE（zip 的 method=8 存的就是这个，没有 zlib 头也没有 adler32 尾）。
 * out/out_cap 由调用方给（**必须一次性给够** —— zip 的 uncompressed size 就在中央目录里，
 * 拿它当容量刚好），*out_len 是实际产出。
 * 返回 0 成功；-1 失败（数据损坏 / 超容量 / 码表非法）。 */
int inflate_raw(const unsigned char *in, size_t in_len,
                unsigned char *out, size_t out_cap, size_t *out_len);

/* 解一个完整的 gzip（1f 8b 08 头 + raw DEFLATE + crc32/ISIZE 尾），
 * 缓冲由它自己按尾部 ISIZE 分配，调用方 free(*out)。
 * 存档上传的文件选择器接受 .gz/.tgz，靠的就是这条路径。
 * 返回 0 成功；-1 失败。 */
int gunzip_alloc(const unsigned char *in, size_t in_len,
                 unsigned char **out, size_t *out_len, char *err, size_t errsz);

#endif
