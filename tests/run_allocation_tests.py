#!/usr/bin/env python3
"""Fault every array growth and copied path in actual parsers/scanners."""
import argparse
import json
import hashlib
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import time
import re

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--legacy", action="store_true")
    args = parser.parse_args()
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    os.environ.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    with tempfile.TemporaryDirectory(prefix="pldmgr-alloc-") as temp:
        build = Path(temp)
        storage = build / "data"
        storage.mkdir()
        # [CN-FORK] v0.6.0.176 数据目录在 fork 里改名为 /data/nayeplmtools（上游是 /data/pldmgr），
        # 两个名字都要换，否则二进制会去读真机的真实路径、夹具全部落空。
        header = (ROOT / "include/pldmgr.h").read_text()
        (build / "pldmgr.h").write_text(header.replace("/data/pldmgr", str(storage))
                                               .replace("/data/nayeplmtools", str(storage)))
        (build / "assets_cacert_pem.h").write_text("static const unsigned char assets_cacert_pem[] = {0};\nstatic const unsigned int assets_cacert_pem_len = 0;\n")
        common = shlex.split(os.environ.get("HOST_CC", "clang")) + ["-std=c11", "-D_DEFAULT_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
                 "-ffunction-sections", "-fdata-sections", "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                 "-I" + str(build), "-Iinclude",
                 # [CN-FORK] v0.6.0.176 fork 的 src/config.c 引 cJSON（取 ARCHIVE_GLOBAL_CONFIG），
                 # 上游不引 —— 补上第三方头目录（与 Makefile 的 INCLUDES 同口径）。
                 "-Isrc/psaccounts/third_party",
                 "-pthread", '-DPLDMGR_USB_ROOT_FORMAT="' + str(storage / "usb%d") + '"']
        if args.legacy:
            common += ["-DLEGACY_ALLOCATION"]
        objects = []
        for name in ["json_helpers", "repository", "slot", "payload_mgr", "sources"]:
            source = f"scratch/{name}_before_step3a.c" if args.legacy and name != "json_helpers" else f"src/{name}.c"
            if args.legacy and name == "payload_mgr":
                # Test-only path overlay for the old hardcoded USB roots.
                patched = build / "payload_before.c"
                patched.write_text((ROOT / source).read_text().replace("/mnt/usb", str(storage / "usb")))
                source = str(patched)
            obj = build / (name + ".o")
            flags = ["-Ddownload_to_file=allocation_unused_download", "-Ddownload_to_file_limited=allocation_unused_download_limited"] if name == "repository" else []
            subprocess.run(common + flags + ["-include", "tests/allocation_hooks.h", "-c", source, "-o", str(obj)], cwd=ROOT, check=True)
            objects.append(str(obj))
        binary = build / "allocation_tests"
        subprocess.run(common + objects + ["src/utils.c", "src/sha256.c", "src/config.c", "tests/allocation_driver.c",
                        "-Wl,--gc-sections", "-lcurl", "-o", str(binary)], cwd=ROOT, check=True)
        header = (ROOT / "include/pldmgr.h").read_text()
        url = re.search(r'#define REPOSITORY_SOURCE_URL\s*\\?\s*"([^"]+)"', header)[1]
        cache = storage / ("repo_cache_" + hashlib.sha256(url.rstrip('/').encode()).hexdigest()[:8] + ".json")
        cache.write_text(json.dumps([{"name": f"item-{i}", "filename": "safe.elf", "url": "https://example.invalid"} for i in range(500)]))
        Path(str(cache) + ".ts").write_text(str(int(time.time())))
        for i in range(70):
            slot = storage / "payloads/local" / f"test-{i}"
            slot.mkdir(parents=True)
            (slot / "safe.elf").write_bytes(b"fixture only")
            (slot / "meta.json").write_text(json.dumps({"kind": "local", "filename": "safe.elf"}))
        failures = 0
        for mode in ["repository", "slots", "source_listing", "usb"]:
            if mode == "usb":
                # Remove fixture metadata files from discovery without deleting:
                # rename the scanned local directory into this temporary build.
                (storage / "payloads/local").rename(build / "saved-slots")
                # [CN-FORK] v0.6.0.176 fork 把 U 盘库目录名统一成 nayeplmtools（上游 pldmgr，
                # 见 pldmgr.h 的 PLDMGR_USB_LIB_NAME），配置文件名也从 pldmgr_config.txt
                # 改成 nayeplmtools_config.txt —— 夹具必须跟着改，否则三条扫描路径全落空。
                for directory, count in [("usb0/nayeplmtools", 24), ("usb0/nayeplmtools/nested", 23), ("usb0", 23)]:
                    path = storage / directory
                    path.mkdir(parents=True, exist_ok=True)
                    for i in range(count): (path / f"test-{i}.elf").write_bytes(b"fixture only")
                (storage / "nayeplmtools_config.txt").write_text("SCAN_USB_PAYLOADS=1\n")
            result = subprocess.run([str(binary), mode], capture_output=True, text=True)
            print(result.stdout.strip())
            if result.returncode:
                failures += 1
                print(result.stderr[-1800:])
        if failures: raise SystemExit(f"Allocation tests: {failures} failing scenarios")


if __name__ == "__main__":
    main()
