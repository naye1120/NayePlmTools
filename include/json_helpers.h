#ifndef JSON_HELPERS_H
#define JSON_HELPERS_H

#include <stddef.h>

/* Incremental JSON array/object builder */
typedef struct JsonListBuilder {
    char *buf;
    size_t size;
    size_t pos;
    int first;
    int error;
} JsonListBuilder;

/* Overflow/format/escape errors are sticky: discard the whole output (pos=0,
 * terminated empty buffer when available), and all subsequent appends fail. */
int json_append(JsonListBuilder *jb, const char *fmt, ...);
int json_builder_fail(JsonListBuilder *jb);
int json_escape_checked(JsonListBuilder *jb, const char *src, char *dst, size_t size);
/* Single formatted response: returns bytes written, or 0 with empty output on
 * overflow/format failure. Successful JSON is always nonempty. */
size_t json_format(char *buf, size_t size, const char *fmt, ...);

enum {
    JSON_STRING_OK = 0,
    JSON_STRING_TRUNCATED = 1,
    JSON_STRING_MISSING = -1,
    JSON_STRING_INVALID = -2,
    JSON_STRING_TOO_LONG = -3
};

/* Extract a direct member of one complete object in [obj_start, obj_end).
 * Decode all JSON escapes/Unicode, validating the entire object (max depth 64).
 * Duplicate requested keys, non-string values, invalid UTF-8, and decoded NUL
 * in the requested value are invalid. Errors clear out when capacity is nonzero.
 * Strict extraction never returns a partial identifier/URL/checksum. */
int json_extract_string(const char *obj_start, const char *obj_end,
                        const char *key, char *out, size_t out_size);

/* Display-text variant: overflow returns TRUNCATED with a terminated prefix of
 * complete UTF-8 scalars. Other results have the same contract as above. */
int json_extract_string_prefix(const char *obj_start, const char *obj_end,
                               const char *key, char *out, size_t out_size);

/* Validate one object starting at '{', returning its exclusive end. Never reads
 * outside [start, limit); allows following array members after the object. */
int json_find_object_end(const char *start, const char *limit, const char **end);

/* Visit direct members/elements of a complete, validated object/array.
 * Spans exclude surrounding whitespace. Callback returns 0 or -1.
 * Object keys are decoded into a bounded C string (maximum 255 bytes). */
typedef int (*JsonMemberVisitor)(const char *key, const char *start,
                                 const char *end, void *context);
typedef int (*JsonValueVisitor)(const char *start, const char *end, void *context);
int json_visit_object(const char *start, const char *end, JsonMemberVisitor visit, void *context);
int json_visit_array(const char *start, const char *end, JsonValueVisitor visit, void *context);
int json_decode_string(const char *start, const char *end, char *out, size_t capacity);

/* Read an entire file into a malloc'd buffer. Caller must free *out_buf.
 * Returns 0 on success, -1 on failure. */
int read_file_text(const char *path, char **out_buf, size_t *out_size);

/* Atomically replace one file via a unique sibling temporary, checked write,
 * flush/file sync/close and rename. Before publication failure retains the old
 * file. Returns 0 once published, -1 on failure. Directory sync is best-effort;
 * this does not promise a transaction across files or power-loss recovery. */
int write_file_text(const char *path, const char *data, size_t size);

/* Create a directory if it doesn't exist. Returns 0 on success. */
int mkdir_if_missing(const char *path);

/* Recursively create all directories in path. Returns 0 on success. */
int ensure_dir_recursive(const char *path);

#endif
