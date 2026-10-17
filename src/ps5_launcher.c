#include "ps5_launcher.h"
#include "process_mgr.h"
#include "pldmgr.h"
#include "slot.h"
#include "repository.h"
#include <limits.h>
#include "history_mgr.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <signal.h>
#include <stdint.h>
#include <errno.h>
#include <time.h>
#include <poll.h>

#ifndef PLDMGR_LOADER_CONNECT_MS
#define PLDMGR_LOADER_CONNECT_MS 5000
#endif
#ifndef PLDMGR_LOADER_TRANSFER_MS
#define PLDMGR_LOADER_TRANSFER_MS 30000
#endif
static long long launch_now_ms(void) {
    struct timespec time; clock_gettime(CLOCK_MONOTONIC, &time);
    return (long long)time.tv_sec*1000+time.tv_nsec/1000000;
}
static int launch_cancelled(PayloadLaunchCancelled callback, void *context) {
    if (callback && callback(context)) { errno=ECANCELED; return 1; }
    return 0;
}
static int wait_writable(int fd, long long deadline, PayloadLaunchCancelled cancelled, void *context) {
    for (;;) {
        if (launch_cancelled(cancelled,context)) return -1;
        long long left=deadline-launch_now_ms();
        if (left<=0) { errno=ETIMEDOUT; return -1; }
        struct pollfd socket={.fd=fd,.events=POLLOUT};
        int ready=poll(&socket,1,left>100 ? 100 : (int)left);
        if (ready>0) {
            if (socket.revents & (POLLERR|POLLHUP|POLLNVAL)) { errno=EIO; return -1; }
            if (socket.revents & POLLOUT) return 0;
        } else if (ready<0 && errno!=EINTR) return -1;
    }
}
static int launch_elf(const char *path, PayloadLaunchCancelled cancelled, void *context, int self_update) {
    if (launch_cancelled(cancelled,context)) return -1;
    char canonical[PATH_MAX];
    int fd=self_update ? slot_open_self_update(path) : slot_open_payload(path,canonical,sizeof(canonical));
    if (self_update && fd >= 0) {
        char version[64];
        if (repository_self_update_version_fd(fd, version, sizeof(version)) ||
            slot_version_compare(version, MENU_VERSION) <= 0) { close(fd); return -1; }
    }
    if (fd<0) { pldmgr_log("[PLDMGR] Payload path rejected: %s\n",path ? path : "(null)"); return -1; }
    int sock=socket(AF_INET,SOCK_STREAM,0);
    if (sock<0) { close(fd); return -1; }
    int flags=fcntl(sock,F_GETFL,0);
    int error=flags<0 || fcntl(sock,F_SETFL,flags|O_NONBLOCK)<0;
    struct sockaddr_in address={0};
    address.sin_family=AF_INET;
    address.sin_port=htons(ELFLDR_PORT);
    address.sin_addr.s_addr=inet_addr("127.0.0.1");
    long long deadline=launch_now_ms()+PLDMGR_LOADER_CONNECT_MS;
    if (!error && !launch_cancelled(cancelled,context)) {
        if (connect(sock,(struct sockaddr *)&address,sizeof(address))<0) {
            if (errno!=EINPROGRESS && errno!=EWOULDBLOCK && errno!=EINTR) error=1;
            else if (wait_writable(sock,deadline,cancelled,context)) error=1;
            else {
                int socket_error=0; socklen_t length=sizeof(socket_error);
                if (getsockopt(sock,SOL_SOCKET,SO_ERROR,&socket_error,&length) || socket_error) error=1;
            }
        }
    } else error=1;
    if (error) pldmgr_log("[PLDMGR] Loader connection failed, timed out, or cancelled.\n");
    size_t total=0;
    deadline=launch_now_ms()+PLDMGR_LOADER_TRANSFER_MS;
    char buffer[8192];
    while (!error) {
        if (launch_cancelled(cancelled,context) || launch_now_ms()>=deadline) { error=1; break; }
        ssize_t bytes=read(fd,buffer,sizeof(buffer));
        if (bytes==0) break;
        if (bytes<0) { if(errno==EINTR)continue; error=1; break; }
        size_t offset=0;
        while (offset<(size_t)bytes) {
            if (launch_cancelled(cancelled,context) || launch_now_ms()>=deadline) { error=1; break; }
#ifdef MSG_NOSIGNAL
            int send_flags=MSG_NOSIGNAL;
#else
            int send_flags=0; /* main also ignores SIGPIPE on PS5 */
#endif
            ssize_t sent=send(sock,buffer+offset,(size_t)bytes-offset,send_flags);
            if (sent>0) { offset+=(size_t)sent; total+=(size_t)sent; }
            else if (sent<0 && errno==EINTR) continue;
            else if (sent<0 && (errno==EAGAIN || errno==EWOULDBLOCK)) {
                if (wait_writable(sock,deadline,cancelled,context)) { error=1; break; }
            } else { error=1; break; }
        }
    }
    if (!total) error=1;
    close(sock); close(fd);
    if (!error && !self_update) history_mgr_add(canonical);
    pldmgr_log("[PLDMGR] Loader transfer %s (%zu bytes sent).\n",error ? "failed or cancelled" : "complete",total);
    return error ? -1 : 0;
}
int ps5_launch_elf_cancellable(const char *path, PayloadLaunchCancelled cancelled, void *context) {
    return launch_elf(path, cancelled, context, 0);
}
int ps5_launch_self_update(const char *path) { return launch_elf(path, NULL, NULL, 1); }
int ps5_launch_elf(const char *path) { return ps5_launch_elf_cancellable(path,NULL,NULL); }

extern int sceSystemServiceLaunchWebBrowser(const char *uri);

int ps5_launch_browser(const char *uri) {
  pldmgr_log("[PLDMGR] Launching browser: %s\n", uri);
  if (sceSystemServiceLaunchWebBrowser(uri) != 0) {
    pldmgr_notify("[PLDMGR] !!! Failed to launch browser.");
    return -1;
  }
  return 0;
}

/* LncUtil Externs */
extern int sceLncUtilGetAppIdOfRunningBigApp(void);
extern int sceLncUtilGetAppTitleId(uint32_t app_id, char *title_id);
extern int sceLncUtilSuspendApp(uint32_t app_id);
extern int sceLncUtilKillApp(uint32_t app_id);

int ps5_kill_disc_player(void) {
  const char *target_process = "SceDiscPlayer";
  const char *target_title_id = "NPXS40140";

  uint32_t app_id = sceLncUtilGetAppIdOfRunningBigApp();
  if (app_id == UINT32_MAX) {
    return 0; /* No app running */
  }

  char title_id[16] = {0};
  if (sceLncUtilGetAppTitleId(app_id, title_id) != 0) {
    return 0; /* Could not get title ID */
  }

  if (strcmp(title_id, target_title_id) != 0) {
    return 0; /* Not the disc player */
  }

  pldmgr_notify("Disc Player detected. Terminating...\n");

  /* 1. Suspend */
  if (sceLncUtilSuspendApp(app_id) != 0) {
    pldmgr_notify("Failed to suspend Disc Player");
    return -1;
  }

  /* Wait for home screen transition stability */
  sleep(2);

  /* 2. SIGKILL */
  pid_t pid = process_find_pid(target_process);
  if (pid != -1) {
    pldmgr_log("Sending SIGKILL to %s (PID: %d)\n", target_process, pid);
    if (kill(pid, SIGKILL) == 0) {
      pldmgr_log("SIGKILL Sent to Disc Player");
    } else {
      pldmgr_notify("Warning: SIGKILL Failed");
    }
    sleep(1);
  }

  /* 3. LncUtil Kill */
  int result = sceLncUtilKillApp(app_id);
  if (result == 0) {
    pldmgr_log("Disc Player Fully Closed");
  } else {
    /* Check if it's already gone (crashed/closed) */
    if ((uint32_t)sceLncUtilGetAppIdOfRunningBigApp() == UINT32_MAX) {
      pldmgr_log("Disc Player Closed");
    } else {
      pldmgr_notify("!!! Failed to kill Disc Player (result: %d)", result);
      return -1;
    }
  }

  return 0;
}
