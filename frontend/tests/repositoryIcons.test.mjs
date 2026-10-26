import test from 'node:test'
import assert from 'node:assert/strict'
import { createRepositoryIconCache } from '../src/utils/repositoryIcons.js'
const icon = 'data:image/png;base64,aGVsbG8='
const catalog = (revision = 'r1', id = 'default', url = 'https://mirror.invalid/catalog.json') => ({ sources: [{ id, url, icons_revision: revision, payloads: [{ icon_key: 'shared', icon: 'data:image/png;base64,d29ybGQ=' }] }] })

test('icons load separately, shared keys reuse data and unchanged revisions avoid requests', async () => {
  let requests = 0
  const cache = createRepositoryIconCache(async url => {
    requests++; assert.match(url, /^\/repository_icons\?source=default&revision=r1$/)
    return { ok: true, json: async () => ({ icons: { shared: icon } }) }
  })
  assert.equal((await cache.hydrate(catalog())).sources[0].payloads[0].icon, icon)
  assert.equal((await cache.hydrate(catalog())).sources[0].payloads[0].icon, icon)
  assert.equal(requests, 1)
})
test('failed updates retain previous images and can retry; explicit clearing removes cached images', async () => {
  let fail = false, requests = 0
  const cache = createRepositoryIconCache(async () => { requests++; return { ok: !fail, json: async () => ({ icons: { shared: icon } }) } })
  await cache.hydrate(catalog()); fail = true
  assert.equal((await cache.hydrate(catalog('r2'))).sources[0].payloads[0].icon, icon)
  fail = false; await cache.hydrate(catalog('r2')); assert.equal(requests, 3)
  cache.clear(); fail = true
  assert.equal((await cache.hydrate(catalog('r2'))).sources[0].payloads[0].icon, null)
})
test('source namespaces are independent, absent descriptors use fallback and remote image URLs are ignored', async () => {
  let calls = 0
  const cache = createRepositoryIconCache(async () => ({ ok: true, json: async () => ({ icons: { shared: ++calls === 1 ? icon : 'https://bad.invalid/image.png' } }) }))
  assert.equal((await cache.hydrate(catalog())).sources[0].payloads[0].icon, icon)
  assert.equal((await cache.hydrate(catalog('r1', 'other'))).sources[0].payloads[0].icon, null)
  assert.equal((await cache.hydrate(catalog(''))).sources[0].payloads[0].icon, null)
})
test('clearing during a pending fetch prevents that result from repopulating the cache', async () => {
  let resolve, calls = 0
  const cache = createRepositoryIconCache(() => { calls++; return new Promise(done => { resolve = done }) })
  const first = cache.hydrate(catalog()); cache.clear()
  resolve({ ok: true, json: async () => ({ icons: { shared: icon } }) }); await first
  const next = cache.hydrate(catalog()); assert.equal(calls, 2)
  resolve({ ok: true, json: async () => ({ icons: { shared: icon } }) }); await next
})

test('out-of-order icon responses cannot replace the latest revision cache', async () => {
  const pending = new Map(); let calls = 0
  const cache = createRepositoryIconCache(url => { calls++; return new Promise(resolve => pending.set(url.includes('revision=r2') ? 'r2' : 'r1', resolve)) })
  const old = cache.hydrate(catalog('r1')), newer = cache.hydrate(catalog('r2'))
  const fresh = 'data:image/png;base64,d29ybGQ='
  pending.get('r2')({ok:true,json:async()=>({icons:{shared:fresh}})}); await newer
  pending.get('r1')({ok:true,json:async()=>({icons:{shared:icon}})}); await old
  assert.equal((await cache.hydrate(catalog('r2'))).sources[0].payloads[0].icon, fresh)
  assert.equal(calls, 2)
})

test('a cached latest revision also prevents an older pending fetch from replacing it', async () => {
  let oldResolve, calls = 0
  const cache = createRepositoryIconCache(url => {
    calls++
    if (url.includes('revision=r1')) return new Promise(resolve => { oldResolve = resolve })
    return Promise.resolve({ok:true,json:async()=>({icons:{shared:icon}})})
  })
  await cache.hydrate(catalog('r2'))
  const old = cache.hydrate(catalog('r1'))
  await cache.hydrate(catalog('r2'))
  oldResolve({ok:true,json:async()=>({icons:{shared:icon}})}); await old
  await cache.hydrate(catalog('r2')); assert.equal(calls, 2)
})

const memoryStorage = () => {
  const values = new Map()
  return {
    getItem: key => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, value),
    removeItem: key => values.delete(key),
  }
}

test('reopening reuses persistent icons; revision changes and clearing fetch fresh icons', async () => {
  const storage = memoryStorage()
  let calls = 0
  const fetchIcons = async () => {
    calls++
    return { ok: true, json: async () => ({ icons: { shared: icon } }) }
  }
  await createRepositoryIconCache(fetchIcons, storage).hydrate(catalog())
  const reopened = createRepositoryIconCache(fetchIcons, storage)
  assert.equal((await reopened.hydrate(catalog())).sources[0].payloads[0].icon, icon)
  assert.equal(calls, 1)
  await reopened.hydrate(catalog('r2'))
  assert.equal(calls, 2)
  reopened.clear()
  await createRepositoryIconCache(fetchIcons, storage).hydrate(catalog('r2'))
  assert.equal(calls, 3)
})

test('unavailable or corrupt storage does not prevent icons from loading', async () => {
  const fetchIcons = async () => ({ ok: true, json: async () => ({ icons: { shared: icon } }) })
  for (const storage of [
    { getItem() { throw Error('denied') }, setItem() { throw Error('quota') }, removeItem() { throw Error('denied') } },
    { getItem: () => 'invalid json', setItem() {}, removeItem() {} },
  ]) {
    const cache = createRepositoryIconCache(fetchIcons, storage)
    assert.equal((await cache.hydrate(catalog())).sources[0].payloads[0].icon, icon)
    cache.clear()
  }
})

test('clearing during download prevents persistence after reopening', async () => {
  const storage = memoryStorage()
  let resolve
  const cache = createRepositoryIconCache(() => new Promise(done => { resolve = done }), storage)
  const pending = cache.hydrate(catalog())
  cache.clear()
  resolve({ ok: true, json: async () => ({ icons: { shared: icon } }) })
  await pending
  const reopened = createRepositoryIconCache(async () => ({ ok: false }), storage)
  assert.equal((await reopened.hydrate(catalog())).sources[0].payloads[0].icon, null)
})
