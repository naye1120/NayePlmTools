#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "repository.h"
#include "slot.h"
#include "sources.h"
#include "json_helpers.h"
#include "utils.h"

/* [CN-FORK] v0.6.0.176 合并上游 runner 后的适配：本 runner 的链接清单是
 * json_helpers.c / utils.c / repository.c / sources.c / slot.c（上游版 sources.c 不读配置），
 * 而 fork 的 sources.c default_source() 会读 DEFAULT_REPO_ENABLED（多源仓库开关）——
 * 那个符号在 src/config.c 里，--gc-sections 下 default_source 是可达的，于是缺定义。
 * 本 runner 只测 JSON 编解码/落盘，不加载配置文件，给个恒返回默认值的桩即可
 * （与产品侧「配置键缺失」时的取值一致）。 */
int config_read_bool(const char *key, int default_val) {
    (void)key;
    return default_val;
}

static void string(const char *value) {
    size_t size = 6 * strlen(value) + 1;
    char *escaped = malloc(size);
    if (!escaped || pldmgr_json_escape(value, escaped, size) != 0) exit(2);
    printf("\"%s\"", escaped);
    free(escaped);
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    char *input = malloc(1048577);
    if (!input) return 2;
    size_t size = fread(input, 1, 1048576, stdin);
    input[size] = 0;
    if (argv[1][0] == 'R') {
        RepoPayload *items;
        size_t count;
        int result = parse_repository_payloads(input, &items, &count);
        printf("{\"status\":%d,\"count\":%zu,\"items\":[", result, count);
        for (size_t i = 0; i < count; i++) {
            if (i) putchar(',');
            printf("{\"name\":"); string(items[i].name);
            printf(",\"filename\":"); string(items[i].filename);
            printf(",\"url\":"); string(items[i].url);
            printf(",\"description\":"); string(items[i].description);
            printf(",\"release_notes\":"); string(items[i].release_notes);
            printf(",\"checksum\":"); string(items[i].checksum);
            putchar('}');
        }
        puts("]}");
        free(items);
    } else if (argv[1][0] == 'S') {
        if (sources_save(input, size) != 0) return 3;
        char *output = malloc(524288);
        if (!output) return 2;
        int result = sources_list_json(output, 524288);
        if (result != 0) { free(output); free(input); return 4; }
        fwrite(output, 1, strlen(output), stdout);
        free(output);
    } else if (argv[1][0] == 'M') {
        if (mkdir(argv[2], 0700) != 0) return 3;
        char path[1024];
        snprintf(path, sizeof(path), "%s/meta.json", argv[2]);
        if (write_file_text(path, input, size) != 0) return 3;
        SlotMeta meta;
        if (slot_read_meta(argv[2], &meta) != 0 || slot_write_meta(argv[2], &meta) != 0) return 4;
        char *output;
        size_t written;
        if (read_file_text(path, &output, &written) != 0) return 5;
        fwrite(output, 1, written, stdout);
        free(output);
    } else return 2;
    free(input);
    return 0;
}
