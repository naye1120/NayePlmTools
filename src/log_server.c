#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <errno.h>
#include <poll.h>     /* [CN-FORK] v0.5.2.122 log_stdout 的「可写才写」 */
#include <time.h>
#include <unistd.h>   /* [CN-FORK] v0.5.2.122 write() */
#include <sys/stat.h> /* [CN-FORK] 日志落盘需要 mkdir */
#include <stdatomic.h> /* 上游 v0.6.0 环形缓冲的原子序号 */
#include <inttypes.h>

#include "log_server.h"
#include "utils.h"
#include "json_helpers.h"

/* Ring buffer state */
static char log_buffer[MAX_LOG_LINES][MAX_LOG_LINE_LEN];
static int log_head = 0;
static int log_count = 0;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t log_cond = PTHREAD_COND_INITIALIZER;
static uint64_t log_next_sequence = 1;
static uint64_t stream_generation = 0;
static int stream_running = 0;

#ifndef PLDMGR_LOG_KEEPALIVE_MS
#define PLDMGR_LOG_KEEPALIVE_MS 5000
#endif

static _Atomic int server_active_flag = 0;

/* [CN-FORK] 日志落盘：实机死机排查之前完全没有证据可查 —— 日志只进内存环形缓冲 + SSE，
 * 一断电全没。这里追加写 /data/nayeplmtools/pldmgr.log 并带时间戳，每行 fflush，
 * 保证死机前最后一条还在。
 *
 * ⚠️ v0.5.2.123：**落盘改用独立的 file_mutex，不再挂在 log_mutex 上。**
 * 原版把 fopen/fprintf/fflush 全放在 log_mutex 里（注释还写着「调用方已持 log_mutex」），
 * 于是「一次慢盘写」＝「全进程的日志一起堵」：log_mutex 是所有 pldmgr_log 的必经之路，
 * 而 pldmgr_log 又长在后台打包 worker 的路径上 —— 盘一卡，打包线程就停在某一行的
 * 日志上不再往下走，界面看到的就是「阶段定住、字节一个不动」。
 * 这与 stdout 那条（见 log_stdout）是**同一类**的错：把一件可能无限期阻塞的 I/O
 * 放进了别人必须经过的临界区。环形缓冲与 SSE 的通知留在 log_mutex 里（纯内存，快），
 * 磁盘那份挪到锁外单独串行 —— 最坏情况只是日志文件落后，绝不再拖住任何人。 */
#define PLDMGR_LOG_PATH "/data/nayeplmtools/pldmgr.log"
#define PLDMGR_LOG_MAX_BYTES (512 * 1024)
static FILE *log_fp = NULL;
static long  log_bytes = 0;
static int   log_dir_tried = 0;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;

static void log_append_file(const char *line) {
    if (log_fp && log_bytes > PLDMGR_LOG_MAX_BYTES) {
        fclose(log_fp);
        log_fp = NULL;
        log_bytes = 0;
    }
    if (!log_fp) {
        if (!log_dir_tried) {
            log_dir_tried = 1;
            mkdir("/data/nayeplmtools", 0777);
        }
        log_fp = fopen(PLDMGR_LOG_PATH, log_bytes ? "a" : "w");
        if (!log_fp) return;
    }

    char ts[32] = "";
    time_t now = time(NULL);
    struct tm tmv;
    if (localtime_r(&now, &tmv) != NULL)
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    int w = fprintf(log_fp, "[%s] %s\n", ts, line);
    if (w > 0) log_bytes += w;
    fflush(log_fp);
}

void log_server_set_active(void) { server_active_flag = 1; }
int pldmgr_server_is_active(void) { return server_active_flag; }

/* [CN-FORK] v0.5.2.122 stdout 改成「可写才写」。
 *
 * payload 的 stdout 是**启动器给的 socket / 管道**（不是终端），对端不排空它。
 * 缓冲一满，`printf` 就**永久**阻塞 —— 而调用 pldmgr_log 的线程里有一个正好是
 * 后台打包线程。于是现象是：任务槽已经报出总量（体积 58.7 MB / 文件 0-3），
 * 此后一个字节都不动，阶段永远停在 "measuring"，用户看到的是一块不动的进度条。
 * 一条日志能卡死一个后台任务，代价完全不成比例。
 *
 * 所以先 poll 问一句「写得进去吗」：不可写就丢掉 stdout 这一份。日志的另外两个
 * 去处（内存环形缓冲给 /logs 与 SSE、以及落盘 /data/nayeplmtools/pldmgr.log）
 * 都在下面照旧，排查能力一点没少。 */
static void log_stdout(const char *line, size_t n) {
    if (!line || n == 0) return;
    struct pollfd p;
    p.fd = STDOUT_FILENO;
    p.events = POLLOUT;
    p.revents = 0;
    if (poll(&p, 1, 0) <= 0) return;
    if (!(p.revents & POLLOUT)) return;
    ssize_t r = write(STDOUT_FILENO, line, n);
    (void)r; /* 短写 / EAGAIN / 失败都无所谓：这不是日志的唯一去处 */
}

void pldmgr_log(const char *fmt, ...) {
    char line[MAX_LOG_LINE_LEN];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    /* Print to stdout（可写才写，见 log_stdout 的注释） */
    log_stdout(line, strlen(line));

    /* Remove trailing newline for internal storage if present */
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
        line[len - 1] = '\0';
        len--;
    }

    if (len == 0)
        return;

    /* Add to circular buffer with lock */
    pthread_mutex_lock(&log_mutex);
    strncpy(log_buffer[log_head], line, MAX_LOG_LINE_LEN);
    log_head = (log_head + 1) % MAX_LOG_LINES;
    if (log_count < MAX_LOG_LINES)
        log_count++;

    /* Signal SSE clients */
    log_next_sequence++;
    pthread_cond_broadcast(&log_cond);
    pthread_mutex_unlock(&log_mutex);

    /* 落盘：**锁外**，只与别的落盘串行（见 log_append_file 的注释）。
     * 放在 log_mutex 外面是这一版的关键改动 —— 磁盘 I/O 慢不能变成全进程的日志慢。 */
    pthread_mutex_lock(&file_mutex);
    log_append_file(line);
    pthread_mutex_unlock(&file_mutex);
}

void log_stream_start(void) {
    pthread_mutex_lock(&log_mutex);
    stream_generation++;
    stream_running = 1;
    pthread_cond_broadcast(&log_cond);
    pthread_mutex_unlock(&log_mutex);
}

void log_stream_stop(void) {
    pthread_mutex_lock(&log_mutex);
    stream_running = 0;
    pthread_cond_broadcast(&log_cond);
    pthread_mutex_unlock(&log_mutex);
}

void log_stream_init(struct LogSSEConnection *conn, const char *last_event_id) {
    memset(conn, 0, sizeof(*conn));
    pthread_mutex_lock(&log_mutex);
    conn->generation = stream_generation;
    conn->next_sequence = log_next_sequence - (uint64_t)log_count;
    /* Strict decimal parsing avoids accepting whitespace/signs or overflow. */
    if (last_event_id && *last_event_id) {
        uint64_t id = 0;
        int valid = 1;
        const char *p = last_event_id;
        while (*p >= '0' && *p <= '9') {
            unsigned digit = (unsigned)(*p++ - '0');
            if (id > (UINT64_MAX - digit) / 10) { valid = 0; break; }
            id = id * 10 + digit;
        }
        if (valid && !*p && id < log_next_sequence) conn->next_sequence = id + 1;
    }
    pthread_mutex_unlock(&log_mutex);
}

/* Snapshot a whole event before releasing the ring lock. Internal CR/LF text
 * needs a data prefix on every line to preserve one EventSource message. */
static void stage_log(struct LogSSEConnection *conn, const char *line) {
    size_t n = (size_t)snprintf(conn->pending, sizeof(conn->pending),
                              "id: %" PRIu64 "\ndata: ", conn->next_sequence);
    for (size_t i = 0; line[i]; i++) {
        if (line[i] == '\r' || line[i] == '\n') {
            if (line[i] == '\r' && line[i + 1] == '\n') i++;
            memcpy(conn->pending + n, "\ndata: ", 7); n += 7;
        } else conn->pending[n++] = line[i];
    }
    memcpy(conn->pending + n, "\n\n", 2);
    conn->pending_size = n + 2;
    conn->next_sequence++;
}

ssize_t log_stream_callback(void *cls, uint64_t pos, char *buf, size_t max) {
    (void)pos; /* MHD serializes forward reads for each response context. */
    struct LogSSEConnection *conn = cls;
    if (!max) return 0;
    pthread_mutex_lock(&log_mutex);
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += PLDMGR_LOG_KEEPALIVE_MS / 1000;
    deadline.tv_nsec += (PLDMGR_LOG_KEEPALIVE_MS % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) { deadline.tv_sec++; deadline.tv_nsec -= 1000000000L; }

    while (conn->pending_offset == conn->pending_size) {
        if (!stream_running || conn->generation != stream_generation) {
            pthread_mutex_unlock(&log_mutex);
            return MHD_CONTENT_READER_END_OF_STREAM;
        }
        conn->pending_offset = conn->pending_size = 0;
        uint64_t oldest = log_next_sequence - (uint64_t)log_count;
        if (conn->next_sequence < oldest) {
            conn->pending_size = (size_t)snprintf(conn->pending, sizeof(conn->pending),
                "event: gap\ndata: [PLDMGR] %" PRIu64 " log entries were overwritten before delivery\n\n",
                oldest - conn->next_sequence);
            conn->next_sequence = oldest;
            break;
        }
        if (conn->next_sequence < log_next_sequence) {
            int offset = (int)(conn->next_sequence - oldest);
            int idx = (log_head - log_count + offset + MAX_LOG_LINES) % MAX_LOG_LINES;
            stage_log(conn, log_buffer[idx]);
            break;
        }
        int wait_result = pthread_cond_timedwait(&log_cond, &log_mutex, &deadline);
        if (wait_result == ETIMEDOUT) {
            /* Recheck stop/generation even if timeout races a stop. */
            if (!stream_running || conn->generation != stream_generation) continue;
            memcpy(conn->pending, ": ping\n\n", 8);
            conn->pending_size = 8;
            break;
        }
        if (wait_result != 0) {
            pthread_mutex_unlock(&log_mutex);
            return MHD_CONTENT_READER_END_WITH_ERROR;
        }
    }
    /* Stop also terminates partially delivered frames; that connection is
     * closing, so a new generation must not revive any old response context. */
    if (!stream_running || conn->generation != stream_generation) {
        pthread_mutex_unlock(&log_mutex);
        return MHD_CONTENT_READER_END_OF_STREAM;
    }
    size_t remaining = conn->pending_size - conn->pending_offset;
    size_t copied = remaining < max ? remaining : max;
    memcpy(buf, conn->pending + conn->pending_offset, copied);
    conn->pending_offset += copied;
    pthread_mutex_unlock(&log_mutex);
    return (ssize_t)copied;
}

void log_stream_cleanup(void *cls) { free(cls); }

size_t log_build_json(char *buf, size_t buf_size) {
    JsonListBuilder jb = { buf, buf_size, 0, 1, 0 };
    json_append(&jb, "{\"logs\":[");

    pthread_mutex_lock(&log_mutex);
    for (int i = 0; i < log_count; i++) {
        int idx = (log_head - log_count + i + MAX_LOG_LINES) % MAX_LOG_LINES;
        char escaped[MAX_LOG_LINE_LEN * 6];
        json_escape_checked(&jb, log_buffer[idx], escaped, sizeof(escaped));
        json_append(&jb,
                         "\"%s\"%s", escaped,
                         (i == log_count - 1) ? "" : ",");
    }
    pthread_mutex_unlock(&log_mutex);

    json_append(&jb, "]}");
    return jb.pos;
}
