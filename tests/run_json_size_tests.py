#!/usr/bin/env python3
"""Exercise actual serializers, guarded capacities, and failed JSON commits."""
import argparse
import json
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time
from urllib.parse import urljoin

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--legacy", action="store_true", help="Compare saved pre-2c serializer snapshots")
    parser.add_argument("--no-sanitize", action="store_true")
    args = parser.parse_args()
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    os.environ.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    compiler = shlex.split(os.environ.get("HOST_CC", "clang"))
    with tempfile.TemporaryDirectory(prefix="pldmgr-json-size-") as temp:
        build = Path(temp)
        storage = build / "data"
        storage.mkdir()
        # [CN-FORK] v0.6.0.176 fork 的默认云端源在缺 DEFAULT_REPO_ENABLED 键时按「关」处理
        # （上游恒开，见 sources.c default_source()），不显式打开的话 repository 一节会列出空源、
        # 「输出必须超过 64 KiB」那条断言必然红。这里把键写进临时配置夹具。
        (storage / "nayeplmtools_config.txt").write_text("DEFAULT_REPO_ENABLED=1\n")
        # [CN-FORK] v0.6.0.176 数据目录在 fork 里改名为 /data/nayeplmtools（上游是 /data/pldmgr），
        # 两个名字都要换，否则二进制会去读真机的真实路径、夹具全部落空。
        header = (ROOT / "include/pldmgr.h").read_text()
        (build / "pldmgr.h").write_text(header.replace("/data/pldmgr", str(storage))
                                               .replace("/data/nayeplmtools", str(storage)))
        (build / "assets_cacert_pem.h").write_text("static const unsigned char assets_cacert_pem[] = {0};\n"
                                                  "static const unsigned int assets_cacert_pem_len = 0;\n")
        # Recording kernel fixture using the SDK-sized names/field offsets.
        (build / "sys").mkdir()
        (build / "sys/proc.h").write_text("")
        (build / "sys/user.h").write_text((ROOT / "tests/process_sdk_stub.h").read_text())
        (build / "sys/sysctl.h").write_text("#include <stddef.h>\n#define CTL_KERN 1\n#define KERN_PROC 2\n#define KERN_PROC_PROC 3\nint sysctl(const int *, unsigned, void *, size_t *, const void *, size_t);\n")
        (build / "kernel.c").write_text('''#include <string.h>
#include <stdlib.h>
#include <sys/user.h>
int sysctl(const int *mib, unsigned n, void *out, size_t *size, const void *input, size_t length) {
    (void)mib; (void)n; (void)input; (void)length;
    *size = 1000 * sizeof(struct kinfo_proc);
    if (out) for (int i = 0; i < 1000; i++) {
        struct kinfo_proc *record = (struct kinfo_proc *)out + i;
        record->ki_structsize = sizeof(*record); record->ki_pid = i + 1; record->ki_rssize = 1024;
        memset(record->ki_comm, 1, 19); record->ki_comm[19] = 0;
    }
    return 0;
}
int sceKernelGetAppInfo(pid_t pid, void *info) { (void)pid; unsigned id = 0; memcpy(info, &id, sizeof(id)); return 0; }
int download_to_file(const char *url, const char *path) { (void)url; (void)path; abort(); }
int download_to_file_limited(const char *url, const char *path, size_t limit) { (void)limit; return download_to_file(url,path); }
''')
        for i in range(400):
            slot = storage / "payloads/local" / f"test-{i}"
            slot.mkdir(parents=True)
            (slot / "safe.elf").write_bytes(b"fixture only")
            meta = {"kind": "repo", "filename": "safe.elf", "id": f"test-{i}",
                    "name": "\x01" * 250, "description": "\x01" * 1000,
                    "source_url": "https://example.invalid", "source_name": "fixture"}
            (slot / "meta.json").write_text(json.dumps(meta))
        common = compiler + ["-std=c11", "-D_DEFAULT_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
                             "-ffunction-sections", "-fdata-sections",
                             "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                             "-I" + str(build), "-Iinclude",
                             # [CN-FORK] v0.6.0.176 fork 的 src/config.c 用 cJSON 取
                             # ARCHIVE_GLOBAL_CONFIG 的字符串值（上游不引 cJSON），
                             # 所以要带上它所在的第三方目录（与 Makefile 的 INCLUDES 同口径）。
                             "-Isrc/psaccounts/third_party",
                             "-pthread"]
        if not args.no_sanitize:
            common += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        if args.legacy:
            common += ["-DLEGACY_SIZE", "-Wno-missing-field-initializers"]

        def source(name):
            return f"scratch/{name}_before_step2c.c" if args.legacy else f"src/{name}.c"

        helper = build / "helpers.o"
        subprocess.run([arg for arg in common if not arg.startswith("-Wl,")] + ["-include", "tests/json_size_hooks.h", "-c", source("json_helpers"),
                                 "-o", str(helper)], cwd=ROOT, check=True)
        repository = build / "repository.o"
        subprocess.run([arg for arg in common if not arg.startswith("-Wl,")] +
                       ["-Ddownload_to_file=json_unused_download", "-Ddownload_to_file_limited=json_unused_download_limited", "-c", "src/repository.c", "-o", str(repository)],
                       cwd=ROOT, check=True)
        binary = build / "json_size_tests"
        sources = [source(name) for name in ["history_mgr", "log_server", "payload_mgr", "slot", "sources", "process_mgr", "config"]]
        subprocess.run(common + [str(helper), str(repository), "src/utils.c", "src/sha256.c", *sources,
                                 str(build / "kernel.c"), "tests/json_size_driver.c", "-lcurl", "-o", str(binary)],
                       cwd=ROOT, check=True)
        failures = []
        # Fresh on-disk cache proves actual repository serialization without a
        # download. The recording download helper aborts if unexpectedly called.
        header = (ROOT / "include/pldmgr.h").read_text()
        import re
        url = re.search(r'#define REPOSITORY_SOURCE_URL\s*\\?\s*"([^"]+)"', header)[1]
        cache = storage / ("repo_cache_" + hashlib.sha256(url.rstrip('/').encode()).hexdigest()[:8] + ".json")
        # Icon-bearing catalogs also resolve icons.json by default. Keep its
        # cache fresh so serializer checks remain independent of downloads.
        icon_cache = storage / ("repo_icons_" + hashlib.sha256(url.encode()).hexdigest())
        icon_cache.with_suffix(".json").write_text(json.dumps({"icons": {
            "shared-icon": "data:image/png;base64,aGVsbG8="
        }}))
        icon_cache.with_suffix(".meta").write_text(json.dumps({
            "url": urljoin(url, "icons.json"), "updated": str(int(time.time()))
        }))
        for mode in ["builder", "history", "logs", "payloads", "processes", "writers", "favorites", "repository", "repository_limit", "payload_limit", "legacy_map"]:
            if mode == "payload_limit":
                for i in range(400, 800):
                    slot = storage / "payloads/local" / f"test-{i}"
                    slot.mkdir()
                    (slot / "safe.elf").write_bytes(b"fixture only")
                    meta = {"kind": "repo", "filename": "safe.elf", "id": f"test-{i}",
                            "name": "\x01" * 250, "description": "\x01" * 1000}
                    (slot / "meta.json").write_text(json.dumps(meta))
            if mode.startswith("repository"):
                count = 30 if mode == "repository" else 300
                cache.write_text(json.dumps([{"id": f"item-{i}", "name": f"Item {i}", "filename": "safe.elf",
                                             "url": "https://example.invalid/safe.elf", "description": "\x01" * 1000,
                                             "release_notes": "\x01" * 2000, "icon_key": "shared-icon"} for i in range(count)]))
                Path(str(cache) + ".ts").write_text(str(int(time.time())))
            result = subprocess.run([str(binary), mode], text=True, capture_output=True)
            summaries = [line for line in result.stdout.splitlines() if line.startswith("JSON size checks:")]
            print(mode + ": " + " ".join(summaries))
            if result.returncode:
                failures.append((mode, f"process exited with code {result.returncode}: " + result.stderr[-2000:]))
            for line in result.stdout.splitlines():
                if not line.startswith("RESULT "):
                    continue
                try:
                    document = json.loads(json.loads(line[7:]))
                    if mode == "history":
                        assert document["history"] == [chr(65 + i) + "\x01" * 510 for i in reversed(range(20))]
                    elif mode == "logs":
                        assert document["logs"] == ["\x01" * 255] * 100
                    elif mode == "payloads":
                        assert len(document["items"]) == 400 and len(document["payloads"]) == 400
                        assert all(item["name"] == "\x01" * 250 and item["description"] == "\x01" * 1000 for item in document["items"])
                    elif mode == "processes":
                        assert len(document["processes"]) == 1000
                        assert all(item["name"] == "\x01" * 19 for item in document["processes"])
                    elif mode == "repository":
                        items = document["sources"][0]["payloads"]
                        assert len(items) == 30
                        assert all(item["description"] == "\x01" * 1000 and item["release_notes"] == "\x01" * 2000 and
                                   item["icon_key"] == "shared-icon" and "icon" not in item for item in items)
                except (ValueError, AssertionError) as error:
                    failures.append((mode, "independent decoding/content check failed: " + str(error)))
        if failures:
            for failure in failures:
                print("FAIL", failure)
            raise SystemExit(f"JSON serialization: {len(failures)} failed checks")
        print("JSON serialization: all bounded/large-output/failed-commit scenarios passed")


if __name__ == "__main__":
    main()
