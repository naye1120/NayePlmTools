#ifndef PROCESS_RECORDS_H
#define PROCESS_RECORDS_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>

/* Pin the SDK layout already used on hardware. Offset 447 is ki_comm, not
 * ki_tdname. A changed SDK must be checked before accepting a different ABI. */
_Static_assert(offsetof(struct kinfo_proc, ki_pid) == 72, "check process PID ABI");
_Static_assert(offsetof(struct kinfo_proc, ki_comm) == 447, "check process name ABI");
_Static_assert(offsetof(struct kinfo_proc, ki_rssize) == 264, "check process RSS ABI");
_Static_assert(sizeof(((struct kinfo_proc *)0)->ki_comm) == 20, "check process name size");
_Static_assert(sizeof(((struct kinfo_proc *)0)->ki_pid) == 4, "check process PID size");
_Static_assert(sizeof(((struct kinfo_proc *)0)->ki_rssize) == 8, "check process RSS size");
_Static_assert(offsetof(struct kinfo_proc, ki_structsize) == 0 && sizeof(int) == 4,
               "check process size header ABI");

typedef struct {
    pid_t pid;
    int64_t rss_pages;
    char name[sizeof(((struct kinfo_proc *)0)->ki_comm)];
} ProcessRecord;

/* Return 1 for one validated record, 0 at end, -1 for malformed bytes.
 * Copy fields instead of casting: the next variable-sized record may be
 * unaligned. Never advance until all consumed fields fit in this record. */
static inline int process_record_next(const unsigned char **cursor,
                                      size_t *remaining, ProcessRecord *record) {
    if (!*remaining) return 0;
    if (*remaining < sizeof(int)) return -1;
    int size;
    memcpy(&size, *cursor, sizeof(size));
    size_t minimum = offsetof(struct kinfo_proc, ki_comm) + sizeof(record->name);
    if (size <= 0 || (size_t)size < minimum || (size_t)size > *remaining) return -1;
    memcpy(&record->pid, *cursor + offsetof(struct kinfo_proc, ki_pid), sizeof(record->pid));
    memcpy(&record->rss_pages, *cursor + offsetof(struct kinfo_proc, ki_rssize), sizeof(record->rss_pages));
    memcpy(record->name, *cursor + offsetof(struct kinfo_proc, ki_comm), sizeof(record->name));
    if (!memchr(record->name, '\0', sizeof(record->name)) || record->rss_pages < 0) return -1;
    *cursor += (size_t)size;
    *remaining -= (size_t)size;
    return 1;
}

#endif
