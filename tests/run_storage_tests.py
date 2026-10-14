#!/usr/bin/env python3
"""Real upload, USB and repository storage; no network or hardware execution."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
os.environ.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
parser = argparse.ArgumentParser()
parser.add_argument("--legacy", action="store_true")
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix="pldmgr-storage-") as temp:
    build = Path(temp)
    storage = build / "data"
    storage.mkdir()
    header = (ROOT / "include/pldmgr.h").read_text()
    # [CN-FORK] v0.6.0.176 数据目录在 fork 里改名为 /data/nayeplmtools（上游是 /data/pldmgr）。
    header = header.replace("/data/pldmgr", str(storage)).replace("/data/nayeplmtools", str(storage))
    (build / "pldmgr.h").write_text(header)
    (build / "assets_cacert_pem.h").write_text("static const unsigned char assets_cacert_pem[] = {0};\nstatic const unsigned int assets_cacert_pem_len = 0;\n")
    flags = shlex.split(os.environ.get("HOST_CC", "clang")) + ["-std=c11", "-D_DEFAULT_SOURCE", "-g", "-O1", "-Wall", "-Wextra", "-ffunction-sections", "-fdata-sections", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-pthread", "-I"+str(build), "-Iinclude", "-Isrc/psaccounts/third_party", '-DPLDMGR_USB_ROOT_FORMAT="'+str(storage / "usb%d")+'"']
    if args.legacy: flags += ["-DLEGACY_STORAGE"]
    objects = []
    for name in ["slot", "payload_mgr", "sources", "repository", "json_helpers", "sha256", "utils", "config"]:
        obj = build / (name + ".o")
        extra = ["-include", "tests/storage_hooks.h"] if name in ["slot", "payload_mgr", "sources", "sha256", "utils", "json_helpers"] else []
        if name == "repository": extra += ["-Ddownload_to_file=storage_unused_download", "-Ddownload_to_file_limited=storage_unused_download_limited"]
        source = f"src/{name}.c"
        if args.legacy and name in ["slot", "payload_mgr", "sources"]:
            source = f"scratch/{name}_before_step3b.c"
        subprocess.run(flags + extra + ["-c", source, "-o", str(obj)], cwd=ROOT, check=True)
        objects.append(str(obj))
    binary = build / "storage_tests"
    subprocess.run(flags + objects + ["tests/storage_driver.c", "-Wl,--gc-sections", "-lcurl", "-o", str(binary)], cwd=ROOT, check=True)
    url = re.search(r'#define REPOSITORY_SOURCE_URL\s*\\?\s*"([^"]+)"', header)[1]
    cache = storage / ("repo_cache_" + hashlib.sha256(url.rstrip('/').encode()).hexdigest()[:8] + ".json")
    cache.write_text(json.dumps([{"id":"fixture", "name":"Fixture", "filename":"safe.elf", "url":"fixture://payload", "version":"2"}]))
    for case in ["new", "rename", "metadata"]:
        result = subprocess.run([str(binary), case], capture_output=True, text=True)
        if args.legacy:
            assert result.returncode != 0 and "FAIL line" in result.stderr, result.stderr
            print("Previous implementation reproduces " + case + ": " + result.stderr.strip().splitlines()[-1])
        else:
            assert result.returncode == 0, result.stderr
            print(result.stdout.strip())
    if not args.legacy: subprocess.run([str(binary)], cwd=ROOT, check=True)
