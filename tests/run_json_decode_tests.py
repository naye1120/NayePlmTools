#!/usr/bin/env python3
"""Bounded production JSON decoding, with Python supplying expected UTF-8."""
import argparse
import json
import os
from pathlib import Path
import random
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", default="src/json_helpers.c")
    parser.add_argument("--legacy-api", action="store_true")
    parser.add_argument("--no-sanitize", action="store_true")
    args = parser.parse_args()
    os.environ.setdefault("ASAN_OPTIONS", "detect_leaks=1:halt_on_error=1")
    os.environ.setdefault("UBSAN_OPTIONS", "halt_on_error=1:print_stacktrace=1")
    cases = []

    def add(document, key="value", value=None, error=None, capacities=(4096,), bound=None):
        if isinstance(document, str):
            document = document.encode()
        if bound is None:
            bound = len(document)
        for capacity in capacities:
            for mode in ("S", "P"):
                if error is not None:
                    expected_status, expected = error, b""
                else:
                    data = value.encode()
                    prefix = b""
                    for char in value:
                        scalar = char.encode()
                        if len(prefix) + len(scalar) >= capacity:
                            break
                        prefix += scalar
                    if not capacity:
                        expected_status, expected = -2, b""
                    elif prefix == data:
                        expected_status, expected = 0, data
                    elif mode == "S":
                        expected_status, expected = -3, b""
                    else:
                        expected_status, expected = 1, prefix
                cases.append((mode, capacity, bound, document, key, expected_status, expected))

    values = ["", 'quote" backslash\\ slash/', "\b\f\n\r\t", "".join(map(chr, range(1, 32))),
              "Zażółć Україна 日本語 😀", "¢€😀", "\u0080\u07ff\u0800\ud7ff\ue000\uffff\U0010ffff"]
    for value in values:
        for ascii_only in (True, False):
            add(json.dumps({"value": value}, ensure_ascii=ascii_only), value=value)
            document = json.dumps({"nested": {"value": "wrong"}, "value": value,
                                   "tail": [True, False, None, -1.25e3, {"x": ":{}[]"}]},
                                  ensure_ascii=ascii_only)
            add(document, value=value, capacities=range(len(value.encode()) + 3))
    add('{"\\u0076alue":"escaped key"}', value="escaped key")
    for key in ['quo"te\\日本語', 'k' * 150, '']:
        add(json.dumps({key: "correct"}), key=key, value="correct")
    add('{"x":"value","value":"correct"}', value="correct")
    add('{"nested":{"value":"wrong"}}', error=-1)
    add('{"value2":"wrong"}', error=-1)
    add('{}', error=-1)
    add('{"value":"first","value":"second"}', error=-2)
    add('{"value":123}', error=-2)
    add('{"value":null}', error=-2)
    add('{"value":"ok","unrelated":"\\u0000"}', value="ok")
    add('{"value":"\\u0000"}', error=-2)
    malformed = [b'{"value":"unterminated}', b'{"value":"x\\"}',
                 b'{"value":"\\q"}', b'{"value":"\\u123"}', b'{"value":"\\uxxxx"}',
                 b'{"value":"\\ud800"}', b'{"value":"\\udc00"}',
                 b'{"value":"\\ud800\\u0041"}', b'{"value":"\x00"}', b'{"value":"\x01"}',
                 b'{"value":"\xc0\x80"}', b'{"value":"\xed\xa0\x80"}',
                 b'{"value":"\xf4\x90\x80\x80"}', b'{"value":"\xe2\x82"}',
                 b'{"value":"ok",}', b'{"value":"ok"}junk', b'{"value":"ok","x":01}',
                 b'{"value":"ok","x":1.}', b'{"value":"ok","x":1e}',
                 b'{"value":"ok","x":tru}', b'{"value":"ok","x":[1,]}',
                 b'{"value":"ok","x":{"k":"\\q"}}', b'[]', b'', b'\v{"value":"ok"}']
    for document in malformed:
        add(document, error=-2, capacities=(1, 4096))
    # End can cut at every byte, including inside Unicode escapes/scalars.
    document = '{"value":"😀\\n\\ud83d\\ude00"}'.encode()
    for bound in range(len(document)):
        add(document, error=-2, bound=bound)
    add(b'{"value":"ok"} trailing bytes', value="ok", bound=len(b'{"value":"ok"}'))
    # A missing key must not be found in data after the supplied bound.
    add(b'{} {"value":"outside"}', error=-1, bound=2)
    add('{"value":"ok","x":' + '[' * 63 + '0' + ']' * 63 + '}', value="ok")
    add('{"value":"ok","x":' + '[' * 64 + '0' + ']' * 64 + '}', error=-2)
    rng = random.Random(20261012)
    alphabet = ['a', '"', '\\', '\n', '\t', '\x01', 'ż', 'Я', '日', '😀']
    for _ in range(500):
        value = "".join(rng.choice(alphabet) for _ in range(rng.randrange(60)))
        document = json.dumps({"value": value, "nested": ["value", {"value": "wrong"}]},
                              ensure_ascii=rng.choice([True, False]))
        add(document, value=json.loads(document)["value"], capacities=(rng.randrange(200),))
    # Validate object framing independently of subsequent array members.
    if not args.legacy_api:
        documents = [(b'{"value":"} { \\\"", "nested":{"x":[]}}, {"next":true}', 0),
                     (b'{"x":[1,{"z":"}"}]}]', 0), (b'{"x":[1,]}', -1)]
        for document, status in documents:
            if status == 0:
                _, character_end = json.JSONDecoder().raw_decode(document.decode())
                end = len(document.decode()[:character_end].encode())
            else:
                end = 0
            cases.append(("E", 4096, len(document), document, "", status, str(end).encode()))
            if status == 0:
                for bound in range(end):
                    cases.append(("E", 4096, bound, document, "", -1, b"0"))
    compiler = shlex.split(os.environ.get("HOST_CC", "clang"))
    with tempfile.TemporaryDirectory(prefix="pldmgr-json-decode-") as temp:
        binary = Path(temp) / "json_decode_tests"
        command = compiler + ["-std=c11", "-D_DEFAULT_SOURCE", "-g", "-O1", "-Wall", "-Wextra", "-Iinclude"]
        if not args.no_sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        if args.legacy_api:
            command += ["-DLEGACY_DECODE"]
        subprocess.run(command + [args.source, "src/utils.c", "tests/json_decode_driver.c", "-o", str(binary)],
                       cwd=ROOT, check=True)
        protocol = "".join(f"{mode} {capacity} {bound} {doc.hex() or '-'} {key.encode().hex() or '-'}\n"
                           for mode, capacity, bound, doc, key, _, _ in cases)
        process = subprocess.run([str(binary)], input=protocol, text=True, capture_output=True)
        if process.returncode:
            raise SystemExit(process.stderr or f"Decoder driver failed: {process.returncode}")
        output = process.stdout.splitlines()
    assert len(output) == len(cases)
    failures = []
    for case, line in zip(cases, output):
        mode, capacity, bound, doc, key, expected_status, expected = case
        status, result = line.split(" ", 1)
        actual = result.encode() if mode == "E" else bytes.fromhex(result)
        # Old status codes differ; compare content and success/error semantics.
        status_ok = int(status) == expected_status if not args.legacy_api else ((int(status) == 0) == (expected_status >= 0))
        if not status_ok or actual != expected:
            failures.append((doc[:120], mode, capacity, bound, int(status), actual, expected_status, expected))
    if failures:
        for failure in failures[:8]:
            print("FAIL document/mode/capacity/bound/status/output/expected:", failure)
        raise SystemExit(f"JSON decoder: {len(failures)} failures / {len(cases)} cases")
    print(f"JSON decoder: {len(cases)} bounded decoding/selection checks passed")


if __name__ == "__main__":
    main()
