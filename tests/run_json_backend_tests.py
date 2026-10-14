#!/usr/bin/env python3
"""Actual repository parsing, source save/load, and slot metadata round trips."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--no-sanitize", action="store_true")
    args = parser.parse_args()
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    os.environ.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    compiler = shlex.split(os.environ.get("HOST_CC", "clang"))
    with tempfile.TemporaryDirectory(prefix="pldmgr-json-backend-") as temp:
        build = Path(temp)
        storage = build / "data"
        storage.mkdir()
        # [CN-FORK] v0.6.0.176 数据目录在 fork 里改名为 /data/nayeplmtools（上游是 /data/pldmgr），
        # 两个名字都要换，否则二进制会去读真机的真实路径、夹具全部落空。
        header = (ROOT / "include/pldmgr.h").read_text()
        (build / "pldmgr.h").write_text(header.replace("/data/pldmgr", str(storage))
                                               .replace("/data/nayeplmtools", str(storage)))
        (build / "assets_cacert_pem.h").write_text("static const unsigned char assets_cacert_pem[] = {0};\n"
                                                  "static const unsigned int assets_cacert_pem_len = 0;\n")
        binary = build / "json_backend_tests"
        command = compiler + ["-std=c11", "-D_DEFAULT_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
                              "-ffunction-sections", "-fdata-sections",
                              "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections",
                              "-I" + str(build), "-Iinclude", "-pthread"]
        if not args.no_sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        subprocess.run(command + ["src/json_helpers.c", "src/utils.c", "src/repository.c",
                                  "src/sources.c", "src/slot.c", "tests/json_backend_driver.c",
                                  "-o", str(binary)], cwd=ROOT, check=True)
        checks = 0

        def run(mode, document):
            nonlocal checks
            checks += 1
            slot = build / f"slot-{checks}"
            result = subprocess.run([str(binary), mode, str(slot)], input=json.dumps(document).encode(),
                                    capture_output=True)
            if result.returncode:
                raise AssertionError(result.stderr.decode() or f"Backend driver failed: {result.returncode}")
            return json.loads(result.stdout)

        value = 'Zażółć Україна 日本語 😀 } { "quote" \\path\nnext\tline\x01'
        item = {"id": "test", "name": value, "filename": "safe.elf",
                "url": "https://example.invalid/safe.elf", "description": value,
                "release_notes": value, "checksum": "a" * 64}
        for manifest in [[item], {"payloads": [item]}, item]:
            actual = run("R", manifest)
            assert actual["status"] == 0 and actual["count"] == 1, actual
            for field in ["name", "filename", "url", "description", "release_notes", "checksum"]:
                assert actual["items"][0][field] == item[field], (field, actual)
        exact = dict(item, release_notes="a" * 2047)
        assert run("R", [exact])["items"][0]["release_notes"] == exact["release_notes"]
        for notes in ["a" * 3000, "日" * 1000, "a" * 2043 + "😀" * 50]:
            actual = run("R", [dict(item, release_notes=notes)])["items"][0]["release_notes"]
            assert actual.endswith("...") and len(actual.encode()) <= 2047, actual
            assert notes.startswith(actual[:-3]), actual
        actual = run("R", [dict(item, description="😀" * 1000)])["items"][0]["description"]
        assert actual == "😀" * 255
        # Strict fields must not become valid-looking shortened identifiers/hashes.
        for field, content in [("name", "a" * 128), ("filename", "a" * 256),
                               ("url", "a" * 1024), ("checksum", "a" * 65), ("checksum", None)]:
            assert run("R", [dict(item, **{field: content})])["count"] == 0
        assert run("R", [dict(item, checksum="")])["count"] == 1
        nested = {"nested": item, "filename": "safe.elf", "url": item["url"]}
        assert run("R", [nested])["count"] == 0
        # [CN-FORK] v0.6.0.176 sources_list_json() 会多回一个 "enabled" 字段（v0.5.1.65 起，
        # 管理来源页要靠它把已关闭的默认源灰显而不是隐行），所以期望值里一并写上。
        source = {"id": "custom", "name": value, "url": "https://example.invalid/source.json",
                  "removable": True, "enabled": True}
        actual = run("S", {"sources": [source]})
        assert actual["sources"][1] == source, actual
        meta = {"schema": 2, "kind": "repo", "filename": "safe.elf", "installed_at": "2026-10-12",
                "id": "test", "source_url": source["url"], "source_name": value,
                "name": value, "version": "1.0", "checksum": "a" * 64,
                "url": item["url"], "description": value, "author": value}
        actual = run("M", meta)
        for field, content in meta.items():
            assert actual[field] == content, (field, actual)
        local = {"schema": 2, "kind": "local", "filename": "safe.elf", "origin": "web_upload",
                 "origin_detail": value, "installed_at": "2026-10-12"}
        actual = run("M", local)
        for field, content in local.items():
            assert actual[field] == content, (field, actual)
        maximum_sources = [{"id": f"custom-{i}", "name": "\x01" * 250,
                            "url": "\x01" * 1000, "removable": True, "enabled": True}
                           for i in range(49)]
        actual = run("S", {"sources": maximum_sources})
        assert actual["sources"][1:] == maximum_sources
        maximum_meta = dict(meta)
        capacities = {"filename": 256, "installed_at": 64, "id": 128, "source_url": 1024,
                      "source_name": 256, "name": 256, "version": 64, "checksum": 65,
                      "last_update": 64, "url": 1024, "source": 1024, "source_direct": 1024,
                      "category": 128, "description": 1024, "author": 128, "details_url": 1024}
        maximum_meta.update({field: "\x01" * (size - 1) for field, size in capacities.items()})
        actual = run("M", maximum_meta)
        for field, content in maximum_meta.items():
            assert actual[field] == content, (field, actual)
        quoted_kind = dict(local, kind='lo"cal')
        assert run("M", quoted_kind)["kind"] == quoted_kind["kind"]
        print(f"JSON backend: {checks} real repository/source/metadata scenarios passed")


if __name__ == "__main__":
    main()
