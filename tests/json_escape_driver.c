#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

/* A line protocol lets Python independently decode thousands of boundary cases.
 * Guard bytes and sanitizers also check the encoder's writable extent. */
int main(void) {
#ifndef LEGACY_ESCAPE
    char probe[1] = {'x'};
    if (pldmgr_json_escape(NULL, probe, sizeof(probe)) != -1 || probe[0] != 0 ||
        pldmgr_json_escape("", NULL, 0) != -1 ||
        pldmgr_json_escape("", probe, 0) != -1) return 6;
#endif
    size_t capacity;
    char hex[8193];
    while (scanf("%zu %8192s", &capacity, hex) == 2) {
        unsigned char input[4097], output[4098];
        size_t length = strcmp(hex, "-") == 0 ? 0 : strlen(hex) / 2;
        if (capacity > 4096 || length > 4096) return 2;
        for (size_t i = 0; i < length; i++) {
            unsigned byte;
            if (sscanf(hex + 2 * i, "%2x", &byte) != 1 || byte == 0) return 2;
            input[i] = (unsigned char)byte;
        }
        input[length] = 0;
        memset(output, 0xa5, sizeof(output));
#ifdef LEGACY_ESCAPE
        pldmgr_json_escape((const char *)input, (char *)output + 1, capacity);
        int result = 0;
#else
        int result = pldmgr_json_escape((const char *)input, (char *)output + 1, capacity);
#endif
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
    }
    return ferror(stdin) ? 5 : 0;
}
