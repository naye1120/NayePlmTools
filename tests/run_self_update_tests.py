#!/usr/bin/env python3
"""Self-update storage and loader handoff with local fixtures and recorded sockets."""
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
os.environ.setdefault('ASAN_OPTIONS', 'detect_leaks=1:halt_on_error=1')
os.environ.setdefault('UBSAN_OPTIONS', 'halt_on_error=1:print_stacktrace=1')
with tempfile.TemporaryDirectory(prefix='pldmgr-self-') as temp:
    build = Path(temp)
    data = build / 'data'
    payloads = data / 'payloads'
    payloads.mkdir(parents=True)
    header = (ROOT / 'include/pldmgr.h').read_text()
    current = re.search(r'#define MENU_VERSION "([^"]+)"', header)[1]
    official = re.search(r'#define REPOSITORY_SOURCE_URL\s*\\?\s*"([^"]+)"', header)[1]
    (build / 'pldmgr.h').write_text(header.replace('/data/pldmgr', str(data)).replace('/data/nayeplmtools', str(data)))
    (build / 'assets_cacert_pem.h').write_text('static const unsigned char assets_cacert_pem[]={0};\nstatic const unsigned int assets_cacert_pem_len=0;\n')
    flags = shlex.split(os.environ.get('HOST_CC', 'clang')) + [
        '-std=c11', '-D_DEFAULT_SOURCE', '-g', '-O1', '-Wall', '-Wextra', '-pthread',
        '-ffunction-sections', '-fdata-sections', '-fsanitize=address,undefined',
        '-fno-omit-frame-pointer', '-I'+str(build), '-Iinclude', '-Isrc/psaccounts/third_party',
        '-DPLDMGR_USB_ROOT_FORMAT="'+str(build / 'usb%d')+'"']
    objects = []
    for name in ['repository', 'sources', 'slot', 'utils', 'json_helpers', 'sha256', 'config', 'payload_mgr', 'ps5_launcher']:
        extra = []
        if name == 'repository':
            extra = ['-Ddownload_to_file=unused_download', '-Ddownload_to_file_limited=unused_limited_download',
                     '-Drenameat=self_test_renameat', '-Dfsync=self_test_fsync']
        if name == 'ps5_launcher':
            extra = ['-Dsocket=self_test_socket', '-Dconnect=self_test_connect', '-Dsend=self_test_send']
        obj = build / (name+'.o')
        subprocess.run(flags+extra+['-c', f'src/{name}.c', '-o', str(obj)], cwd=ROOT, check=True)
        objects.append(str(obj))
    binary = build / 'self_tests'
    subprocess.run(flags+objects+['tests/self_update_driver.c', '-Wl,--gc-sections', '-lcurl', '-o', str(binary)], cwd=ROOT, check=True)
    def run(*args, **env):
        return subprocess.run([str(binary), *map(str, args)], env={**os.environ, **env}, capture_output=True, text=True, check=True).stdout.strip()
    def elf(version, prefix=0):
        return b'\x7fELF'+b'x'*prefix+b'PLDMGR_VER:'+version.encode()+b'\0trailer'
    legacy = payloads / 'pldmgr'
    legacy.mkdir()
    alias = payloads / 'PAYLOAD-MANAGER'
    alias.mkdir()
    equal = legacy / 'pldmgr_equal.elf'
    equal.write_bytes(elf(current))
    (legacy / 'pldmgr_lower.elf').write_bytes(elf('0.0.1'))
    (legacy / 'pldmgr_bad.elf').write_bytes(b'not ELF PLDMGR_VER:999.0.0\0')
    (legacy / 'pldmgr_invalid.elf').write_bytes(elf('garbage'))
    (legacy / 'pldmgr_prefixed.elf').write_bytes(elf('v999.0.0'))
    (legacy / 'pldmgr_missing.elf').write_bytes(b'\x7fELFmissing version')
    (legacy / 'pldmgr_overlong.elf').write_bytes(elf('9'*70))
    (legacy / 'pldmgr_unterminated.elf').write_bytes(b'\x7fELFPLDMGR_VER:999.0.0')
    assert run('scan').splitlines()[0] == '0 '+current
    assert run('self', equal) == '-1 0 0'
    low = legacy / 'pldmgr_v90.0.0.elf'
    low.write_bytes(elf('90.0.0'))
    high = alias / 'payload-manager_v91.0.0.bin'
    high.write_bytes(elf('91.0.0', 8179))  # marker crosses a read boundary
    assert run('scan').splitlines() == ['1 91.0.0', str(high)]
    assert run('self', high) == '0 1 0'
    assert run('launch', high) == '-1 0 0'
    assert run('self', high, FAIL_LAUNCH='1') == '-1 1 0'
    external = build / 'pldmgr_external.elf'
    external.write_bytes(elf('999.0.0'))
    link = legacy / 'pldmgr_link.elf'
    link.symlink_to(external)
    assert run('self', link) == '-1 0 0'
    assert run('self', external) == '-1 0 0'
    assert run('migrate') == '0 0'
    assert equal.exists() and high.exists() and low.exists()
    listing = json.loads(run('list'))
    assert listing['self'] == {'id': 'pldmgr', 'running_version': current, 'pending_version': '91.0.0'}
    assert not listing['items'] and not listing['payloads']

    fixture = build / 'download.elf'
    fixture.write_bytes(elf('92.0.0'))
    entry = dict(name='pldmgr', filename='pldmgr_v92.0.0.elf', version='v92.0.0', url='fixture://update', checksum=hashlib.sha256(fixture.read_bytes()).hexdigest())
    def cache(url, item):
        path = data / ('repo_cache_'+hashlib.sha256(url.rstrip('/').encode()).hexdigest()[:8]+'.json')
        path.write_text(json.dumps([item]))
    cache(official, entry)
    custom = 'https://custom.invalid/payloads.json'
    cache(custom, entry)
    (data / 'sources.json').write_text(json.dumps({'sources': [{'id': 'custom', 'name': 'Custom', 'url': custom}]}))
    (data / 'nayeplmtools_config.txt').write_text('MULTI_SOURCES_ENABLED=1\n')
    catalog = json.loads(run('catalog'))
    assert [p['id'] for p in catalog['sources'][0]['payloads']] == ['pldmgr']
    assert not catalog['sources'][1]['payloads']
    assert not json.loads(run('install', custom))['ok']
    assert not json.loads(run('install', official, 'keep_both'))['ok']
    assert not json.loads(run('install', official, 'replace'))['ok']
    for bad in [dict(entry, checksum=''), dict(entry, checksum='a'*64), dict(entry, version='93.0.0'),
                dict(entry, filename='../pldmgr.elf'), dict(entry, version=current)]:
        cache(official, bad)
        assert not json.loads(run('install', official, UPDATE_FIXTURE=str(fixture)))['ok'], bad
        assert low.exists() and high.exists()
    cache(official, entry)
    for fault in ['FAIL_RENAME', 'FAIL_SYNC']:
        assert not json.loads(run('install', official, UPDATE_FIXTURE=str(fixture), **{fault: '1'}))['ok']
        assert low.read_bytes() == elf('90.0.0') and high.read_bytes() == elf('91.0.0', 8179)
    result = json.loads(run('install', official+'/', UPDATE_FIXTURE=str(fixture)))
    assert result['ok'] and result['self_update'] and result['restart_required'], result
    installed = legacy / entry['filename']
    assert installed.read_bytes() == fixture.read_bytes()
    assert list(legacy.iterdir()) == [installed]
    assert not json.loads(run('install', official, UPDATE_FIXTURE=str(fixture)))['ok']
    assert run('scan').splitlines() == ['1 92.0.0', str(installed)]
    assert not list(payloads.glob('.download-*'))
    assert json.loads(run('list'))['self']['pending_version'] == '92.0.0'
    assert run('self', installed) == '0 1 0'
    assert run('launch', installed) == '-1 0 0'
    usb = build / 'usb0' / 'nayeplmtools'
    usb.mkdir(parents=True)
    for name in ['pldmgr_v99.0.0.elf', 'payload-manager.bin', 'PAYLOAD_MANAGER.elf']:
        path = usb / name
        path.write_bytes(elf('99.0.0'))
        assert run('guards', name, path) == '-1 -1 -1 -1'
        assert path.exists()
    print('Self-update: official-only install, validation, pending status, migration, guards and recorded handoff passed')
