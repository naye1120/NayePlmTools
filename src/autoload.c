#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include "pldmgr.h"
#include "autoload.h"
#include "config.h"
#include "json_helpers.h"
#include "slot.h"
#include "ps5_launcher.h"

#ifndef PLDMGR_FRONTEND_WAIT_MS
#define PLDMGR_FRONTEND_WAIT_MS 5000
#endif
#define MAX_SEQUENCE_TEXT 4095
#define MAX_ENTRY_TEXT 511
#define MAX_ENTRY_DELAY_MS 3600000

/* Ownership lock serializes start/stop/join; worker uses only state_mutex.
 * Never hold state_mutex during join, filesystem access, notifications or launch. */
static pthread_mutex_t lifecycle_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t state_cond = PTHREAD_COND_INITIALIZER;
static pthread_t worker_thread;
static int owned, running, cancel_requested, frontend_ready;
static long long countdown_end, entry_delay_end;
static AutoloadSnapshot status = {.state=AUTOLOAD_DISABLED, .remaining=-1, .remaining_ms=-1000,.entry_index=-1};

typedef struct { char key[MAX_ENTRY_TEXT + 1]; int delay_ms; } SequenceEntry;

static long long now_ms(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (long long)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}
const char *pldmgr_autoload_state_name(AutoloadState state) {
    const char *names[] = {"disabled", "waiting", "countdown", "executing", "done", "aborted", "failed"};
    return state >= AUTOLOAD_DISABLED && state <= AUTOLOAD_FAILED ? names[state] : "failed";
}
void pldmgr_autoload_get_snapshot(AutoloadSnapshot *out) {
    if (!out) return;
    pthread_mutex_lock(&state_mutex);
    *out = status;
    if (status.state == AUTOLOAD_COUNTDOWN) {
        out->remaining_ms = countdown_end - now_ms();
        if (out->remaining_ms < 0) out->remaining_ms = 0;
        out->remaining = (int)((out->remaining_ms + 999) / 1000);
    }
    if (status.state == AUTOLOAD_EXECUTING && entry_delay_end) {
        out->entry_remaining_ms = entry_delay_end - now_ms();
        if (out->entry_remaining_ms < 0) out->entry_remaining_ms = 0;
    }
    pthread_mutex_unlock(&state_mutex);
}
int pldmgr_autoload_get_remaining_seconds(void) {
    AutoloadSnapshot snapshot; pldmgr_autoload_get_snapshot(&snapshot); return snapshot.remaining;
}
long long pldmgr_autoload_get_remaining_ms(void) {
    AutoloadSnapshot snapshot; pldmgr_autoload_get_snapshot(&snapshot); return snapshot.remaining_ms;
}
void pldmgr_autoload_get_status(int *total, int *done, char *current) {
    AutoloadSnapshot snapshot; pldmgr_autoload_get_snapshot(&snapshot);
    if (total) *total=snapshot.total;
    if (done) *done=snapshot.done;
    if (current) memcpy(current, snapshot.current, sizeof(snapshot.current));
}
void pldmgr_autoload_frontend_ready(void) {
    pthread_mutex_lock(&state_mutex);
    frontend_ready=1;
    pthread_cond_broadcast(&state_cond);
    pthread_mutex_unlock(&state_mutex);
}
static int cancelled(void *context) {
    (void)context;
    pthread_mutex_lock(&state_mutex);
    int result=cancel_requested;
    pthread_mutex_unlock(&state_mutex);
    return result;
}
/* Timed condition waits allow prompt stop without changing countdown deadlines. */
static int wait_until(long long end, int waiting_frontend) {
    pthread_mutex_lock(&state_mutex);
    while (!cancel_requested && !(waiting_frontend && frontend_ready) && now_ms() < end) {
        struct timespec wake;
        clock_gettime(CLOCK_REALTIME, &wake);
        wake.tv_nsec += 100000000;
        if (wake.tv_nsec >= 1000000000) { wake.tv_sec++; wake.tv_nsec -= 1000000000; }
        pthread_cond_timedwait(&state_cond, &state_mutex, &wake);
    }
    int result=cancel_requested;
    pthread_mutex_unlock(&state_mutex);
    return result;
}
/* Parse the exact sequence once. Counters and UI list derive from these entries,
 * not a second pass with different whitespace/delay rules or a newer file. */
static int read_sequence(SequenceEntry **out, size_t *count, char list[4096]) {
    *out=NULL; *count=0; list[0]=0;
    int fd=open(AUTOLOAD_CONFIG_PATH,O_RDONLY|O_NONBLOCK);
    if (fd<0) return errno==ENOENT ? 1 : -1;
    struct stat info;
    if (fstat(fd,&info) || !S_ISREG(info.st_mode) || info.st_size<0 || info.st_size>MAX_SEQUENCE_TEXT) {
        close(fd); return -1;
    }
    FILE *file=fdopen(fd,"rb");
    if (!file) { close(fd); return -1; }
    char text[MAX_SEQUENCE_TEXT+1];
    size_t size=fread(text,1,sizeof(text),file);
    int failed=ferror(file);
    if (fclose(file)) failed=1;
    if (failed || size>MAX_SEQUENCE_TEXT || memchr(text,0,size)) return -1;
    text[size]=0;
    /* [CN-FORK] v0.6.0.176 剥掉 UTF-8 BOM。实机这份 autoload.txt 常被 Windows
     * 记事本另存成带 BOM，上游的 isspace() 首尾裁剪管不到 BOM（0xEF 不是空白字符），
     * 结果 BOM 会被拼进第一个载荷名 → 路径拼不出来、必报 Payload not found。
     * 行首缩进与 CRLF 上游已经处理（isspace + 拒收内嵌 \r），这里只补 BOM 这一项。 */
    if (size >= 3 && (unsigned char)text[0] == 0xEF &&
        (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) {
        memmove(text, text + 3, size - 3 + 1);
        size -= 3;
    }
    size_t capacity=1;
    for (size_t i=0;i<size;i++) if (text[i]=='\n') capacity++;
    SequenceEntry *entries=calloc(capacity, sizeof(*entries));
    if (!entries) return -1;
    char *save=NULL; size_t pos=0;
    for (char *line=strtok_r(text,"\n",&save);line;line=strtok_r(NULL,"\n",&save)) {
        while (*line && isspace((unsigned char)*line)) line++;
        char *end=line+strlen(line);
        while (end>line && isspace((unsigned char)end[-1])) *--end=0;
        size_t length=strlen(line);
        if (!length) continue;
        if (length > MAX_ENTRY_TEXT || strpbrk(line,"\r,")) goto invalid;
        SequenceEntry *entry=&entries[*count];
        entry->delay_ms=-1;
        if (*line=='!') {
            if (length==1) goto invalid;
            long delay=0;
            for (const char *p=line+1;*p;p++) {
                if (*p<'0' || *p>'9' || delay > MAX_ENTRY_DELAY_MS/10) goto invalid;
                delay=delay*10+*p-'0';
                if (delay > MAX_ENTRY_DELAY_MS) goto invalid;
            }
            entry->delay_ms=(int)delay;
        }
        size_t needed=length+(pos?1:0);
        if (needed >= 4096-pos) goto invalid;
        if (pos) list[pos++]=',';
        memcpy(list+pos,line,length); pos+=length; list[pos]=0;
        memcpy(entry->key,line,length+1);
        (*count)++;
    }
    *out=entries; return 0;
invalid:
    free(entries); *count=0; list[0]=0; return -1;
}
static void set_current(const char *key) {
    size_t length=strlen(key);
    if (length>=sizeof(status.current)) {
        length=sizeof(status.current)-1;
        while (length && ((unsigned char)key[length]&0xc0)==0x80) length--;
    }
    memcpy(status.current,key,length); status.current[length]=0;
}
static void finish(AutoloadState state) {
    pthread_mutex_lock(&state_mutex);
    if (entry_delay_end) {
        status.entry_remaining_ms=entry_delay_end-now_ms();
        if (status.entry_remaining_ms<0) status.entry_remaining_ms=0;
    }
    entry_delay_end=0;
    status.state=cancel_requested ? AUTOLOAD_ABORTED : state;
    status.remaining=status.state==AUTOLOAD_DONE ? 0 : -1;
    status.remaining_ms=(long long)status.remaining*1000;
    snprintf(status.current,sizeof(status.current),"%s",status.state==AUTOLOAD_DONE ? "DONE" : "");
    countdown_end=0;
    running=0;
    pthread_cond_broadcast(&state_cond);
    pthread_mutex_unlock(&state_mutex);
}
static void *autoload_worker(void *argument) {
    (void)argument;
    SequenceEntry *entries=NULL; size_t count=0;
    char list[4096]={0};
    PldmgrConfig cfg; config_read(&cfg);
    if (!cfg.autoload_enabled) { finish(AUTOLOAD_DISABLED); return NULL; }
    int parsed=read_sequence(&entries,&count,list);
    if (parsed) {
        if (parsed<0) pldmgr_log("[Autoload] Invalid or unreadable sequence; no payloads launched.\n");
        finish(parsed>0 ? AUTOLOAD_DISABLED : AUTOLOAD_FAILED); return NULL;
    }
    pthread_mutex_lock(&state_mutex);
    status.delay=cfg.autoload_delay;
    status.scan_usb_payloads=cfg.scan_usb_payloads;
    memcpy(status.list,list,sizeof(list));
    for (size_t i=0;i<count;i++) if (entries[i].delay_ms<0) status.outcomes[status.total++]='p';
    status.state=AUTOLOAD_WAITING;
    status.remaining=cfg.autoload_delay;
    status.remaining_ms=(long long)cfg.autoload_delay*1000;
    pthread_mutex_unlock(&state_mutex);
    if (cfg.auto_browser_open) {
        pldmgr_log("[Autoload] Browser Mode: Waiting for frontend connection...\n");
        if (wait_until(now_ms()+PLDMGR_FRONTEND_WAIT_MS,1)) goto aborted;
    }
    if (cancelled(NULL)) goto aborted;
    pthread_mutex_lock(&state_mutex);
    status.state=AUTOLOAD_COUNTDOWN;
    countdown_end=now_ms()+(long long)cfg.autoload_delay*1000;
    long long end=countdown_end;
    pthread_mutex_unlock(&state_mutex);

    /* Preserve the existing PS-button mode, prompt/drain ordering and per-read
     * matching. The user explicitly withdrew the separate detection changes. */
    int klog_fd=-1;
    if (cfg.autoload_delay>0 && !cfg.auto_browser_open) {
        if (!pldmgr_server_is_active()) {
            char ip[64];
            if (pldmgr_get_local_ip(ip,sizeof(ip))!=0) strcpy(ip,"0.0.0.0");
            pldmgr_notify("NayePlmTools Running\nhttp://%s:%d",ip,MENU_PORT); /* [CN-FORK] 本 fork 的产品名 */
            pldmgr_notify("Autoloading in %ds\nPress PS Button to Abort",cfg.autoload_delay);
        }
        klog_fd=open("/dev/klog",O_RDONLY|O_NONBLOCK);
        if (klog_fd>=0) {
            char flush[4096];
            while (read(klog_fd,flush,sizeof(flush))>0) {
                if (cancelled(NULL)) { close(klog_fd); goto aborted; }
            }
        }
        pldmgr_log("[Autoload] Fallback Mode: Starting %ds countdown (PS Button active)...\n",cfg.autoload_delay);
    } else if (cfg.autoload_delay>0) {
        pldmgr_log("[Autoload] Browser Mode: Starting %ds countdown...\n",cfg.autoload_delay);
    }
    while (now_ms()<end) {
        if (cancelled(NULL)) { if(klog_fd>=0)close(klog_fd); goto aborted; }
        if (klog_fd>=0) {
            char log[2048]; ssize_t n=read(klog_fd,log,sizeof(log)-1);
            if (n>0) {
                log[n]=0;
                if (strstr(log,"onPSButtonPressed")) {
                    pldmgr_log("[Autoload] ABORTED via PS Button.\n");
                    pldmgr_notify("Autoload Aborted");
                    pldmgr_autoload_abort(); close(klog_fd); goto aborted;
                }
            }
        }
        long long tick=now_ms()+100;
        if (tick>end) tick=end;
        if (wait_until(tick,0)) { if(klog_fd>=0)close(klog_fd); goto aborted; }
    }
    if (klog_fd>=0) close(klog_fd);
    if (cancelled(NULL)) goto aborted;
    pthread_mutex_lock(&state_mutex);
    status.state=AUTOLOAD_EXECUTING; status.remaining=0; status.remaining_ms=0; countdown_end=0;
    pthread_mutex_unlock(&state_mutex);
    pldmgr_log("[Autoload] Starting sequence...\n");
    int payload_index=0;
    for (size_t i=0;i<count;i++) {
        if (cancelled(NULL)) goto aborted;
        SequenceEntry *entry=&entries[i];
        if (entry->delay_ms>=0) {
            long long deadline=now_ms()+entry->delay_ms;
            pthread_mutex_lock(&state_mutex);
            status.entry_index=(int)i;
            status.entry_duration_ms=entry->delay_ms;
            status.entry_remaining_ms=entry->delay_ms;
            entry_delay_end=deadline;
            snprintf(status.current,sizeof(status.current),"DELAY");
            pthread_mutex_unlock(&state_mutex);
            if (wait_until(deadline,0)) goto aborted;
            pthread_mutex_lock(&state_mutex);
            status.entry_remaining_ms=0; entry_delay_end=0;
            pthread_mutex_unlock(&state_mutex);
            continue;
        }
        pthread_mutex_lock(&state_mutex);
        status.entry_index=(int)i;
        status.entry_duration_ms=0; status.entry_remaining_ms=0; entry_delay_end=0;
        set_current(entry->key);
        pthread_mutex_unlock(&state_mutex);
        int skipped=0, failed=0;
        char path[512];
        if (is_pldmgr_name(entry->key)) skipped=1;
        else if (slot_resolve(entry->key,path,sizeof(path))) failed=1;
        else if (is_pldmgr_name(path)) skipped=1;
        else {
            pldmgr_log("[Autoload] Launching: %s\n",path);
            if (ps5_launch_elf_cancellable(path,cancelled,NULL)) failed=1;
        }
        if (skipped) pldmgr_log("[Autoload] Skipping self: %s\n",entry->key);
        if (failed) pldmgr_log("[Autoload] Missing, failed or cancelled payload: %s\n",entry->key);
        pthread_mutex_lock(&state_mutex);
        status.done++;
        status.outcomes[payload_index++]=skipped ? 'k' : failed ? 'f' : 's';
        status.skipped+=skipped;
        status.failed+=failed;
        status.successful+=!skipped && !failed;
        pthread_mutex_unlock(&state_mutex);
        if (cancelled(NULL)) goto aborted;
        if (wait_until(now_ms()+500,0)) goto aborted;
    }
    free(entries); finish(AUTOLOAD_DONE);
    pldmgr_log("[Autoload] Sequence complete.\n"); return NULL;
aborted:
    free(entries); finish(AUTOLOAD_ABORTED);
    pldmgr_log("[Autoload] Sequence aborted.\n"); return NULL;
}
int pldmgr_autoload_start(void) {
    pthread_mutex_lock(&lifecycle_mutex);
    if (owned) { pthread_mutex_unlock(&lifecycle_mutex); return 0; }
    pthread_mutex_lock(&state_mutex);
    status=(AutoloadSnapshot){.state=AUTOLOAD_WAITING,.remaining=-1,.remaining_ms=-1000,.entry_index=-1};
    cancel_requested=0; frontend_ready=0; countdown_end=0; entry_delay_end=0; running=1;
    int result=pthread_create(&worker_thread,NULL,autoload_worker,NULL);
    if (result) { running=0;status.state=AUTOLOAD_FAILED; }
    else owned=1;
    pthread_mutex_unlock(&state_mutex);
    pthread_mutex_unlock(&lifecycle_mutex);
    if (result) pldmgr_log("[Autoload] Failed to create worker: %s\n",strerror(result));
    return result ? -1 : 0;
}
void pldmgr_autoload_abort(void) {
    pthread_mutex_lock(&state_mutex);
    if (running) { cancel_requested=1; pthread_cond_broadcast(&state_cond); }
    pthread_mutex_unlock(&state_mutex);
}
void pldmgr_autoload_stop(void) {
    pthread_mutex_lock(&lifecycle_mutex);
    if (owned) {
        pldmgr_autoload_abort();
        pthread_join(worker_thread,NULL);
        owned=0;
    }
    pthread_mutex_unlock(&lifecycle_mutex);
}
void pldmgr_autoload_reset(void) {
    pthread_mutex_lock(&state_mutex);
    if (!running) {
        status=(AutoloadSnapshot){.state=AUTOLOAD_DISABLED,.remaining=-1,.remaining_ms=-1000,.entry_index=-1};
        countdown_end=0; entry_delay_end=0;
    }
    pthread_mutex_unlock(&state_mutex);
}
int pldmgr_autoload_update_config_entry(const char *old_key,const char *new_key) {
    int result=config_update_list_entry(AUTOLOAD_CONFIG_PATH,old_key,new_key);
    if (result) pldmgr_log("[Autoload] Failed to update entry: %s\n",old_key ? old_key : "");
    return result;
}
