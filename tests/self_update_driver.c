#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include "pldmgr.h"
#include "repository.h"
#include "sources.h"
#include "slot.h"
#include "payload_mgr.h"
#include "ps5_launcher.h"

static int sockets, history;
void pldmgr_log(const char *fmt, ...) { (void)fmt; }
int pldmgr_autoload_update_config_entry(const char *a, const char *b) { (void)a; (void)b; return 0; }
void history_mgr_add(const char *path) { (void)path; history++; }
int self_test_socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol; sockets++;
    return getenv("FAIL_LAUNCH") ? -1 : open("/dev/null", O_WRONLY);
}
int self_test_connect(int fd, const struct sockaddr *addr, socklen_t length) { (void)fd; (void)addr; (void)length; return 0; }
ssize_t self_test_send(int fd, const void *buf, size_t length, int flags) { (void)fd; (void)buf; (void)flags; return length; }
int self_test_renameat(int from, const char *src, int to, const char *dst) {
    if (getenv("FAIL_RENAME")) { errno = EIO; return -1; }
    return renameat(from, src, to, dst);
}
int self_test_fsync(int fd) {
    if (getenv("FAIL_SYNC")) { errno = EIO; return -1; }
    return fsync(fd);
}
int download_to_file(const char *url, const char *path) {
    /* Only local fixtures. Manifest refresh fails, leaving the cached fixture. */
    if (strcmp(url, "fixture://update")) return -1;
    const char *input = getenv("UPDATE_FIXTURE");
    FILE *in = input ? fopen(input, "rb") : NULL;
    if (!in) return -1;
    FILE *out = fopen(path, "wb");
    if (!out) { fclose(in); return -1; }
    char buf[4096]; size_t n; int rc = 0;
    while ((n = fread(buf, 1, sizeof(buf), in))) if (fwrite(buf, 1, n, out) != n) { rc = -1; break; }
    if (ferror(in)) rc = -1;
    fclose(in); if (fclose(out)) rc = -1;
    return rc;
}
int download_to_file_limited(const char *url, const char *path, size_t limit) { (void)limit; return download_to_file(url, path); }
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    char out[131072], path[1024], version[64];
    if (!strcmp(argv[1], "scan")) {
        int found = repository_check_self_update(path, sizeof(path), version, sizeof(version));
        printf("%d %s\n%s\n", found, version, path);
    } else if (!strcmp(argv[1], "install")) {
        if (argc < 3) return 2;
        sources_repo_install(argv[2], argc > 4 ? argv[4] : "pldmgr", argc > 3 ? argv[3] : NULL, NULL, out, sizeof(out));
        puts(out);
    } else if (!strcmp(argv[1], "list")) {
        if (!payload_mgr_list_json(out, sizeof(out))) return 3;
        puts(out);
    } else if (!strcmp(argv[1], "catalog")) {
        if (!sources_multi_repository_list_json(out, sizeof(out), 0)) return 3;
        puts(out);
    } else if (!strcmp(argv[1], "migrate")) {
        printf("%d %d\n", slot_storage_recover(), slot_migrate_legacy());
    } else if (!strcmp(argv[1], "launch") || !strcmp(argv[1], "self")) {
        if (argc < 3) return 2;
        int rc = !strcmp(argv[1], "self") ? ps5_launch_self_update(argv[2]) : ps5_launch_elf(argv[2]);
        printf("%d %d %d\n", rc, sockets, history);
    } else if (!strcmp(argv[1], "guards")) {
        if (argc < 4) return 2;
        int check = payload_mgr_check_existing(argv[2], out, sizeof(out));
        int upload = payload_mgr_import_to_storage_mode(argv[2], argv[3], "new", "web_upload", NULL, out, sizeof(out), path, sizeof(path));
        int usb_check = payload_mgr_usb_check(argv[3], out, sizeof(out));
        int usb_move = payload_mgr_usb_move(argv[3], "new", 0, out, sizeof(out));
        printf("%d %d %d %d\n", check, upload, usb_check, usb_move);
    } else return 2;
    return 0;
}
