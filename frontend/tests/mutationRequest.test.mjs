import test from 'node:test'
import assert from 'node:assert/strict'
import { apiFetch, mutationFetch, saveConfigOnExit } from '../src/utils/mutationRequest.js'
import { mutationAuthorized, sameOrigin } from '../mock-policy.js'

async function clients(navigatorValue, action) {
  const fetch = globalThis.fetch
  const nav = Object.getOwnPropertyDescriptor(globalThis, 'navigator')
  const calls = []
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: navigatorValue })
  globalThis.fetch = async (...args) => { calls.push(args); return { ok: true } }
  try { await action(calls) }
  finally {
    globalThis.fetch = fetch
    if (nav) Object.defineProperty(globalThis, 'navigator', nav)
    else delete globalThis.navigator
  }
}
test('mutations force POST, add marker, and preserve body/options without changing original headers', async () => {
  await clients(undefined, async calls => {
    const headers = new Headers({ 'Content-Type': 'application/json', 'X-PLDMGR-Request': 'old' })
    await mutationFetch('/set_config', { method: 'GET', headers, body: '{}', keepalive: true })
    const options = calls[0][1]
    assert.equal(options.method, 'POST')
    assert.equal(options.headers.get('X-PLDMGR-Request'), '1')
    assert.equal(options.headers.get('Content-Type'), 'application/json')
    assert.equal(options.body, '{}')
    assert.equal(options.keepalive, true)
    assert.equal(headers.get('X-PLDMGR-Request'), 'old')
    const blob = new Blob(['fixture'])
    await mutationFetch('/manage:upload', { body: blob })
    assert.equal(calls[1][1].body, blob)
    assert.equal(calls[1][1].headers.get('Content-Type'), null)
  })
})
test('successful beacon queues JSON once without duplicate fetch', async () => {
  let beacon
  await clients({ sendBeacon: (...args) => { beacon = args; return true } }, async calls => {
    assert.equal(saveConfigOnExit('{}'), true)
    assert.equal(beacon[0], '/set_config')
    assert.equal(beacon[1].type, 'application/json')
    assert.equal(await beacon[1].text(), '{}')
    assert.equal(calls.length, 0)
  })
})
for (const [name, nav] of [['missing', undefined], ['rejected', { sendBeacon: () => false }],
  ['throws', { sendBeacon: () => { throw new Error('queue') } }]]) {
  test(`beacon ${name} uses a marked keepalive fetch`, async () => {
    await clients(nav, async calls => {
      assert.equal(saveConfigOnExit('{}'), false)
      assert.equal(calls[0][1].method, 'POST')
      assert.equal(calls[0][1].headers.get('X-PLDMGR-Request'), '1')
      assert.equal(calls[0][1].keepalive, true)
      assert.equal(calls[0][1].body, '{}')
    })
  })
}
test('mock matches full origins and supports beacons/LAN clients without trusting foreign markers', () => {
  assert.equal(sameOrigin('http://localhost:8084', 'localhost:8084'), true)
  assert.equal(sameOrigin('http://[::1]:8084', '[::1]:8084'), true)
  assert.equal(sameOrigin('http://localhost/page', 'localhost', true), true)
  for (const origin of ['null', 'http://localhost.evil', 'http://localhost/path',
    'http://localhost http://evil.invalid', 'https://localhost', 'http://evil@localhost'])
    assert.equal(sameOrigin(origin, 'localhost'), false)
  assert.equal(mutationAuthorized({ host: 'localhost', origin: 'http://localhost' }), true)
  assert.equal(mutationAuthorized({ host: 'localhost', referer: 'http://localhost/page' }), true)
  assert.equal(mutationAuthorized({ 'x-pldmgr-request': '1' }), true)
  for (const headers of [{}, { host: 'localhost', origin: 'null', 'x-pldmgr-request': '1' },
    { host: 'localhost', origin: 'http://evil.invalid', 'x-pldmgr-request': '1' },
    { host: 'localhost', referer: 'http://evil.invalid', 'x-pldmgr-request': '1' },
    { 'x-pldmgr-request': '1', 'sec-fetch-site': 'cross-site' }])
    assert.equal(mutationAuthorized(headers), false)
})

test('read requests keep GET semantics while marking an actual frontend connection', async () => {
  await clients(undefined, async calls => {
    await apiFetch('/get_config')
    assert.equal(calls[0][1].method, undefined)
    assert.equal(calls[0][1].headers.get('X-PLDMGR-Request'), '1')
  })
})
