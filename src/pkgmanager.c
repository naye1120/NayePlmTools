/*
 * [CN-FORK] PKG Manager 内置集成实现，接口语义见 pkgmanager.h。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "pldmgr.h"
#include "pkgmanager.h"
#include "ps5_launcher.h"

/* src/pkgmanager_blob.S 经 incbin 嵌入的基线 ELF */
extern const unsigned char pkgmanager_blob[];
extern const unsigned char pkgmanager_blob_end[];

/* PKG Manager main.c 埋的版本签名：__attribute__((used)) "PKGMGR_VER:" <version> */
static const char PKGMGR_VER_SIG[] = "PKGMGR_VER:";

int pkgmanager_extract_version(const void *buf, size_t len, char *out, size_t osz) {
    const unsigned char *p = (const unsigned char *)buf;
    size_t siglen = sizeof(PKGMGR_VER_SIG) - 1;
    if (!buf || !out || osz < 8 || len <= siglen) return -1;
    for (size_t i = 0; i + siglen <= len; i++) {
        if (p[i] != 'P' || memcmp(p + i, PKGMGR_VER_SIG, siglen) != 0) continue;
        size_t j = i + siglen, n = 0;
        while (j < len && n + 1 < osz && p[j] >= 0x21 && p[j] < 0x7f)
            out[n++] = (char)p[j++];
        out[n] = 0;
        if (n > 0) return 0;
    }
    return -1;
}

static void pkgmgr_ensure_dir(void) {
    mkdir(BASE_DATA_DIR, 0777);
    mkdir(PKGMGR_DIR, 0777);
}

/* 朴素字节查找：2MB 文件里找短串足够快，不依赖 memmem（SDK libc 不保证有） */
static int find_bytes(const unsigned char *hay, size_t hlen, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0 || hlen < nlen) return 0;
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (hay[i] == needle[0] && memcmp(hay + i, needle, nlen) == 0) return 1;
    }
    return 0;
}

/* 从上传文件名猜版本：pkg-manager_v1.4.0-CN.elf → 1.4.0-CN（取 'v'+数字 开头串） */
static int pkgmgr_version_from_filename(const char *name, char *out, size_t osz) {
    if (!name || !name[0]) return -1;
    for (const char *p = name; *p; p++) {
        if ((*p == 'v' || *p == 'V') && p[1] >= '0' && p[1] <= '9') {
            size_t n = 0;
            const char *q = p + 1;
            while (*q && n + 1 < osz && ((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'z') ||
                                         (*q >= 'A' && *q <= 'Z') || *q == '.' || *q == '-'))
                out[n++] = *q++;
            out[n] = 0;
            if (n > 0) return 0;
        }
    }
    return -1;
}

static void pkgmgr_write_version_txt(const char *ver) {
    FILE *f = fopen(PKGMGR_VERSION_PATH, "w");
    if (f) {
        fprintf(f, "%s", ver);
        fclose(f);
    }
}

static int pkgmgr_read_version_txt(char *out, size_t osz) {
    FILE *f = fopen(PKGMGR_VERSION_PATH, "r");
    if (!f) return -1;
    if (!fgets(out, (int)osz, f)) {
        fclose(f);
        out[0] = 0;
        return -1;
    }
    fclose(f);
    out[strcspn(out, "\r\n")] = 0;
    return out[0] ? 0 : -1;
}

int pkgmanager_seed_if_needed(char *err, size_t esz) {
    struct stat st;
    err[0] = 0;
    /* 已有落盘文件（用户上传的更新或此前播种的基线）：绝不覆盖 */
    if (stat(PKGMGR_ELF_PATH, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0)
        return 0;

    size_t blob_len = (size_t)(pkgmanager_blob_end - pkgmanager_blob);
    if (blob_len < 4096) {
        snprintf(err, esz, "embedded blob looks invalid (%zu bytes)", blob_len);
        return -1;
    }
    pkgmgr_ensure_dir();
    int fd = open(PKGMGR_TMP_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (fd < 0) {
        snprintf(err, esz, "open tmp: %s", strerror(errno));
        return -1;
    }
    FILE *f = fdopen(fd, "wb");
    size_t w = fwrite(pkgmanager_blob, 1, blob_len, f);
    fclose(f);
    if (w != blob_len) {
        remove(PKGMGR_TMP_PATH);
        snprintf(err, esz, "short write %zu/%zu", w, blob_len);
        return -1;
    }
    if (rename(PKGMGR_TMP_PATH, PKGMGR_ELF_PATH) != 0) {
        remove(PKGMGR_TMP_PATH);
        snprintf(err, esz, "rename: %s", strerror(errno));
        return -1;
    }
    pkgmgr_write_version_txt(PKGMGR_EMBEDDED_VERSION);
    pldmgr_log("[PLDMGR] PKG Manager seeded v%s (%zu bytes) -> %s\n",
               PKGMGR_EMBEDDED_VERSION, blob_len, PKGMGR_ELF_PATH);
    return 0;
}

int pkgmanager_is_running(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return 0;
    struct timeval tv = {0, 400000}; /* loopback 建连很快，400ms 兜底 */
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PKGMGR_UI_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    int running = (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    close(sock);
    return running;
}

int pkgmanager_launch(char *msg, size_t msz) {
    char err[160] = "";
    if (pkgmanager_seed_if_needed(err, sizeof(err)) != 0) {
        snprintf(msg, msz, "%s", err);
        return -1;
    }
    if (ps5_launch_elf(PKGMGR_ELF_PATH) != 0) {
        snprintf(msg, msz, "elfldr launch failed (is elfldr running on port %d?)", ELFLDR_PORT);
        return -1;
    }
    snprintf(msg, msz, "OK");
    return 0;
}

/* [CN-FORK] 进程名判定：主匹配固定名 PKGMGR_PROC_NAME；兼容别名则要求同时含
 * "pkg" 与 "manager"/"mgr" 且以 .elf 结尾，避免误杀其它同名前缀的 payload。 */
static int pkgmgr_name_matches(const char *comm) {
    char lower[32];
    size_t i = 0;
    for (; i + 1 < sizeof(lower) && comm[i]; i++) {
        char c = comm[i];
        lower[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 'a') : c;
    }
    lower[i] = '\0';

    if (strcmp(lower, PKGMGR_PROC_NAME) == 0) return 1;

    const char *ext = strrchr(lower, '.');
    if (!ext || strcmp(ext, ".elf") != 0) return 0;
    if (!strstr(lower, "pkg")) return 0;
    return strstr(lower, "manager") || strstr(lower, "mgr") ? 1 : 0;
}

/* [CN-FORK] 关闭 PKG Manager：它是 elfldr 拉起的独立守护进程，本服务只能按 PID 杀。
 * 进程名依据：elfldr 拉起的 payload，其 kinfo_proc.ki_comm 就是 ELF 文件名本身
 *   （process_mgr.c 的 is_user_daemon() 正是靠 ".elf" 后缀识别守护进程、并硬排除
 *    "mini-syscore.elf"，可证）。本 fork 固定按 PKGMGR_ELF_PATH 落盘再拉起，文件名
 *    恒为 pkg-manager.elf（15 字符，不超 ki_comm 名限），因此按名匹配是确定做法。
 * 互杀安全：跳过自己与 PID<=1；历史上传起多个实例时会一次全部干掉。 */
int pkgmanager_stop(char *msg, size_t msz) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t buf_size = 0;

    if (sysctl(mib, 4, NULL, &buf_size, NULL, 0) != 0 || buf_size == 0) {
        snprintf(msg, msz, "sysctl KERN_PROC failed: %s", strerror(errno));
        return -1;
    }
    void *buf = malloc(buf_size);
    if (!buf) {
        snprintf(msg, msz, "OOM while enumerating processes");
        return -1;
    }
    if (sysctl(mib, 4, buf, &buf_size, NULL, 0) != 0) {
        free(buf);
        snprintf(msg, msz, "sysctl KERN_PROC failed: %s", strerror(errno));
        return -1;
    }

    pid_t self = getpid();
    int killed = 0, failed = 0;
    for (void *ptr = buf; ptr < (buf + buf_size);
         ptr += ((struct kinfo_proc *)ptr)->ki_structsize) {
        struct kinfo_proc *ki = (struct kinfo_proc *)ptr;
        if (ki->ki_structsize < sizeof(struct kinfo_proc)) break;
        if (ki->ki_pid == self || ki->ki_pid <= 1) continue;

        char comm[sizeof(ki->ki_comm) + 1];
        strncpy(comm, ki->ki_comm, sizeof(comm) - 1);
        comm[sizeof(comm) - 1] = '\0';
        if (!pkgmgr_name_matches(comm)) continue;

        if (kill(ki->ki_pid, SIGKILL) == 0) {
            killed++;
            pldmgr_log("[PLDMGR] PKG Manager: SIGKILL sent to pid %d (%s)\n",
                       (int)ki->ki_pid, comm);
        } else {
            failed++;
            pldmgr_log("[PLDMGR] PKG Manager: kill pid %d failed: %s\n",
                       (int)ki->ki_pid, strerror(errno));
        }
    }
    free(buf);

    if (killed > 0) {
        snprintf(msg, msz, "SIGKILL sent to %d process(es)", killed);
        return 0;
    }
    if (failed > 0) {
        snprintf(msg, msz, "kill failed for %d process(es)", failed);
        return -1;
    }
    if (!pkgmanager_is_running()) {
        snprintf(msg, msz, "PKG Manager was not running");
        return 0;
    }
    /* 端口在响但找不到进程：通常是上游版本自己改了 proctitle，不尝试广谱杀进程 */
    snprintf(msg, msz, "no pkg-manager process found while port %d is open", PKGMGR_UI_PORT);
    return -1;
}

void pkgmanager_status_json(char *out, size_t osz) {
    char inst[64] = "";
    if (pkgmgr_read_version_txt(inst, sizeof(inst)) != 0) {
        struct stat st;
        /* 文件在但 version.txt 缺失（历史遗留/手工放置）：显示未知 */
        snprintf(inst, sizeof(inst), "%s",
                 stat(PKGMGR_ELF_PATH, &st) == 0 ? "?" : "-");
    }
    snprintf(out, osz,
             "{\"running\":%s,\"port\":%d,\"embedded_version\":\"%s\","
             "\"installed_version\":\"%s\",\"file\":%s}",
             pkgmanager_is_running() ? "true" : "false", PKGMGR_UI_PORT,
             PKGMGR_EMBEDDED_VERSION, inst,
             access(PKGMGR_ELF_PATH, F_OK) == 0 ? "true" : "false");
}

int pkgmanager_commit_upload(const char *tmp_path, const char *filename_hint, char *msg, size_t msz) {
    struct stat st;
    if (stat(tmp_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        snprintf(msg, msz, "temp file missing");
        return -1;
    }
    /* 合理区间：当前基线约 2MB；下限挡误传的小文件，上限防内存失控 */
    if (st.st_size < 256 * 1024 || st.st_size > 16 * 1024 * 1024) {
        snprintf(msg, msz, "size %lld looks wrong for a PKG Manager ELF", (long long)st.st_size);
        remove(tmp_path);
        return -1;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)st.st_size);
    if (!buf) {
        snprintf(msg, msz, "OOM");
        remove(tmp_path);
        return -1;
    }
    int fd = open(tmp_path, O_RDONLY);
    ssize_t rd = fd >= 0 ? read(fd, buf, (size_t)st.st_size) : -1;
    if (fd >= 0) close(fd);
    if (rd != (ssize_t)st.st_size) {
        free(buf);
        snprintf(msg, msz, "read failed");
        remove(tmp_path);
        return -1;
    }
    if (!(buf[0] == 0x7f && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F')) {
        free(buf);
        snprintf(msg, msz, "not an ELF file");
        remove(tmp_path);
        return -1;
    }
    char ver[64] = "";
    if (pkgmanager_extract_version(buf, (size_t)st.st_size, ver, sizeof(ver)) != 0)
        ver[0] = 0;
    if (!ver[0] && pkgmgr_version_from_filename(filename_hint, ver, sizeof(ver)) != 0)
        ver[0] = 0;
    if (!ver[0])
        snprintf(ver, sizeof(ver), "unknown");
    /* 身份识别：签名串会被 PKGManager 自己的 --gc-sections 掉，不能硬要求；
     * 改用必然存在的日志格式串 "PKG Manager" 判定，防传错文件 */
    if (!find_bytes(buf, (size_t)st.st_size, "PKG Manager")) {
        free(buf);
        snprintf(msg, msz, "binary has no 'PKG Manager' strings (wrong file?)");
        remove(tmp_path);
        return -1;
    }
    free(buf);
    pkgmgr_ensure_dir();
    /* rename 原子覆盖：运行中的旧实例占着旧 inode 不受影响，重启后切新版 */
    if (rename(tmp_path, PKGMGR_ELF_PATH) != 0) {
        snprintf(msg, msz, "rename: %s", strerror(errno));
        remove(tmp_path);
        return -1;
    }
    pkgmgr_write_version_txt(ver);
    pldmgr_log("[PLDMGR] PKG Manager updated via upload -> v%s\n", ver);
    snprintf(msg, msz, "%s", ver);
    return 0;
}
