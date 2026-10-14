#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "log_server.h"
#include "pldmgr.h"

static FILE *result;
static unsigned char output[262144];
static size_t used;
static uint64_t position;
static void emit(int i) { pldmgr_log("entry %03d %0180d\n", i, i); }
static void init(struct LogSSEConnection *conn, const char *id) {
#ifdef LEGACY_SSE
    (void)id; memset(conn, 0, sizeof(*conn));
#else
    log_stream_init(conn, id);
#endif
}
static ssize_t read_part(struct LogSSEConnection *conn, size_t capacity) {
    unsigned char *guard = malloc(capacity + 2); assert(guard);
    memset(guard, 0xa5, capacity + 2);
    ssize_t n = log_stream_callback(conn, position, (char *)guard + 1, capacity);
    assert(guard[0] == 0xa5 && guard[capacity + 1] == 0xa5);
    if (n > 0) {
        assert((size_t)n <= capacity && used + (size_t)n <= sizeof(output));
        memcpy(output + used, guard + 1, (size_t)n); used += (size_t)n; position += (uint64_t)n;
    }
    free(guard); return n;
}
static void event(struct LogSSEConnection *conn, size_t capacity) {
    size_t start = used;
    do { assert(read_part(conn, capacity) > 0); }
    while (used < start + 2 || output[used - 1] != '\n' || output[used - 2] != '\n');
}
static void show(const char *label) {
    fprintf(result, "%s ", label);
    for (size_t i = 0; i < used; i++) fprintf(result, "%02x", output[i]);
    fputc('\n', result); used = 0; position = 0;
}
#ifndef LEGACY_SSE
static void *idle_reader(void *arg) {
    struct LogSSEConnection *conn = arg;
    char buf[64];
    return (void *)(intptr_t)log_stream_callback(conn, 0, buf, sizeof(buf));
}
static void *producer(void *arg) {
    (void)arg;
    for (int i = 0; i < 80; i++) { emit(i); usleep(1000); }
    return NULL;
}
static void *consumer(void *arg) {
    struct LogSSEConnection *conn = arg;
    char buf[7], frame[MAX_LOG_SSE_FRAME]; size_t completed = 0, size = 0;
    while (completed < 80) {
        ssize_t n = log_stream_callback(conn, 0, buf, sizeof(buf)); assert(n > 0 && n <= 7);
        for (ssize_t i = 0; i < n; i++) {
            assert(size + 1 < sizeof(frame)); frame[size++] = buf[i];
            if (size >= 2 && frame[size - 1] == '\n' && frame[size - 2] == '\n') {
                frame[size] = 0;
                if (strncmp(frame, ": ping", 6)) {
                    unsigned long long id; int entry;
                    assert(sscanf(frame, "id: %llu\ndata: entry %d", &id, &entry) == 2);
                    assert(id == completed + 1 && entry == (int)completed); completed++;
                }
                size = 0;
            }
        }
    }
    return NULL;
}
#endif
int main(int argc, char **argv) {
    assert(argc >= 2);
    result = fdopen(dup(STDOUT_FILENO), "w"); assert(result);
    assert(freopen("/dev/null", "w", stdout));
    const char *scenario = argv[1];
    size_t capacity = argc > 2 ? (size_t)atoi(argv[2]) : 7;
    struct LogSSEConnection a, b;
#ifndef LEGACY_SSE
    log_stream_start();
#endif
    if (!strcmp(scenario, "catchup")) {
        for (int i = 0; i < 80; i++) emit(i);
        init(&a, NULL);
        for (int i = 0; i < 80; i++) event(&a, capacity);
        show("RESULT");
    } else if (!strcmp(scenario, "burst")) {
        init(&a, NULL); init(&b, NULL);
        for (int i = 0; i < 90; i++) emit(i);
        for (int i = 0; i < 90; i++) event(&a, capacity);
        show("RESULT");
        for (int i = 0; i < 90; i++) event(&b, 1);
        show("SECOND");
    } else if (!strcmp(scenario, "rollover")) {
        init(&a, NULL);
        for (int i = 0; i < 140; i++) emit(i);
        event(&a, capacity); // Explicit gap precedes all retained entries.
        for (int i = 40; i < 140; i++) event(&a, capacity);
        show("RESULT");
    } else if (!strcmp(scenario, "partial-rollover")) {
        emit(0); init(&a, NULL); assert(read_part(&a, 1) == 1);
        for (int i = 1; i < 141; i++) emit(i);
        event(&a, capacity); event(&a, capacity);
        for (int i = 41; i < 141; i++) event(&a, capacity);
        show("RESULT");
    } else if (!strcmp(scenario, "multiline")) {
        pldmgr_log("first\r\n\nlast\revent: injected\ndata: text\n");
        init(&a, NULL); event(&a, capacity); show("RESULT");
    } else if (!strcmp(scenario, "newlines")) {
        char text[256]; memset(text, '\n', 255); text[254] = 'X'; text[255] = 0;
        pldmgr_log("%s", text); init(&a, NULL); event(&a, capacity); show("RESULT");
    } else if (!strcmp(scenario, "ping")) {
        init(&a, NULL); event(&a, capacity); show("RESULT");
    } else if (!strcmp(scenario, "resume")) {
        for (int i = 0; i < 5; i++) emit(i);
        init(&a, "3"); for (int i = 3; i < 5; i++) event(&a, capacity); show("RESULT");
    } else if (!strcmp(scenario, "id")) {
        emit(0); init(&a, argv[3]); event(&a, capacity); show("RESULT");
#ifndef LEGACY_SSE
    } else if (!strcmp(scenario, "stop")) {
        init(&a, NULL); pthread_t reader; assert(!pthread_create(&reader, NULL, idle_reader, &a));
        usleep(20000); struct timespec start, finish; clock_gettime(CLOCK_MONOTONIC, &start);
        log_stream_stop(); void *value; assert(!pthread_join(reader, &value)); clock_gettime(CLOCK_MONOTONIC, &finish);
        assert((intptr_t)value == MHD_CONTENT_READER_END_OF_STREAM);
        assert((finish.tv_sec - start.tv_sec) * 1000 + (finish.tv_nsec - start.tv_nsec) / 1000000 < 500);
        log_stream_start(); assert(read_part(&a, capacity) == MHD_CONTENT_READER_END_OF_STREAM);
        emit(0); init(&b, NULL); event(&b, capacity); show("RESULT");
    } else if (!strcmp(scenario, "partial-stop")) {
        emit(0); init(&a, NULL); assert(read_part(&a, 1) == 1); log_stream_stop();
        assert(read_part(&a, capacity) == MHD_CONTENT_READER_END_OF_STREAM);
        log_stream_start(); assert(read_part(&a, capacity) == MHD_CONTENT_READER_END_OF_STREAM);
        used = 0; init(&b, NULL); event(&b, capacity); show("RESULT");
    } else if (!strcmp(scenario, "concurrent")) {
        init(&a, NULL); init(&b, NULL); pthread_t p, c1, c2;
        assert(!pthread_create(&p, NULL, producer, NULL));
        assert(!pthread_create(&c1, NULL, consumer, &a)); assert(!pthread_create(&c2, NULL, consumer, &b));
        pthread_join(p, NULL); pthread_join(c1, NULL); pthread_join(c2, NULL);
    } else if (!strcmp(scenario, "zero-capacity")) {
        init(&a, NULL); assert(read_part(&a, 0) == 0); emit(0); event(&a, capacity); show("RESULT");
#else
    } else if (!strcmp(scenario, "legacy-catchup")) {
        for (int i = 0; i < 80; i++) emit(i);
        init(&a, NULL); read_part(&a, 1024); emit(80); read_part(&a, 1024); show("RESULT");
    } else if (!strcmp(scenario, "legacy-small")) {
        init(&a, NULL); emit(0); read_part(&a, 1024); emit(1);
        // The old live callback's snprintf return exceeds the provided buffer.
        char out[1]; ssize_t n = log_stream_callback(&a, 0, out, sizeof(out)); assert(n > 1);
        fprintf(result, "OVERSIZE %zd\n", n);
    } else if (!strcmp(scenario, "legacy-ping")) {
        init(&a, NULL); read_part(&a, 1);
#endif
    } else assert(0);
#ifndef LEGACY_SSE
    log_stream_stop();
#endif
    fclose(result);
}
