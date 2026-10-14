#!/usr/bin/env python3
"""Decode actual SSE callback bytes independently, including tiny read buffers."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--thread-sanitize', action='store_true')
args = parser.parse_args()
os.environ.setdefault('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1' if sys.platform == 'darwin' else 'detect_leaks=1:halt_on_error=1')
os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
os.environ.setdefault('TSAN_OPTIONS', 'halt_on_error=1')

def decode(raw):
    events = []
    for frame in raw.decode().split('\n\n')[:-1]:
        data, event, identity = [], 'message', None
        for line in frame.split('\n'):
            field, _, value = line.partition(':')
            if value.startswith(' '): value = value[1:]
            if field == 'data': data.append(value)
            if field == 'event': event = value
            if field == 'id': identity = int(value)
        if data: events.append((event, identity, '\n'.join(data)))
    return events

def expected(start, end):
    return [('message', i + 1, f'entry {i:03d} {i:0180d}') for i in range(start, end)]

with tempfile.TemporaryDirectory(prefix='pldmgr-sse-') as temp:
    build = Path(temp)
    (build / 'microhttpd.h').write_text('#include <sys/types.h>\n#define MHD_CONTENT_READER_END_OF_STREAM (-1)\n#define MHD_CONTENT_READER_END_WITH_ERROR (-2)\n')
    source = 'src/log_server.c'
    if args.baseline:
        for file in ['src/log_server.c', 'include/log_server.h']:
            (build / Path(file).name).write_text(subprocess.check_output(['git', 'show', 'eb10907:' + file], cwd=ROOT, text=True))
        source = str(build / 'log_server.c')
    flags = shlex.split(os.environ.get('HOST_CC', 'clang')) + ['-std=c11', '-D_DEFAULT_SOURCE', '-g', '-O1', '-Wall', '-Wextra', '-pthread', '-I' + str(build), '-Iinclude',
                '-DPLDMGR_LOG_KEEPALIVE_MS=100', '-fno-omit-frame-pointer', '-fsanitize=thread' if args.thread_sanitize else '-fsanitize=address,undefined']
    if args.baseline: flags += ['-DLEGACY_SSE']
    binary = build / 'log_tests'
    subprocess.run(flags + [source, 'src/utils.c', 'src/json_helpers.c', 'tests/log_stream_driver.c', '-o', str(binary)], cwd=ROOT, check=True)
    def run(scenario, capacity=7, *extra):
        result = subprocess.run([str(binary), scenario, str(capacity), *extra], text=True, capture_output=True, timeout=10)
        assert result.returncode == 0, result.stderr
        return {line.split(' ', 1)[0]: line.split(' ', 1)[1] for line in result.stdout.splitlines()}
    if args.baseline:
        actual = bytes.fromhex(run('legacy-catchup')['RESULT'])
        assert actual.count(b'data: entry') < 81
        print('Baseline: >1 KiB initial history is dropped after first read')
        assert int(run('legacy-small')['OVERSIZE']) > 1
        print('Baseline: live callback returns more bytes than the output capacity')
        result = subprocess.run([str(binary), 'legacy-ping', '1'], capture_output=True, text=True, timeout=10)
        assert result.returncode != 0 and 'AddressSanitizer' in result.stderr, result.stderr
        print('Baseline: idle keepalive writes beyond a one-byte output buffer (ASan)')
    else:
        checks = 0
        for capacity in [1, 7, 8, 64, 1024]:
            for scenario in ['catchup', 'burst', 'rollover', 'partial-rollover', 'multiline', 'newlines', 'ping']:
                output = run(scenario, capacity)
                raw = bytes.fromhex(output['RESULT']); actual = decode(raw)
                if scenario == 'catchup': assert actual == expected(0, 80)
                if scenario == 'burst': assert actual == decode(bytes.fromhex(output['SECOND'])) == expected(0, 90)
                if scenario in ['rollover', 'partial-rollover']:
                    if scenario == 'partial-rollover': assert actual.pop(0) == expected(0, 1)[0]
                    assert actual.pop(0) == ('gap', None, '[PLDMGR] 40 log entries were overwritten before delivery')
                    assert actual == expected(40, 140) if scenario == 'rollover' else actual == expected(41, 141)
                if scenario == 'multiline': assert actual == [('message', 1, 'first\n\nlast\nevent: injected\ndata: text')]
                if scenario == 'newlines': assert actual == [('message', 1, '\n' * 254 + 'X')]
                if scenario == 'ping': assert raw == b': ping\n\n' and actual == []
                checks += 1
            print(f'SSE: capacity {capacity}, history/bursts/two clients/rollover/partial/multiline/ping passed')
        assert decode(bytes.fromhex(run('resume')['RESULT'])) == expected(3, 5); checks += 1
        for identity in ['', '-1', ' 1', 'x', '2', '18446744073709551616', '999999999999999999999999']:
            assert decode(bytes.fromhex(run('id', 7, identity)['RESULT'])) == expected(0, 1); checks += 1
        for scenario in ['stop', 'partial-stop', 'zero-capacity']:
            assert decode(bytes.fromhex(run(scenario)['RESULT'])) == expected(0, 1); checks += 1
        run('concurrent'); checks += 1
        print(f'SSE: {checks} decoding/bounds/lifecycle/concurrency scenarios passed')
