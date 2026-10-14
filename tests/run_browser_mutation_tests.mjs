// Actual Chrome requests against the native stub server and Vite's real proxy.
// Requires that test server published on loopback:8081; never target a console.
import assert from 'node:assert/strict'
import { createServer as httpServer } from 'node:http'
import { readFile, mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { createServer } from '../frontend/node_modules/vite/dist/node/index.js'

const backend = 'http://127.0.0.1:8081'
const chrome = process.env.CHROME_BIN || (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : '/usr/bin/google-chrome')
const html = await readFile(new URL('./browser_mutation_fixture.html', import.meta.url), 'utf8')
const state = async () => (await fetch(backend + '/__test_state')).json()
const foreign = httpServer((req, res) => { res.setHeader('Content-Type', 'text/html'); res.end(html) })
await new Promise(resolve => foreign.listen(0, '127.0.0.1', resolve))
const frontend = fileURLToPath(new URL('../frontend/', import.meta.url))
const vite = await createServer({ root: frontend, configFile: join(frontend, 'vite.config.js'),
  server: { host: '127.0.0.1', port: 0 }, plugins: [{ name: 'browser-policy-fixture', configureServer(server) {
    server.middlewares.use((req, res, next) => {
      if (!req.url.startsWith('/__test_browser')) return next()
      res.setHeader('Content-Type', 'text/html'); res.end(html)
    })
  }}] })
await vite.listen()
async function browser(url) {
  const profile = await mkdtemp(join(tmpdir(), 'pldmgr-policy-browser-'))
  let child, ws
  const wait = ms => new Promise(resolve => setTimeout(resolve, ms))
  try {
    child = spawn(chrome, ['--headless', ...(process.platform === 'linux' ? ['--no-sandbox'] : []), '--disable-gpu', '--no-first-run', '--no-default-browser-check',
      '--disable-background-networking', '--disable-component-update', '--disable-sync', '--disable-extensions',
      '--password-store=basic', '--user-data-dir=' + profile, '--remote-debugging-port=0', 'about:blank'])
    let launchError
    child.on('error', error => { launchError = error })
    child.stdout.resume(); child.stderr.resume()
    let portInfo
    for (let i = 0; i < 200; i++) {
      if (launchError) throw launchError
      try { portInfo = await readFile(join(profile, 'DevToolsActivePort'), 'utf8'); break } catch {}
      await wait(50)
    }
    assert.ok(portInfo, 'Chrome debugger did not start')
    const [port, path] = portInfo.trim().split('\n')
    ws = new WebSocket(`ws://127.0.0.1:${port}${path}`)
    await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
    let id = 0
    const pending = new Map()
    ws.onmessage = event => {
      const reply = JSON.parse(event.data)
      const entry = pending.get(reply.id)
      if (entry) { pending.delete(reply.id); reply.error ? entry.reject(reply.error) : entry.resolve(reply.result) }
    }
    function rpc(method, params = {}, sessionId) {
      return new Promise((resolve, reject) => {
        const call = ++id; pending.set(call, { resolve, reject })
        ws.send(JSON.stringify({ id: call, method, params, ...(sessionId ? { sessionId } : {}) }))
        setTimeout(() => { if (pending.delete(call)) reject(new Error('Debugger timeout: ' + method)) }, 5000).unref()
      })
    }
    const { targetId } = await rpc('Target.createTarget', { url })
    const { sessionId } = await rpc('Target.attachToTarget', { targetId, flatten: true })
    let text
    for (let i = 0; i < 200; i++) {
      const response = await rpc('Runtime.evaluate', { expression: 'document.getElementById("result")?.textContent', returnByValue: true }, sessionId)
      text = response.result?.value
      if (text && text !== 'RUNNING') break
      await wait(50)
    }
    assert.ok(text && text !== 'RUNNING', 'Browser fixture did not finish: ' + url)
    const result = JSON.parse(text)
    assert.equal(result.done, true, JSON.stringify(result))
    return result
  } finally {
    ws?.close()
    if (child) {
      const exited = new Promise(resolve => child.once('exit', resolve))
      child.kill('SIGTERM')
      await Promise.race([exited, wait(2000)])
      if (child.exitCode === null) child.kill('SIGKILL')
    }
    await rm(profile, { recursive: true, force: true })
  }
}

try {
  let before = await state()
  assert.ok(Number.isInteger(before.mutations) && Number.isInteger(before.active) && before.running === 1, 'Requires the native test-only server')
  await browser(backend + '/__test_browser')
  let after = await state()
  assert.equal(after.mutations, before.mutations + 4, 'native same-origin actions and both beacons')
  before = after
  const url = `http://127.0.0.1:${foreign.address().port}/?mode=foreign&target=${encodeURIComponent(backend)}`
  await browser(url); after = await state()
  assert.deepEqual(after, before, 'foreign form/image/fetch/beacon/preflight must not mutate or activate autoload')
  before = after
  await browser(`http://127.0.0.1:${vite.httpServer.address().port}/__test_browser?proxy=1`)
  after = await state()
  assert.equal(after.mutations, before.mutations + 4, 'Vite proxy preserves browser Host for actions and beacons')
  assert.equal(after.running, 1)
  console.log('Chrome: native same-origin actions/beacons, foreign requests with zero effects, and Vite proxy passed')
} finally { await vite.close(); await new Promise(resolve => foreign.close(resolve)) }
