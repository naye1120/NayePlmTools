#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "json_helpers.h"

static char *unhex(const char *hex, size_t *size, int terminated) {
    *size = strcmp(hex, "-") == 0 ? 0 : strlen(hex) / 2;
    char *out = malloc(*size + (terminated || !*size));
    if (!out) exit(2);
    for (size_t i = 0; i < *size; i++) {
        unsigned byte;
        if (sscanf(hex + 2 * i, "%2x", &byte) != 1) exit(2);
        out[i] = (char)byte;
    }
    if (terminated) out[*size] = 0;
    return out;
}

int main(void) {
    char mode, input_hex[32769], key_hex[8193];
    size_t capacity, bound;
    while (scanf(" %c %zu %zu %32768s %8192s", &mode, &capacity, &bound,
                 input_hex, key_hex) == 5) {
        size_t input_size, key_size;
#ifdef LEGACY_DECODE
        char *input = unhex(input_hex, &input_size, 1);
#else
        /* Exact allocation: bounded input deliberately has no trailing NUL. */
        char *input = unhex(input_hex, &input_size, 0);
#endif
        char *key = unhex(key_hex, &key_size, 1);
        unsigned char output[4098];
        if (capacity > 4096 || bound > input_size) return 2;
        memset(output, 0xa5, sizeof(output));
        int result;
#ifndef LEGACY_DECODE
        if (mode == 'P') result = json_extract_string_prefix(input, input + bound, key,
                                                            (char *)output + 1, capacity);
        else if (mode == 'E') {
            const char *end;
            result = json_find_object_end(input, input + bound, &end);
            printf("%d %zu\n", result, end ? (size_t)(end - input) : 0);
            free(input); free(key);
            continue;
        } else
#endif
        result = json_extract_string(input, input + bound, key, (char *)output + 1, capacity);
        if (output[0] != 0xa5) return 3;
        for (size_t i = capacity + 1; i < sizeof(output); i++)
            if (output[i] != 0xa5) return 3;
        if (capacity && !memchr(output + 1, 0, capacity)) return 4;
        printf("%d ", result);
        if (capacity) {
            size_t size = strlen((char *)output + 1);
            for (size_t i = 0; i < size; i++) printf("%02x", output[i + 1]);
        }
        puts("");
        free(input); free(key);
    }
    return ferror(stdin) ? 5 : 0;
}
