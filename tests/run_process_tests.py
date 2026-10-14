#!/usr/bin/env python3
"""Actual process listing/startup lookup with synthetic SDK-layout sysctl bytes."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', action='store_true')
args = parser.parse_args()
os.environ.setdefault('ASAN_OPTIONS', 'detect_leaks=0:halt_on_error=1' if sys.platform == 'darwin' else 'detect_leaks=1:halt_on_error=1')
os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
with tempfile.TemporaryDirectory(prefix='pldmgr-process-') as temp:
    build = Path(temp)
    (build / 'sys').mkdir()
    # Measured with the PS5 target compiler. Production asserts these same
    # offsets against the actual SDK; this shim only supplies host types.
    (build / 'sys/proc.h').write_text('#include <stdint.h>\n')
    (build / 'sys/user.h').write_text((ROOT / 'tests/process_sdk_stub.h').read_text())
    (build / 'sys/sysctl.h').write_text('''#include <stddef.h>
#define CTL_KERN 1
#define KERN_PROC 14
#define KERN_PROC_PROC 8
int sysctl(const int *, unsigned int, void *, size_t *, const void *, size_t);
''')
    flags = shlex.split(os.environ.get('HOST_CC', 'clang')) + [
        '-std=c11', '-D_DEFAULT_SOURCE', '-g', '-O1', '-Wall', '-Wextra',
        '-I' + str(build), '-Iinclude', '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    sources = ['src/process_mgr.c']
    if args.baseline:
        def previous(path):
            return subprocess.check_output(['git', 'show', '9ffd392:' + path], cwd=ROOT, text=True)
        old = build / 'process_old.c'
        old.write_text(previous('src/process_mgr.c'))
        main = previous('src/main.c')
        function = main[main.index('static pid_t find_pid('):main.index('/* PS5 System Calls')]
        startup = build / 'startup_old.c'
        startup.write_text('#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n#include <unistd.h>\n#include <sys/sysctl.h>\n#include "pldmgr.h"\n' +
                           function.replace('static pid_t find_pid(', 'pid_t process_find_pid('))
        sources = [str(old), str(startup)]
    objects = []
    for index, source in enumerate(sources):
        obj = build / f'process-{index}.o'
        subprocess.run(flags + ['-include', 'tests/process_hooks.h', '-c', source, '-o', str(obj)], cwd=ROOT, check=True)
        objects.append(str(obj))
    binary = build / 'process_tests'
    subprocess.run(flags + objects + ['src/json_helpers.c', 'src/utils.c', 'tests/process_driver.c', '-o', str(binary)], cwd=ROOT, check=True)
    if args.baseline:
        result = subprocess.run([str(binary), 'short'], capture_output=True, text=True, timeout=5)
        assert result.returncode != 0 and ('AddressSanitizer' in result.stderr or 'runtime error' in result.stderr), result.stderr
        print('Baseline: startup lookup reads beyond a four-byte record (sanitizer reproduced)')
        result = subprocess.run([str(binary), 'short-list'], capture_output=True, text=True, timeout=5)
        assert result.returncode != 0 and ('AddressSanitizer' in result.stderr or 'runtime error' in result.stderr), result.stderr
        print('Baseline: process listing reads beyond a four-byte record (sanitizer reproduced)')
        try:
            subprocess.run([str(binary), 'zero'], capture_output=True, text=True, timeout=2)
        except subprocess.TimeoutExpired:
            print('Baseline: startup zero-size record loops indefinitely (timeout reproduced)')
        else:
            raise AssertionError('baseline zero-size startup scan did not loop')
    else:
        errors = ['short-header', 'zero', 'negative', 'short', 'oversized', 'unterminated',
                  'negative-rss', 'trailing', 'bad-after-match', 'query-fail', 'fetch-fail',
                  'query-large', 'returned-large', 'alloc-fail', 'grow-always']
        valid = ['normal', 'empty', 'grow', 'huge-rss', 'nonpositive', 'thread-only', 'max-name', 'small-output']
        for scenario in errors + valid:
            result = subprocess.run([str(binary), scenario], capture_output=True, text=True, timeout=5)
            assert result.returncode == 0, scenario + ': ' + result.stderr
            actual = json.loads(result.stdout)
            if scenario in errors:
                assert actual['found'] == -1 and actual['length'] == 0 and actual['app_calls'] == 0, (scenario, actual)
            else:
                assert actual['found'] == (-1 if scenario in ['empty', 'nonpositive', 'thread-only'] else 41), (scenario, actual)
                if scenario == 'small-output':
                    assert actual['length'] == 0, actual
                else:
                    entries = actual['data']['processes']
                    assert len(entries) == (0 if scenario == 'empty' else 5), actual
                    if entries:
                        if scenario == 'max-name':
                            assert entries[0]['name'] == 'abcdefghijklmno.elf', actual
                        if scenario == 'huge-rss':
                            assert entries[0]['memory'] > 0, actual
                        assert entries[1]['pid'] == ( -5 if scenario == 'nonpositive' else 41), actual
                        assert entries[1]['memory'] == 16.0, actual
                        assert entries[1]['is_daemon'] is True, actual
                        assert entries[2]['is_daemon'] is False and entries[3]['is_daemon'] is False, actual
                        assert entries[4]['name'] == 'a"\\\n.elf' and entries[4]['is_daemon'] is True, actual
            attempts = 3 if scenario == 'grow-always' else 2 if scenario == 'grow' else 1
            assert actual['queries'] == actual['lookup_queries'] == attempts, (scenario, actual)
            assert actual['fetches'] <= attempts and actual['lookup_fetches'] <= attempts, (scenario, actual)
            print('Process records:', scenario, 'passed')
        print(f'Process records: {len(errors) + len(valid)} sanitizer scenarios passed')
