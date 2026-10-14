#!/usr/bin/env python3
"""Actual joinable worker, configuration files and concurrent API readers; fake loader only."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--thread-sanitize',action='store_true')
parser.add_argument('--legacy',action='store_true')
args=parser.parse_args()
os.environ.setdefault('ASAN_OPTIONS','detect_leaks=0:halt_on_error=1' if sys.platform=='darwin' else 'detect_leaks=1:halt_on_error=1')
os.environ.setdefault('UBSAN_OPTIONS','halt_on_error=1:print_stacktrace=1')
os.environ.setdefault('TSAN_OPTIONS','halt_on_error=1')
with tempfile.TemporaryDirectory(prefix='pldmgr-autoload-') as temp:
    build=Path(temp);storage=build/'data';storage.mkdir()
    (build/'pldmgr.h').write_text((ROOT/'include/pldmgr.h').read_text().replace('/data/pldmgr',str(storage)).replace('/data/nayeplmtools',str(storage)))
    flags=shlex.split(os.environ.get('HOST_CC','clang'))+['-std=c11','-D_DEFAULT_SOURCE','-g','-O1','-Wall','-Wextra','-pthread','-ffunction-sections','-fdata-sections','-I'+str(build),'-Iinclude','-Isrc/psaccounts/third_party', '-DPLDMGR_FRONTEND_WAIT_MS=2000', '-fno-omit-frame-pointer', '-fsanitize=thread' if args.thread_sanitize else '-fsanitize=address,undefined']
    objects=[]
    for name in ['autoload','config','json_helpers','utils']:
        source=f'src/{name}.c';extra=['-include','tests/autoload_hooks.h'] if name=='autoload' else []
        if args.legacy and name=='autoload':
            snapshot=build/'autoload_legacy.c'
            old=subprocess.check_output(['git','show','356a18a:src/autoload.c'],cwd=ROOT,text=True)
            old+='''\nvoid pldmgr_autoload_stop(void) {}
void pldmgr_autoload_frontend_ready(void) { int total,done; pldmgr_autoload_get_status(&total,&done,0); }
void pldmgr_autoload_get_snapshot(AutoloadSnapshot *out) {
    memset(out,0,sizeof(*out));
    pldmgr_autoload_get_status(&out->total,&out->done,out->current);
    out->remaining=pldmgr_autoload_get_remaining_seconds();
    out->state=!strcmp(out->current,"DONE") ? AUTOLOAD_DONE : AUTOLOAD_EXECUTING;
}
'''
            snapshot.write_text(old);source=str(snapshot)
        obj=build/(name+'.o');subprocess.run(flags+extra+['-c',source,'-o',str(obj)],cwd=ROOT,check=True);objects.append(str(obj))
    binary=build/'autoload_tests';subprocess.run(flags+objects+['tests/autoload_driver.c','-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections','-o',str(binary)],cwd=ROOT,check=True)
    cases=['counters'] if args.legacy else ['counters','reset','wait','delay','progress','send','snapshot','invalid','concurrency','thread','unicode','allocation','timeout']
    for case in cases:
        result=subprocess.run([str(binary),case],cwd=ROOT,text=True,capture_output=True,timeout=15)
        if args.legacy:
            assert result.returncode and ('snapshot.total==4' in result.stderr or (args.thread_sanitize and 'ThreadSanitizer: data race' in result.stderr)),result.stdout+result.stderr
            print('Previous autoload reproduces '+('a ThreadSanitizer data race' if 'ThreadSanitizer: data race' in result.stderr else 'inconsistent payload/delay/blank counters'))
        else:
            assert result.returncode==0,result.stdout+result.stderr
            print(result.stdout.strip())
