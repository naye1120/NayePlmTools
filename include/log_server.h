#ifndef LOG_SERVER_H
#define LOG_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <microhttpd.h>

#define MAX_LOG_LINES 100
#define MAX_LOG_LINE_LEN 256

/* SSE log stream connection state */
#define MAX_LOG_SSE_FRAME (7 * MAX_LOG_LINE_LEN + 96)
struct LogSSEConnection {
    uint64_t next_sequence;
    uint64_t generation;
    size_t pending_size;
    size_t pending_offset;
    char pending[MAX_LOG_SSE_FRAME];
};

/* Start a fresh HTTP stream generation; stop wakes readers before MHD joins.
 * Logs remain in the ring across HTTP restarts. */
void log_stream_start(void);
void log_stream_stop(void);
/* Initialize once before publishing the response. Optional Last-Event-ID
 * resumes a retained cursor; invalid/future values replay retained history. */
void log_stream_init(struct LogSSEConnection *conn, const char *last_event_id);

/* MHD content reader callback for SSE log streaming */
ssize_t log_stream_callback(void *cls, uint64_t pos, char *buf, size_t max);

/* MHD free callback for SSE connections */
void log_stream_cleanup(void *cls);

/* Build complete /log JSON. Returns bytes written, or 0 on serialization failure. */
size_t log_build_json(char *buf, size_t buf_size);

/* Track whether any browser client has connected */
void log_server_set_active(void);
/* pldmgr_server_is_active() and pldmgr_log() are declared in pldmgr.h */

#endif
