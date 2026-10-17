#!/usr/bin/env python3
"""[CN-FORK] v0.6.0.178 越狱引导器同步（/data/ps5_autoloader）宿主用例。

真机只要 /data/ps5_autoloader 和 storage 两个目录，所以在主机上没法直接跑；
这里照 run_storage_tests.py 的做法，把 include/pldmgr.h 里的数据路径整体改写到
临时目录，再拿真的 payload_mgr.c / slot.c 跑安装、U 盘导入、启动对账三条路。
"""
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix="pldmgr-autoloader-") as temp:
    build = Path(temp)
    storage = build / "data"
    storage.mkdir()
    header = (ROOT / "include/pldmgr.h").read_text()
    # 引导器目录先换（它的字符串里没有 /data/nayeplmtools），再换数据目录。
    header = header.replace("/data/ps5_autoloader", str(storage / "ps5_autoloader"))
    header = header.replace("/data/pldmgr", str(storage)).replace("/data/nayeplmtools", str(storage))
    (build / "pldmgr.h").write_text(header)
    (build / "assets_cacert_pem.h").write_text(
        "static const unsigned char assets_cacert_pem[] = {0};\n"
        "static const unsigned int assets_cacert_pem_len = 0;\n"
    )
    flags = shlex.split(os.environ.get("HOST_CC", "clang")) + [
        "-std=c11", "-D_DEFAULT_SOURCE", "-g", "-O1", "-Wall", "-Wextra", "-pthread",
        "-I" + str(build), "-Iinclude", "-Isrc/psaccounts/third_party",
        '-DPLDMGR_USB_ROOT_FORMAT="' + str(storage / "usb%d") + '"',
    ]
    sources = [
        "slot", "payload_mgr", "sources", "repository",
        "json_helpers", "sha256", "utils", "config",
    ]
    objs = []
    for name in sources:
        obj = build / (name + ".o")
        # repository.c 自带的 download_to_file* 与驱动里的桩重名，按 storage 套件的办法改名。
        extra = ["-Ddownload_to_file=autoloader_unused_download",
                 "-Ddownload_to_file_limited=autoloader_unused_download_limited"] if name == "repository" else []
        subprocess.run(flags + extra + ["-c", f"src/{name}.c", "-o", str(obj)], cwd=ROOT, check=True)
        objs.append(str(obj))
    subprocess.run(flags + ["-c", "src/psaccounts/third_party/cJSON.c", "-o", str(build / "cJSON.o")],
                   cwd=ROOT, check=True)
    objs.append(str(build / "cJSON.o"))

    binary = build / "autoloader_sync"
    subprocess.run(flags + objs + [str(ROOT / "tests/autoloader_sync_driver.c"), "-lcurl", "-o", str(binary)],
                   cwd=ROOT, check=True)
    result = subprocess.run([str(binary)], cwd=ROOT)
    if result.returncode != 0:
        sys.exit(result.returncode)
