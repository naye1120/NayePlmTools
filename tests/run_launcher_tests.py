#!/usr/bin/env python3
"""Actual nonblocking launcher with a socketpair byte recorder; no real ELF loader."""
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
os.environ.setdefault('ASAN_OPTIONS','detect_leaks=0:halt_on_error=1' if sys.platform=='darwin' else 'detect_leaks=1:halt_on_error=1')
os.environ.setdefault('UBSAN_OPTIONS','halt_on_error=1:print_stacktrace=1')
with tempfile.TemporaryDirectory(prefix='pldmgr-loader-') as temp:
    build=Path(temp)
    flags=shlex.split(os.environ.get('HOST_CC','clang'))+['-std=c11','-D_DEFAULT_SOURCE','-g','-O1','-Wall','-Wextra','-pthread','-ffunction-sections','-fdata-sections','-Iinclude','-DPLDMGR_LOADER_CONNECT_MS=200','-DPLDMGR_LOADER_TRANSFER_MS=1000','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    obj=build/'launcher.o';binary=build/'launcher_tests'
    subprocess.run(flags+['-include','tests/launcher_hooks.h','-c','src/ps5_launcher.c','-o',str(obj)],cwd=ROOT,check=True)
    subprocess.run(flags+[str(obj),'tests/launcher_driver.c','-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections','-o',str(binary)],cwd=ROOT,check=True)
    small=build/'small.elf';small.write_bytes(bytes(range(256))*80)
    big=build/'big.elf';big.write_bytes(bytes(range(256))*4096)
    for case in ['ok','partial','interrupt','socket','fcntl','connect','socket-error','read','zero','connect-stall','send-stall','cancel-before','cancel-send','cancel-wait','disc-none','disc-other','disc-title-error','disc-suspend-error','disc-ok','disc-no-pid','disc-kill-error','disc-gone']:
        fixture=big if case in ['send-stall','cancel-wait'] else small
        result=subprocess.run([str(binary),case,str(fixture)],text=True,capture_output=True,timeout=10)
        assert result.returncode==0,result.stdout+result.stderr
        print(result.stdout.strip())
