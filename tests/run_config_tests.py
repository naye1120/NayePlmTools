#!/usr/bin/env python3
"""Actual config/source persistence and history under faults and concurrent access."""
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
parser.add_argument('--thread-sanitize', action='store_true')
parser.add_argument('--legacy', action='store_true')
parser.add_argument('--legacy-history', action='store_true')
args = parser.parse_args()
if args.legacy_history and not args.thread_sanitize: parser.error("--legacy-history requires --thread-sanitize")
os.environ.setdefault('ASAN_OPTIONS', ('detect_leaks=0:halt_on_error=1' if sys.platform == 'darwin' else 'detect_leaks=1:halt_on_error=1'))
os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
os.environ.setdefault('TSAN_OPTIONS', 'halt_on_error=1')
with tempfile.TemporaryDirectory(prefix='pldmgr-config-') as temp:
    build = Path(temp)
    storage = build / 'data'
    storage.mkdir()
    (build / 'pldmgr.h').write_text((ROOT / 'include/pldmgr.h').read_text().replace('/data/pldmgr', str(storage)).replace('/data/nayeplmtools', str(storage)))
    (build / 'assets_cacert_pem.h').write_text('static const unsigned char assets_cacert_pem[]={0};\nstatic const unsigned int assets_cacert_pem_len=0;\n')
    flags = shlex.split(os.environ.get('HOST_CC', 'clang')) + ['-std=c11', '-D_DEFAULT_SOURCE', '-g', '-O1', '-Wall', '-Wextra', '-pthread', '-ffunction-sections', '-fdata-sections', '-I'+str(build), '-Iinclude', '-Isrc/psaccounts/third_party']
    flags += ['-fsanitize=thread' if args.thread_sanitize else '-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    objects = []
    for name in ['config', 'sources', 'history_mgr', 'json_helpers', 'utils', 'sha256', 'slot', 'repository']:
        obj = build / (name+'.o')
        source = f'src/{name}.c'
        extra = ['-include', 'tests/config_hooks.h'] if name == 'json_helpers' else []
        if args.legacy and name == 'config':
            # Adapt only the old void remap symbol, unused by the scalar reproduction.
            snapshot = build / 'config_legacy.c'
            old = subprocess.check_output(['git', 'show', 'cc0fc90:src/config.c'], cwd=ROOT, text=True)
            old = old.replace('void pldmgr_favorites_update_entry', 'void legacy_favorites_update_entry')
            old += '\nint config_update_list_entry(const char *p,const char *a,const char *b) {(void)p;(void)a;(void)b;return 0;}\nint pldmgr_favorites_update_entry(const char *a,const char *b) {(void)a;(void)b;return 0;}\n'
            snapshot.write_text(old)
            source = str(snapshot)
        if args.legacy_history and name == 'history_mgr':
            snapshot = build / 'history_legacy.c'
            snapshot.write_text(subprocess.check_output(['git', 'show', 'cc0fc90:src/history_mgr.c'], cwd=ROOT, text=True))
            source = str(snapshot)
        if name == 'repository': extra += ['-Ddownload_to_file=config_unused_download', '-Ddownload_to_file_limited=config_unused_download_limited']
        subprocess.run(flags+extra+['-c', source, '-o', str(obj)], cwd=ROOT, check=True)
        objects.append(str(obj))
    binary = build / 'config_tests'
    gc = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
    subprocess.run(flags+objects+['tests/config_driver.c', 'src/psaccounts/third_party/cJSON.c', gc, '-lcurl', '-o', str(binary)], cwd=ROOT, check=True)
    cases = ['history'] if args.legacy_history else (['scalar'] if args.legacy else (['settings', 'history', 'sources'] if args.thread_sanitize else ['scalar','validation','atomic','settings','history','sources']))
    for case in cases:
        # Independent clean storage for each scenario.
        for path in storage.iterdir():
            if path.is_file(): path.unlink()
        result = subprocess.run([str(binary), case], cwd=ROOT, text=True, capture_output=True)
        if args.legacy_history:
            assert result.returncode and 'ThreadSanitizer: data race' in result.stderr, result.stderr
            print('Previous history implementation reproduces a ThreadSanitizer data race')
            continue
        if args.legacy:
            assert result.returncode and 'cfg.auto_browser_open==0' in result.stderr, result.stderr
            print('Previous implementation reproduces ignored individual setting: '+result.stderr.strip().splitlines()[0])
            continue
        if result.returncode:
            print(result.stdout); print(result.stderr, file=sys.stderr)
            raise SystemExit(result.returncode)
        start = result.stdout.find('{')
        if start >= 0:
            document, _ = json.JSONDecoder().raw_decode(result.stdout[start:])
            if case == 'history':
                expected = ['local/item-10']+[f'local/item-{i}' for i in range(24,4,-1) if i != 10]
                assert document['history'] == expected, document
            if case == 'sources':
                sources = document['sources']
                assert len(sources)==13 and len({s['id'] for s in sources})==13
                assert all(s['name']=='Fíxture' for s in sources[1:])
                assert {s['url'] for s in sources[1:]} == {f'fixture://source-{i}' for i in range(12)}
        print('\n'.join(line for line in result.stdout.splitlines() if line.startswith(('Config','Atomic','History','Source'))))
    print('Config/history/source checks passed'+(' under ThreadSanitizer' if args.thread_sanitize else ''))
