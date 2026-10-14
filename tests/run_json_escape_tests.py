#!/usr/bin/env python3
"""Test the production encoder against Python's independent JSON/UTF-8 decoders."""
import argparse
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def chunks(data):
    """Each invalid UTF-8 byte becomes one replacement escape, by contract."""
    result = []
    while data:
        byte = data[0]
        width = 1 if byte < 128 else (2 if 0xC2 <= byte <= 0xDF else
                                     3 if 0xE0 <= byte <= 0xEF else
                                     4 if 0xF0 <= byte <= 0xF4 else 0)
        try:
            if not width or len(data) < width:
                raise UnicodeError()
            char = data[:width].decode("utf-8")
            if len(char) != 1:
                raise UnicodeError()
            result.append(json.dumps(char, ensure_ascii=False)[1:-1].encode("utf-8"))
            data = data[width:]
        except UnicodeError:
            result.append(b"\\ufffd")
            data = data[1:]
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default="src/utils.c")
    parser.add_argument("--legacy-api", action="store_true")
    parser.add_argument("--no-sanitize", action="store_true")
    args = parser.parse_args()
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    os.environ.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    samples = [b"", b'"', b"\\", b'a"b\\c', bytes(range(1, 32)), b"\x7f",
               "Zażółć gęślą jaźń / Україна / 日本語 / 😀".encode(),
               bytes.fromhex("c080eda080f4908080ff80"),
               bytes.fromhex("c2e282f09f98"),
               "¢€😀".encode(),
               "\u0080\u07ff\u0800\ud7ff\ue000\uffff\U00010000\U0010ffff".encode(),
               b"x" * 1024, b"\x01" * 600]
    # Every small output capacity, including one byte below complete escapes
    # and multibyte scalars. Fixed seed adds mixed valid and invalid inputs.
    cases = []
    for sample in samples:
        full_size = sum(map(len, chunks(sample)))
        capacities = range(min(full_size + 3, 150))
        boundaries = {4096, full_size, full_size + 1, full_size + 2}
        for capacity in sorted(set(capacities) | {size for size in boundaries if size <= 4096}):
            cases.append((sample, capacity))
    rng = random.Random(20261012)
    for _ in range(1000):
        sample = bytes(rng.randrange(1, 256) for _ in range(rng.randrange(1, 80)))
        cases.append((sample, rng.randrange(0, 300)))
    compiler = shlex.split(os.environ.get("HOST_CC", "clang"))
    with tempfile.TemporaryDirectory(prefix="pldmgr-json-tests-") as temp:
        build = Path(temp)
        header = (ROOT / "include/utils.h").read_text()
        if args.legacy_api:
            header = header.replace("int pldmgr_json_escape", "void pldmgr_json_escape")
        (build / "utils.h").write_text(header)
        command = compiler + ["-std=c11", "-D_DEFAULT_SOURCE", "-g", "-O1", "-Wall", "-Wextra",
                              "-I" + str(build), "-Iinclude"]
        if not args.no_sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        if args.legacy_api:
            command += ["-DLEGACY_ESCAPE"]
        binary = build / "json_escape_tests"
        subprocess.run(command + [args.source, "tests/json_escape_driver.c", "-o", str(binary)],
                       cwd=ROOT, check=True)
        protocol = "".join(f"{size} {data.hex() or '-'}\n" for data, size in cases)
        output = subprocess.run([str(binary)], input=protocol, text=True,
                                capture_output=True, check=True).stdout.splitlines()
    assert len(output) == len(cases)
    failures = []
    for (data, capacity), line in zip(cases, output):
        status, hex_result = line.split(" ", 1)
        actual = bytes.fromhex(hex_result)
        expected = b""
        for chunk in chunks(data):
            if len(expected) + len(chunk) >= capacity:
                break
            expected += chunk
        full = b"".join(chunks(data))
        expected_status = 0 if capacity and expected == full else -1
        try:
            if not args.legacy_api:
                assert int(status) == expected_status
            assert actual == expected
            if capacity:
                decoded = json.loads(b'"' + actual + b'"')
                assert decoded == json.loads(b'"' + expected + b'"')
        except (AssertionError, ValueError, UnicodeError) as error:
            failures.append((data.hex(), capacity, actual.hex(), str(error)))
    if failures:
        for failure in failures[:8]:
            print("FAIL input/capacity/output:", failure)
        raise SystemExit(f"JSON encoder: {len(failures)} failures / {len(cases)} cases")
    print(f"JSON encoder: {len(cases)} independent decoding/bounds checks passed")


if __name__ == "__main__":
    main()
