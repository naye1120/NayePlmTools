#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdint.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include "json_helpers.h"
#include "utils.h"

int json_builder_fail(JsonListBuilder *jb) {
    jb->error = 1;
    jb->pos = 0;
    if (jb->buf && jb->size) jb->buf[0] = '\0';
    return -1;
}

static int json_append_args(JsonListBuilder *jb, const char *fmt, va_list args) {
    if (jb->error) return -1;
    if (!jb->buf || jb->pos >= jb->size) return json_builder_fail(jb);
    int written = vsnprintf(jb->buf + jb->pos, jb->size - jb->pos, fmt, args);
    if (written < 0 || (size_t)written >= jb->size - jb->pos) return json_builder_fail(jb);
    jb->pos += (size_t)written;
    return 0;
}

int json_append(JsonListBuilder *jb, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int result = json_append_args(jb, fmt, args);
    va_end(args);
    return result;
}

int json_escape_checked(JsonListBuilder *jb, const char *src, char *dst, size_t size) {
    if (pldmgr_json_escape(src, dst, size) != 0) return json_builder_fail(jb);
    return jb->error ? -1 : 0;
}

size_t json_format(char *buf, size_t size, const char *fmt, ...) {
    JsonListBuilder jb = { buf, size, 0, 1, 0 };
    va_list args;
    va_start(args, fmt);
    json_append_args(&jb, fmt, args);
    va_end(args);
    return jb.pos;
}

typedef struct {
    const char *p, *end;
} JsonReader;

typedef struct {
    char *out;
    size_t capacity, written;
    int truncated;
    const char *expected;
    size_t expected_size, matched;
    int equal;
} JsonStringSink;

static void json_space(JsonReader *r) {
    while (r->p < r->end && (*r->p == ' ' || *r->p == '\t' ||
                            *r->p == '\r' || *r->p == '\n')) r->p++;
}

static int json_take(JsonReader *r, char c) {
    if (r->p == r->end || *r->p != c) return -1;
    r->p++;
    return 0;
}

static int json_hex4(JsonReader *r, uint32_t *value) {
    if ((size_t)(r->end - r->p) < 4) return -1;
    *value = 0;
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)*r->p++;
        unsigned digit;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else return -1;
        *value = (*value << 4) | digit;
    }
    return 0;
}

static int json_emit(JsonStringSink *sink, const char *bytes, size_t size) {
    if (!sink) return 0;
    if (sink->expected && sink->equal) {
        if (size > sink->expected_size - sink->matched ||
            memcmp(bytes, sink->expected + sink->matched, size) != 0) sink->equal = 0;
        else sink->matched += size;
    }
    if (sink->out) {
        if (memchr(bytes, 0, size)) return -1; /* C-string API cannot represent NUL. */
        if (!sink->truncated) {
            if (size >= sink->capacity - sink->written) sink->truncated = 1;
            else {
                memcpy(sink->out + sink->written, bytes, size);
                sink->written += size;
                sink->out[sink->written] = '\0';
            }
        }
    }
    return 0;
}

static int json_string(JsonReader *r, JsonStringSink *sink) {
    if (json_take(r, '"') != 0) return -1;
    while (r->p < r->end) {
        unsigned char c = (unsigned char)*r->p++;
        char bytes[4];
        const char *part = bytes;
        size_t size = 1;
        if (c == '"') return 0;
        if (c < 0x20) return -1;
        if (c == '\\') {
            if (r->p == r->end) return -1;
            switch (*r->p++) {
                case '"': bytes[0] = '"'; break;
                case '\\': bytes[0] = '\\'; break;
                case '/': bytes[0] = '/'; break;
                case 'b': bytes[0] = '\b'; break;
                case 'f': bytes[0] = '\f'; break;
                case 'n': bytes[0] = '\n'; break;
                case 'r': bytes[0] = '\r'; break;
                case 't': bytes[0] = '\t'; break;
                case 'u': {
                    uint32_t cp, low;
                    if (json_hex4(r, &cp) != 0) return -1;
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (json_take(r, '\\') != 0 || json_take(r, 'u') != 0 ||
                            json_hex4(r, &low) != 0 || low < 0xdc00 || low > 0xdfff) return -1;
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) return -1;
                    if (cp < 0x80) bytes[0] = (char)cp;
                    else if (cp < 0x800) {
                        size = 2;
                        bytes[0] = (char)(0xc0 | (cp >> 6));
                        bytes[1] = (char)(0x80 | (cp & 63));
                    } else if (cp < 0x10000) {
                        size = 3;
                        bytes[0] = (char)(0xe0 | (cp >> 12));
                        bytes[1] = (char)(0x80 | ((cp >> 6) & 63));
                        bytes[2] = (char)(0x80 | (cp & 63));
                    } else {
                        size = 4;
                        bytes[0] = (char)(0xf0 | (cp >> 18));
                        bytes[1] = (char)(0x80 | ((cp >> 12) & 63));
                        bytes[2] = (char)(0x80 | ((cp >> 6) & 63));
                        bytes[3] = (char)(0x80 | (cp & 63));
                    }
                    break;
                }
                default: return -1;
            }
        } else if (c < 0x80) bytes[0] = (char)c;
        else {
            part = r->p - 1;
            if (c >= 0xc2 && c <= 0xdf) size = 2;
            else if (c >= 0xe0 && c <= 0xef) size = 3;
            else if (c >= 0xf0 && c <= 0xf4) size = 4;
            else return -1;
            if ((size_t)(r->end - part) < size) return -1;
            for (size_t i = 1; i < size; i++)
                if ((unsigned char)part[i] < 0x80 || (unsigned char)part[i] > 0xbf) return -1;
            unsigned char second = (unsigned char)part[1];
            if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
                (c == 0xf0 && second < 0x90) || (c == 0xf4 && second > 0x8f)) return -1;
            r->p = part + size;
        }
        if (json_emit(sink, part, size) != 0) return -1;
    }
    return -1;
}

static int json_digit(JsonReader *r) {
    return r->p < r->end && *r->p >= '0' && *r->p <= '9';
}

static int json_value(JsonReader *r, unsigned depth) {
    json_space(r);
    if (r->p == r->end) return -1;
    char c = *r->p;
    if (c == '"') return json_string(r, NULL);
    if (c == '{' || c == '[') {
        if (depth >= 64) return -1;
        r->p++;
        char close = c == '{' ? '}' : ']';
        json_space(r);
        if (r->p < r->end && *r->p == close) { r->p++; return 0; }
        for (;;) {
            if (c == '{') {
                if (json_string(r, NULL) != 0) return -1;
                json_space(r);
                if (json_take(r, ':') != 0) return -1;
            }
            if (json_value(r, depth + 1) != 0) return -1;
            json_space(r);
            if (r->p < r->end && *r->p == close) { r->p++; return 0; }
            if (json_take(r, ',') != 0) return -1;
            json_space(r);
        }
    }
    if (c == 't' || c == 'f' || c == 'n') {
        const char *literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
        size_t size = strlen(literal);
        if ((size_t)(r->end - r->p) < size || memcmp(r->p, literal, size) != 0) return -1;
        r->p += size;
        return 0;
    }
    if (c == '-') r->p++;
    if (!json_digit(r)) return -1;
    if (*r->p == '0') r->p++;
    else while (json_digit(r)) r->p++;
    if (r->p < r->end && *r->p == '.') {
        r->p++;
        if (!json_digit(r)) return -1;
        while (json_digit(r)) r->p++;
    }
    if (r->p < r->end && (*r->p == 'e' || *r->p == 'E')) {
        r->p++;
        if (r->p < r->end && (*r->p == '+' || *r->p == '-')) r->p++;
        if (!json_digit(r)) return -1;
        while (json_digit(r)) r->p++;
    }
    return 0;
}

int json_find_object_end(const char *start, const char *limit, const char **end) {
    if (!end) return -1;
    *end = NULL;
    if (!start || !limit || start >= limit || *start != '{') return -1;
    JsonReader r = { start, limit };
    if (json_value(&r, 0) != 0) return -1;
    *end = r.p;
    return 0;
}

static int json_extract(const char *start, const char *end, const char *key,
                        char *out, size_t capacity, int prefix) {
    if (!out || !capacity) return JSON_STRING_INVALID;
    out[0] = '\0';
    if (!start || !end || start > end || !key) return JSON_STRING_INVALID;
    JsonReader r = { start, end };
    JsonStringSink value = { .out = out, .capacity = capacity };
    int found = 0;
    json_space(&r);
    if (json_take(&r, '{') != 0) goto invalid;
    json_space(&r);
    if (r.p < r.end && *r.p == '}') r.p++;
    else for (;;) {
        JsonStringSink member = { .expected = key, .expected_size = strlen(key), .equal = 1 };
        if (json_string(&r, &member) != 0) goto invalid;
        json_space(&r);
        if (json_take(&r, ':') != 0) goto invalid;
        json_space(&r);
        if (member.equal && member.matched == member.expected_size) {
            if (found || json_string(&r, &value) != 0) goto invalid;
            found = 1;
        } else if (json_value(&r, 1) != 0) goto invalid;
        json_space(&r);
        if (r.p < r.end && *r.p == '}') { r.p++; break; }
        if (json_take(&r, ',') != 0) goto invalid;
        json_space(&r);
    }
    json_space(&r);
    if (r.p != r.end) goto invalid;
    if (!found) return JSON_STRING_MISSING;
    if (!value.truncated) return JSON_STRING_OK;
    if (prefix) return JSON_STRING_TRUNCATED;
    out[0] = '\0';
    return JSON_STRING_TOO_LONG;
invalid:
    out[0] = '\0';
    return JSON_STRING_INVALID;
}

int json_extract_string(const char *start, const char *end, const char *key,
                        char *out, size_t capacity) {
    return json_extract(start, end, key, out, capacity, 0);
}

int json_extract_string_prefix(const char *start, const char *end, const char *key,
                               char *out, size_t capacity) {
    return json_extract(start, end, key, out, capacity, 1);
}

int json_decode_string(const char *start, const char *end, char *out, size_t capacity) {
    if (!out || !capacity || !start || !end || start > end) return -1;
    out[0] = '\0';
    JsonReader r = {start, end};
    JsonStringSink sink = {.out = out, .capacity = capacity};
    if (json_string(&r, &sink) || r.p != end || sink.truncated) {
        out[0] = '\0'; return -1;
    }
    return 0;
}

static int json_visit(const char *start, const char *end, JsonMemberVisitor member,
                      JsonValueVisitor value, void *context, int object) {
    if (!start || !end || start >= end) return -1;
    JsonReader check = {start, end};
    json_space(&check);
    if (check.p == end || *check.p != (object ? '{' : '[') || json_value(&check, 0)) return -1;
    json_space(&check);
    if (check.p != end) return -1;
    JsonReader r = {start, end};
    json_space(&r); r.p++; json_space(&r);
    char close = object ? '}' : ']';
    if (*r.p == close) return 0;
    for (;;) {
        char key[256];
        if (object) {
            JsonStringSink sink = {.out = key, .capacity = sizeof(key)};
            if (json_string(&r, &sink) || sink.truncated) return -1;
            json_space(&r); r.p++; json_space(&r);
        }
        const char *first = r.p;
        if (json_value(&r, 1)) return -1;
        if (object ? member(key, first, r.p, context) : value(first, r.p, context)) return -1;
        json_space(&r);
        if (*r.p == close) return 0;
        r.p++; json_space(&r);
    }
}
int json_visit_object(const char *start, const char *end, JsonMemberVisitor visit, void *context) {
    return visit ? json_visit(start, end, visit, NULL, context, 1) : -1;
}
int json_visit_array(const char *start, const char *end, JsonValueVisitor visit, void *context) {
    return visit ? json_visit(start, end, NULL, visit, context, 0) : -1;
}

int read_file_text(const char *path, char **out_buf, size_t *out_size) {
    if (!out_buf || !out_size) { errno = EINVAL; return -1; }
    *out_buf = NULL; *out_size = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return -1; }
    long length = ftell(f);
    if (length < 0 || (uintmax_t)length >= SIZE_MAX || fseek(f, 0, SEEK_SET)) {
        fclose(f); return -1;
    }
    char *buf = malloc((size_t)length + 1);
    if (!buf) { fclose(f); errno = ENOMEM; return -1; }
    size_t count = fread(buf, 1, (size_t)length, f);
    int failed = count != (size_t)length || ferror(f);
    if (fclose(f)) failed = 1;
    if (failed) { free(buf); errno = EIO; return -1; }
    buf[count] = '\0'; *out_buf = buf; *out_size = count;
    return 0;
}

int write_file_text(const char *path, const char *data, size_t size) {
    if (!path || (!data && size)) { errno = EINVAL; return -1; }
    /* Sibling temporary file: concurrent writers never share a staging path,
     * readers see the complete old or new inode, never a truncated file. */
    size_t length = strlen(path);
    if (length > SIZE_MAX - 16) { errno = ENAMETOOLONG; return -1; }
    char *temp = malloc(length + 16);
    if (!temp) return -1;
    snprintf(temp, length + 16, "%s.tmp-XXXXXX", path);
    int fd = mkstemp(temp);
    if (fd < 0) { free(temp); return -1; }
    FILE *f = fdopen(fd, "wb");
    int failed = !f;
    if (!f) close(fd);
    else {
        if ((size && fwrite(data, 1, size, f) != size) || fflush(f) || fsync(fileno(f))) failed = 1;
        if (fclose(f)) failed = 1;
    }
    if (!failed && rename(temp, path)) failed = 1;
    int saved = errno;
    if (failed) unlink(temp);
    else {
        /* Publication succeeded. Directory fsync is best-effort on filesystems
         * that do not support it; a late sync failure cannot undo publication. */
        char *slash = strrchr(temp, '/');
        const char *parent = ".";
        if (slash) { if (slash == temp) slash[1] = '\0'; else *slash = '\0'; parent = temp; }
        int dir = open(parent, O_RDONLY | O_DIRECTORY);
        if (dir >= 0) { fsync(dir); close(dir); }
    }
    free(temp); errno = saved;
    return failed ? -1 : 0;
}

int mkdir_if_missing(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        return S_ISDIR(st.st_mode) ? 0 : -1;
    }
    return mkdir(path, 0777);
}

int ensure_dir_recursive(const char *path) {
    char tmp[512];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) {
        return -1;
    }

    strncpy(tmp, path, sizeof(tmp));
    tmp[len] = '\0';

    for (size_t i = 1; i < len; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (strlen(tmp) > 0 && mkdir_if_missing(tmp) != 0) {
                return -1;
            }
            tmp[i] = '/';
        }
    }
    return mkdir_if_missing(tmp);
}
