#!/usr/bin/env python3
"""Actual TLS/download, manifest hash validation, and a recorded loader socket."""
import argparse
import hashlib
import http.server
import json
import os
import re
from pathlib import Path
import shlex
import ssl
import subprocess
import tempfile
import threading
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--legacy',action='store_true')
args=parser.parse_args()
os.environ.setdefault('ASAN_OPTIONS','detect_leaks=1:halt_on_error=1')
os.environ.setdefault('UBSAN_OPTIONS','halt_on_error=1:print_stacktrace=1')
checks=0
with tempfile.TemporaryDirectory(prefix='pldmgr-integrity-') as temp:
    build=Path(temp);data=build/'data';data.mkdir()
    (build/'pldmgr.h').write_text((ROOT/'include/pldmgr.h').read_text().replace('/data/pldmgr',str(data)).replace('/data/nayeplmtools',str(data)))
    def openssl(*params):
        subprocess.run(['openssl',*params],cwd=build,check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    openssl('req','-x509','-newkey','rsa:2048','-nodes','-keyout','ca.key','-out','ca.pem','-subj','/CN=Fixture CA','-days','2')
    def certificate(name,host='localhost',days='2',self_signed=False):
        openssl('req','-newkey','rsa:2048','-nodes','-keyout',name+'.key','-out',name+'.csr','-subj','/CN='+host)
        (build/(name+'.ext')).write_text('subjectAltName=DNS:'+host+(',IP:127.0.0.1' if host=='localhost' else '')+'\n')
        if self_signed:
            openssl('x509','-req','-in',name+'.csr','-signkey',name+'.key','-out',name+'.pem','-days',days,'-extfile',name+'.ext')
        else:
            openssl('x509','-req','-in',name+'.csr','-CA','ca.pem','-CAkey','ca.key','-CAcreateserial','-out',name+'.pem','-days',days,'-extfile',name+'.ext')
    for name,host,days,self_signed in [('trusted','localhost','2',False),('untrusted','localhost','2',True),('wronghost','other.invalid','2',False),('expired','localhost','-1',False)]:
        certificate(name,host,days,self_signed)
    ca=(build/'ca.pem').read_bytes()
    (build/'assets_cacert_pem.h').write_text('static const unsigned char assets_cacert_pem[]={'+','.join(str(x) for x in ca)+'};\nstatic const unsigned int assets_cacert_pem_len='+str(len(ca))+';\n')
    flags=shlex.split(os.environ.get('HOST_CC','clang'))+['-std=c11','-D_DEFAULT_SOURCE','-g','-O1','-Wall','-Wextra','-ffunction-sections','-fdata-sections','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-I'+str(build),'-Iinclude','-Isrc/psaccounts/third_party','-DPLDMGR_USB_ROOT_FORMAT="'+str(build/'usb%d')+'"']
    if args.legacy:flags += ["-DLEGACY_INTEGRITY"]
    objects=[]
    for name in ['repository','slot','utils','json_helpers','sha256','config','ps5_launcher']:
        obj=build/(name+'.o');source=f'src/{name}.c'
        if args.legacy and name in ['repository','slot','ps5_launcher']:source=f'scratch/{name}_before_step4a.c'
        extra=['-Dsocket=integrity_socket','-Dconnect=integrity_connect','-Dsend=integrity_send'] if name=='ps5_launcher' else []
        if name=='slot' and not args.legacy:extra += ['-Dopenat=integrity_openat']
        if name=='repository' and not args.legacy:extra += ['-include','tests/integrity_io_hooks.h']
        subprocess.run(flags+extra+['-c',source,'-o',str(obj)],cwd=ROOT,check=True);objects.append(str(obj))
    binary=build/'integrity_tests'
    subprocess.run(flags+objects+['tests/integrity_driver.c','-Wl,--gc-sections','-lcurl','-o',str(binary)],cwd=ROOT,check=True)
    def run(*params, fault=None, swap=None, target=None):
        env=os.environ.copy()
        if fault:env['PLDMGR_IO_FAULT']=fault
        if swap:env.update(PLDMGR_SWAP_FILE=str(swap),PLDMGR_SWAP_TARGET=str(target))
        result=subprocess.run([str(binary),*map(str,params)],capture_output=True,text=True,check=True,env=env)
        return [int(x) for x in result.stdout.split()]
    servers=[];ports={};requests=[]
    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version='HTTP/1.1'
        def log_message(self,*a):pass
        def do_GET(self):
            requests.append((self.server.label,self.path))
            if self.path=='/redirect-good':
                self.send_response(302);self.send_header('Location',f'https://localhost:{ports["trusted"]}/body');self.send_header('Content-Length','0');self.end_headers();return
            if self.path=='/redirect-bad':
                self.send_response(302);self.send_header('Location',f'https://localhost:{ports["untrusted"]}/body');self.send_header('Content-Length','0');self.end_headers();return
            if self.path=='/downgrade':
                self.send_response(302);self.send_header('Location',f'http://localhost:{ports["http"]}/body');self.send_header('Content-Length','0');self.end_headers();return
            body=b'fixture bytes' if self.path not in ['/large','/chunked'] else b'x'*4096
            self.send_response(404 if self.path=='/404' else 200)
            if self.path=='/chunked':self.send_header('Transfer-Encoding','chunked')
            else:self.send_header('Content-Length',str(len(body)+100 if self.path=='/truncated' else len(body)))
            self.end_headers()
            try:
                if self.path=='/chunked':self.wfile.write(hex(len(body))[2:].encode()+b'\r\n'+body+b'\r\n0\r\n\r\n')
                else:self.wfile.write(body)
                self.wfile.flush()
            except (BrokenPipeError,ConnectionResetError):pass
            if self.path=='/truncated':self.close_connection=True
    for label in ['http','trusted','untrusted','wronghost','expired']:
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Handler);server.daemon_threads=True;server.label=label
        if label!='http':
            ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(build/(label+'.pem'),build/(label+'.key'));server.socket=ctx.wrap_socket(server.socket,server_side=True)
        ports[label]=server.server_address[1];servers.append(server);threading.Thread(target=server.serve_forever,daemon=True).start()
    def download(label,path='/body',expected=True,limit=1024,fault=None):
        global checks
        url=('http' if label=='http' else 'https')+f'://localhost:{ports[label]}'+path
        output=build/'download';output.unlink(missing_ok=True)
        rc=run('download',url,output,limit,fault=fault)[0];checks+=1
        assert (rc==0)==expected,(label,path,rc)
        assert output.exists()==expected,(label,path,'partial file remains')
        if expected:assert output.read_bytes()==b'fixture bytes'
    if args.legacy:
        # Each comparison succeeds only if the old code violates the new policy.
        legacy=[]
        for label in ['untrusted','wronghost','expired']:
            try:download(label,expected=False)
            except AssertionError:legacy.append(label)
        assert len(legacy)==3,legacy
        print('Previous implementation reproduced insecure acceptance:',', '.join(legacy))
    else:
        for label in ['http','trusted']:download(label)
        for label in ['untrusted','wronghost','expired']:download(label,expected=False)
        download('trusted','/redirect-good');download('http','/redirect-good')
        for label,path in [('trusted','/redirect-bad'),('trusted','/downgrade'),('http','/404'),('http','/truncated'),('http','/large'),('http','/chunked')]:download(label,path,False)
        for fault in ['write','flush','close','sync']:download('trusted',expected=False,fault=fault)
        output=build/'unsupported'
        assert run('download','file:///etc/hosts',output,1024)[0]!=0 and not output.exists();checks+=1
    for server in servers:server.shutdown();server.server_close()
    entry={'name':'fixture','id':'fixture','filename':'safe.elf','url':'https://example.invalid/safe.elf'}
    for value,accepted in [(None,True),('',True),('a'*64,True),('A'*64,True),('a'*63,False),('a'*65,False),('z'*64,False),(' '*64,False),(42,False)]:
        document=dict(entry)
        if value is not None:document['checksum']=value
        rc,count=run('parse',json.dumps([document]));checks+=1
        if args.legacy and value in ['a'*63,'z'*64,' '*64]:assert count==1;continue
        assert rc==0 and count==(1 if accepted else 0),(value,rc,count)
    library=data/'payloads/local/safe';library.mkdir(parents=True)
    (library/'safe.elf').write_bytes(b'fixture');(library/'meta.json').write_text(json.dumps({'kind':'local','filename':'safe.elf'}))
    (library/'safe.bin').write_bytes(b'fixture')
    outside=build/'outside';outside.mkdir();(outside/'safe.elf').write_bytes(b'fixture')
    lookalike=Path(str(data)+'-other');lookalike.mkdir();(lookalike/'safe.elf').write_bytes(b'fixture')
    (library/'escape.elf').symlink_to(outside/'safe.elf');(library/'alias.elf').symlink_to(library/'safe.elf')
    (library/'fake.elf').symlink_to(library/'meta.json');(library/'pldmgr.elf').write_bytes(b'fixture');(library/'self.elf').symlink_to(library/'pldmgr.elf')
    (library/'directory.elf').mkdir();(library/'empty.elf').touch()
    usb=build/'usb0';(usb/'nayeplmtools').mkdir(parents=True);(usb/'safe.bin').write_bytes(b'fixture');(usb/'nayeplmtools/safe.elf').write_bytes(b'fixture')
    valid=['local/safe','safe.elf',library/'safe.elf',library/'safe.bin',library/'alias.elf',usb/'nayeplmtools/safe.elf']
    invalid=[library/'meta.json',outside/'safe.elf',lookalike/'safe.elf',library/'escape.elf',library/'fake.elf',library/'pldmgr.elf',library/'self.elf',library/'directory.elf',library/'empty.elf',str(library)+'/../safe/safe.elf','local/../safe','local/safe/meta.json',str(outside)+'/missing/safe.elf',usb/'safe.bin']
    if args.legacy:
        for path in [library/'meta.json',outside/'safe.elf',library/'escape.elf',library/'pldmgr.elf']:
            assert run('launch',path)[1]==1
        print('Previous implementation reproduced unchecked loader calls for metadata, outside paths, symlinks and self')
    else:
        for path in valid:assert run('launch',path)==[0,1,1],path;checks+=1
        for path in invalid:
            rc,sockets,history=run('launch',path);assert rc!=0 and sockets==history==0,path;checks+=1
        for path in invalid[:10]:
            rc,sockets,history=run('direct',path);assert rc!=0 and sockets==history==0,path;checks+=1
        saved=usb/'saved-library';(usb/'nayeplmtools').rename(saved);(usb/'nayeplmtools').symlink_to(outside,target_is_directory=True)
        for path in [usb/'nayeplmtools/safe.elf',outside/'safe.elf']:
            rc,sockets,history=run('direct',path);assert rc!=0 and sockets==history==0;checks+=1
        (usb/'nayeplmtools').unlink();saved.rename(usb/'nayeplmtools')
        (data/'nayeplmtools_config.txt').write_text('SCAN_USB_PAYLOADS=1\n')
        assert run('launch',usb/'safe.bin')==[0,1,1];checks+=1
        (build/'usb1').symlink_to(outside,target_is_directory=True)
        for path in [build/'usb1/safe.elf',outside/'safe.elf']:
            rc,sockets,history=run('direct',path);assert rc!=0 and sockets==history==0;checks+=1
        race=library/'race.elf';race.write_bytes(b'fixture')
        rc,sockets,history=run('direct',race,swap=race,target=outside/'safe.elf');assert rc!=0 and sockets==history==0;checks+=1
        race_dir=library/'race-dir';race_dir.mkdir();(race_dir/'safe.elf').write_bytes(b'fixture')
        rc,sockets,history=run('direct',race_dir/'safe.elf',swap=race_dir,target=outside);assert rc!=0 and sockets==history==0;checks+=1
        (library/'meta.json').write_text(json.dumps({'kind':'local','filename':'../../../../outside/safe.elf'}))
        assert run('launch','local/safe')[1:]==[0,0];checks+=1
    if not args.legacy:
        # Install with a real parser/cache/commit, recorded transfer bytes, and
        # a manifest refresh that changes the entry between attempts.
        repo_retry=build/'repo_retry.o';source_retry=build/'source_retry.o'
        subprocess.run(flags+['-Ddownload_to_file=retry_unused_download','-Ddownload_to_file_limited=retry_unused_download_limited','-c','src/repository.c','-o',str(repo_retry)],cwd=ROOT,check=True)
        subprocess.run(flags+['-c','src/sources.c','-o',str(source_retry)],cwd=ROOT,check=True)
        retry_binary=build/'retry_tests'
        retry_objects=[x for x in objects if Path(x).stem not in ['repository','ps5_launcher']]+[str(repo_retry),str(source_retry)]
        subprocess.run(flags+retry_objects+['tests/retry_driver.c','-Wl,--gc-sections','-o',str(retry_binary)],cwd=ROOT,check=True)
        url=re.search(r'#define REPOSITORY_SOURCE_URL\s*\\?\s*"([^"]+)"',(ROOT/'include/pldmgr.h').read_text())[1]
        cache=data/('repo_cache_'+hashlib.sha256(url.rstrip('/').encode()).hexdigest()[:8]+'.json')
        expected=hashlib.sha256(b'retry bytes').hexdigest()
        manifest=build/'refresh.json'
        initial=dict(entry,url='fixture://initial',checksum='a'*64)
        variants=[('valid',dict(entry,url='fixture://retry',checksum=expected),True,1),
                  ('uppercase',dict(entry,url='fixture://retry',checksum=expected.upper()),True,1),
                  ('missing',dict(entry,url='fixture://retry'),True,1),
                  ('empty',dict(entry,url='fixture://retry',checksum=''),True,1),
                  ('mismatch',dict(entry,url='fixture://retry',checksum='b'*64),False,1),
                  ('short',dict(entry,url='fixture://retry',checksum='a'*63),False,0),
                  ('long',dict(entry,url='fixture://retry',checksum='a'*65),False,0),
                  ('nonhex',dict(entry,url='fixture://retry',checksum='z'*64),False,0),
                  ('traversal',dict(entry,url='fixture://retry',filename='../outside.elf',checksum=expected),False,0),
                  ('extension',dict(entry,url='fixture://retry',filename='meta.json',checksum=expected),False,0),
                  ('self',dict(entry,url='fixture://retry',filename='pldmgr.elf',checksum=expected),False,0)]
        for fail_first in [0,1]:
            for label,fresh,success,retry_count in variants:
                cache.write_text(json.dumps([initial]));manifest.write_text(json.dumps([fresh]))
                result=subprocess.run([str(retry_binary),str(manifest),str(fail_first)],capture_output=True,text=True,check=True)
                actual=json.loads(result.stdout);checks+=1
                assert (actual['rc']==0)==success,(label,fail_first,actual)
                assert actual['first']==1 and actual['retry']==retry_count,(label,actual)
                assert actual['metadata_same']==(not success),(label,actual)
                assert actual['body_hash']==(expected if success else hashlib.sha256(b'old bytes').hexdigest()),(label,actual)
    print(f'Integrity tests: {checks} TLS/download/hash/path scenarios passed')
