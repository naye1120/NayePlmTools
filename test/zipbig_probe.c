/* 手动探针（**不在 `run_host_tests.sh` 里**，不进任何一道门）：zip_write 在
 * 「大文件 + 多 256 KB 分块 + CRC 回填」这条路上的行为。
 *
 * 为什么要它：主机夹具里的文件都只有几 KB，那条路一次都没被走到过 —— 而真机上
 * 用户的包是 58.7 MB / 3 个文件，他报的「打包一直卡在 0 B / 58.7 MB」正好落在这里。
 * `media_scan` 套件里已经有一条 1536 KiB 的断言钉着它（回归用），这个探针是
 * **手验用**：想快速确认一个真机体积的包能不能写完、要多久，跑它。
 *
 * 用法（WSL）：
 *   clang -Wall -Wextra -O2 -Iinclude -Isrc/psaccounts/third_party \
 *     test/zipbig_probe.c src/zip_io.c src/game_job.c src/inflate.c \
 *     src/json_helpers.c src/utils.c -o /tmp/zipbig_probe -lpthread
 *   head -c 55000000 /dev/urandom > /tmp/big.mp4
 *   /tmp/zipbig_probe /tmp/out.zip /tmp/big.mp4 /tmp/s1.png /tmp/s2.png
 *   python3 -c "import zipfile;print(zipfile.ZipFile('/tmp/out.zip').testzip())"
 *
 * 2026-10 实测：61 MB / 3 个文件 → rc=0、236 次分块、wall<1s、testzip() 全绿。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "zip_io.h"

static long long g_bytes;
static int g_files;
static int g_ticks;

static void onb(void *u, long long n) { (void)u; g_bytes += n; g_ticks++; }
static void onf(void *u) { (void)u; g_files++; }

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <out.zip> <file...>\n", argv[0]); return 2; }
    int n = argc - 2;
    zip_ent_t *e = (zip_ent_t *)calloc((size_t)n, sizeof(zip_ent_t));
    long long total = 0;
    for (int i = 0; i < n; i++) {
        struct stat sb;
        if (stat(argv[i + 2], &sb) != 0) { perror("stat"); return 2; }
        snprintf(e[i].abs, sizeof(e[i].abs), "%s", argv[i + 2]);
        snprintf(e[i].zname, sizeof(e[i].zname), "video/%d.bin", i);
        e[i].size = (long long)sb.st_size;
        total += e[i].size;
        printf("  in[%d] %s  %lld bytes\n", i, argv[i + 2], (long long)sb.st_size);
    }
    printf("total=%lld bytes, %d files\n", total, n);

    zip_hooks_t h = { NULL, NULL, onb, onf };
    const char *err = NULL;
    time_t t0 = time(NULL);
    int rc = zip_write(argv[1], e, n, &h, &err);
    time_t t1 = time(NULL);
    printf("rc=%d err=%s  bytes=%lld  files=%d  ticks=%d  wall=%lds\n",
           rc, err ? err : "-", g_bytes, g_files, g_ticks, (long)(t1 - t0));
    free(e);
    return rc;
}
