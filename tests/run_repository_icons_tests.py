#!/usr/bin/env python3
"""Persistent icon caches and real catalog serialization with a recorded transport."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import time

parser = argparse.ArgumentParser()
parser.add_argument('--thread-sanitize', action='store_true')
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='pldmgr-icons-') as temporary:
    build = Path(temporary)
    storage = build / 'data'
    storage.mkdir()
    header = (ROOT / 'include/pldmgr.h').read_text().replace('/data/pldmgr', str(storage)).replace('/data/nayeplmtools', str(storage))
    source = 'https://mirror.invalid/catalog/payloads.json'
    import re
    header = re.sub(r'(#define REPOSITORY_SOURCE_URL\s*\\?\s*)"[^"]+"', lambda m: m[1] + json.dumps(source), header)
    (build / 'pldmgr.h').write_text(header)
    (build / 'assets_cacert_pem.h').write_text('static const unsigned char assets_cacert_pem[]={0};\nstatic const unsigned int assets_cacert_pem_len=0;\n')
    flags = shlex.split(os.environ.get('HOST_CC', 'clang')) + ['-std=c11', '-D_DEFAULT_SOURCE', '-g', '-O1', '-Wall', '-Wextra', '-pthread',
        '-ffunction-sections', '-fdata-sections', '-fsanitize=thread' if args.thread_sanitize else '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-I'+str(build), '-Iinclude']
    objects = []
    for name in ['sources', 'repository', 'slot', 'json_helpers', 'sha256', 'utils']:
        obj = build / (name + '.o')
        extra = ['-Ddownload_to_file=icons_unused_download', '-Ddownload_to_file_limited=icons_unused_download_limited'] if name == 'repository' else []
        subprocess.run(flags + extra + ['-c', 'src/'+name+'.c', '-o', str(obj)], cwd=ROOT, check=True)
        objects.append(str(obj))
    binary = build / 'icons-tests'
    subprocess.run(flags + objects + ['tests/repository_icons_driver.c', '-lcurl', '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections', '-o', str(binary)], cwd=ROOT, check=True)
    catalog, images, record = [build / name for name in ('catalog.json', 'icons.json', 'requests.txt')]
    env = os.environ.copy()
    env.update(ICON_TEST_CATALOG=str(catalog), ICON_TEST_ICONS=str(images), ICON_TEST_RECORD=str(record), ASAN_OPTIONS='detect_leaks=0:halt_on_error=1' if sys.platform == 'darwin' else 'detect_leaks=1:halt_on_error=1')
    image = 'data:image/png;base64,aGVsbG8='
    image2 = 'data:image/png;base64,d29ybGQ='
    items = [{'id':'one','name':'One','filename':'one.elf','url':'https://mirror.invalid/one.elf','icon_key':'shared'}]
    def manifest(url='icons.json'):
        catalog.write_text(json.dumps({'name':'Fixture','icons_url':url,'payloads':items}))
    def run(*args, fail=False):
        current = dict(env)
        if fail: current['ICON_TEST_FAIL']='1'
        result = subprocess.run([str(binary),*args],cwd=ROOT,env=current,check=True,capture_output=True,text=True)
        return json.loads(result.stdout) if result.stdout.strip() else None
    def requests(): return record.read_text().splitlines() if record.exists() else []
    def icon_requests(): return [url for url in requests() if url != source]
    def revision(force=False,fail=False):
        listing=run('list', *(['force'] if force else []),fail=fail)
        assert 'icon' not in listing['sources'][0]['payloads'][0]
        return listing['sources'][0]['icons_revision']
    meta = storage / ('repo_icons_'+hashlib.sha256(source.encode()).hexdigest()+'.meta')
    def expire():
        state=json.loads(meta.read_text());state.update(updated=str(int(time.time())-8*86400),attempted='0');meta.write_text(json.dumps(state))
    manifest();images.write_text(json.dumps({'icons':{'shared':image}}))
    first=revision();assert len(first)==64 and icon_requests()==['https://mirror.invalid/catalog/icons.json']
    assert run('icons')['icons']=={'shared':image}
    assert revision()==first and len(icon_requests())==1  # separate processes prove disk persistence
    assert revision(force=True)==first and len(icon_requests())==1  # catalog refresh does not force image fetch
    items.append({'id':'two','name':'Two','filename':'two.elf','url':'https://mirror.invalid/two.elf','icon_key':'new'})
    manifest();images.write_text(json.dumps({'icons':{'shared':image,'new':image2}}))
    second=revision(force=True);assert second!=first and len(icon_requests())==2
    assert run('icons')['icons']['new']==image2
    assert revision()==second and len(icon_requests())==2
    images.write_text(json.dumps({'icons':{'shared':image2,'new':image2}}));expire()
    third=revision();assert third!=second and len(icon_requests())==3
    expire();assert revision(fail=True)==third and len(icon_requests())==4
    assert revision(fail=True)==third and len(icon_requests())==4  # failure backoff
    items[1]['icon_key']='absent';manifest();assert revision(force=True)==third and len(icon_requests())==5
    assert revision()==third and len(icon_requests())==5  # permanently absent keys do not refetch on every list
    expire();images.write_text('{"icons":{"shared":"https://bad.invalid/image.png"}}')
    assert revision()==third and run('icons')['icons']['shared']==image2
    expire();images.write_text('{"icons":{"shared":"data:image/png;base64,aGVsbG8=","shared":"data:image/png;base64,aGVsbG8="}}')
    assert revision()==third  # duplicate keys cannot replace the good cache
    images.write_text(json.dumps({'icons':{'shared':image,'absent':image}}))
    for relative, expected in [('nested/../icons.json','https://mirror.invalid/catalog/icons.json'),('/assets/icons.json','https://mirror.invalid/assets/icons.json'),('https://cdn.invalid/icons.json','https://cdn.invalid/icons.json')]:
        manifest(relative);revision(force=True);assert icon_requests()[-1]==expected
    before=len(icon_requests());manifest('https://new.invalid/icons.json')
    kept=revision(force=True,fail=True);assert kept and len(icon_requests())==before+1
    assert revision(fail=True)==kept and len(icon_requests())==before+1
    state=json.loads(meta.read_text());state['attempted']='0';meta.write_text(json.dumps(state))
    assert revision() and len(icon_requests())==before+2  # failed URL changes are retried after backoff
    for bad in ['file:///tmp/icons.json','https://user:password@cdn.invalid/icons.json','http://cdn.invalid/icons.json']:
        before=len(icon_requests());manifest(bad);assert revision(force=True)=='' and len(icon_requests())==before
    catalog.write_text(json.dumps(items));assert revision(force=True)==''  # bare catalogs without icons supported
    catalog_cache=storage/('repo_cache_'+hashlib.sha256(source.encode()).hexdigest()[:8]+'.json')
    manifest();images.write_text(json.dumps({'icons':{'shared':image,'absent':image}}));revision(force=True)
    catalog_timestamp = Path(str(catalog_cache)+'.ts')
    before = requests().count(source)
    icons_before = len(icon_requests())
    catalog_timestamp.write_text(str(int(time.time())-2*3600))
    revision();assert requests().count(source)==before  # still fresh before three hours
    catalog_timestamp.write_text(str(int(time.time())-3*3600-5))
    revision();assert requests().count(source)==before+1  # expired catalogs refresh on access
    assert len(icon_requests())==icons_before  # catalog expiry does not expire the weekly icon cache
    Path(str(catalog_cache)+'.ts').write_text('0')
    outage=dict(env,ICON_TEST_FAIL_CATALOG='1',ICON_TEST_FAIL='1')
    cached_listing=json.loads(subprocess.check_output([str(binary),'list'],cwd=ROOT,env=outage,text=True))
    assert cached_listing['sources'][0]['error'] is True
    assert len(cached_listing['sources'][0]['payloads'])==len(items)
    assert cached_listing['sources'][0]['icons_revision']
    Path(str(catalog_cache)+'.ts').write_text(str(int(time.time())))
    custom='https://another.invalid/catalog/payloads.json'
    run('configure', json.dumps({'sources':[{'id':'other','name':'Other','url':custom}]}))
    other_cache=storage/('repo_cache_'+hashlib.sha256(custom.encode()).hexdigest()[:8]+'.json')
    other_cache.write_text(json.dumps({'icons_url':'icons.json','payloads':items}))
    Path(str(other_cache)+'.ts').write_text(str(int(time.time())))
    images.write_text(json.dumps({'icons':{'shared':image2,'absent':image2}}))
    listing=run('list');assert listing['sources'][1]['icons_revision']
    assert run('icons','other')['icons']['shared']==image2
    assert run('icons','default')['icons']['shared']==image
    assert icon_requests()[-1]=='https://another.invalid/catalog/icons.json'
    state=json.loads(meta.read_text());state['attempted']='0';meta.write_text(json.dumps(state))
    manifest();images.write_text('x'*(4*1024*1024));retained=run('icons')['icons']
    revision(force=True);assert run('icons')['icons']==retained
    assert subprocess.run([str(binary),'icons','https://arbitrary.invalid/icons.json'],cwd=ROOT,env=env,capture_output=True).returncode==1
    (storage/('repo_icons_'+hashlib.sha256(source.encode()).hexdigest()+'.json.tmp-ABC123')).write_text('partial')
    # Only exact cache names are deleted; settings, payloads and unrelated names survive.
    preserved = ['favorites.txt','autoload.txt','sources.json','repo_cache_not-a-hash.json']
    for name in preserved: (storage/name).write_text('{}' if name=='sources.json' else 'preserve')
    (storage/'payloads').mkdir();(storage/'payloads'/'one.elf').write_bytes(b'preserve')
    run('clear')
    assert not list(storage.glob('repo_icons_*')) and not list(storage.glob('repo_cache_????????.json*'))
    assert all((storage/name).exists() for name in preserved) and (storage/'payloads'/'one.elf').read_bytes()==b'preserve'
    (storage/'sources.json').unlink()
    manifest();images.write_text(json.dumps({'icons':{'shared':image,'absent':image}}));assert revision()
    run('concurrent')
    print('Icon cache: persistence, weekly refresh, unknown keys, retry backoff, failed/malformed refresh retention, URL resolution, scoped clearing and concurrent list/read/clear passed.')
