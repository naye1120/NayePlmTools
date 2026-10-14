import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
const frontend = fileURLToPath(new URL('../frontend/', import.meta.url))
const origin = 'http://127.0.0.1:8081'
const child = spawn(process.execPath, ['mock-server.js'], { cwd: frontend })
let output = ''
child.stdout.on('data', data => { output += data })
child.stderr.on('data', data => { output += data })
const wait = ms => new Promise(resolve => setTimeout(resolve, ms))
async function request(route, options = {}) {
  return fetch(origin + route, options)
}
try {
  for (let i = 0; i < 100 && !output.includes('Running at'); i++) {
    if (child.exitCode !== null) throw new Error(output)
    await wait(50)
  }
  assert.ok(output.includes('Running at'), output)
  const catalog = await (await request('/repository_payloads')).json()
  const entries = catalog.sources[0].payloads
  assert.ok(entries.some(entry => entry.icon_key))
  assert.ok(entries.every(entry => !Object.hasOwn(entry, 'icon')))
  const images = await (await request('/repository_icons?source=default')).json()
  assert.ok(entries.filter(entry => entry.icon_key).every(entry => images.icons[entry.icon_key]?.startsWith('data:image/')))
  const sources = await (await request('/sources_list')).json()
  for (const route of ['/repository_cache_clear', '/abort', '/autoload_clear', '/repository_refresh', '/repository_install?id=test',
    '/sources_add?url=http://evil.invalid', '/sources_remove?index=1', '/manage:delete?slot=local/test',
    '/loadpayload:local/test', '/process_kill?pid=123', '/usb_move_perform?path=test', '/shutdown',
    '/set_config', '/sources_set', '/manage:upload?filename=test.elf']) {
    assert.equal((await request(route)).status, 405, route)
    assert.equal((await request(route, { method: 'POST', headers: { Origin: 'http://evil.invalid', 'X-PLDMGR-Request': '1' } })).status, 403, route)
  }
  assert.deepEqual(await (await request('/sources_list')).json(), sources)
  assert.equal((await request('/set_config', { method: 'OPTIONS', headers: { Origin: 'http://evil.invalid', 'Access-Control-Request-Method': 'POST', 'Access-Control-Request-Headers': 'X-PLDMGR-Request' } })).status, 403)
  assert.equal((await request('/set_config', { method: 'POST', body: '{}', headers: { 'X-PLDMGR-Request': '1', 'Content-Type': 'application/json' } })).status, 200)
  assert.equal((await request('/set_config', { method: 'POST', body: '{}', headers: { Origin: origin, 'Content-Type': 'text/plain' } })).status, 200)
  const response = await request('/sources_list', { headers: { Origin: 'http://evil.invalid' } })
  assert.equal(response.headers.get('Access-Control-Allow-Origin'), null)
  const fixture = await request('/test_repo.json', { headers: { Origin: 'http://localhost:5173' } })
  assert.equal(fixture.headers.get('Access-Control-Allow-Origin'), '*')
  console.log('Mock server: GET mutations, hostile POST/preflight, marked clients, and beacon content types passed')
} finally {
  const exited = new Promise(resolve => child.once('exit', resolve))
  child.kill('SIGTERM')
  await Promise.race([exited, wait(2000)])
  if (child.exitCode === null) child.kill('SIGKILL')
}
