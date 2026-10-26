import assert from 'node:assert/strict'
import { readFile, mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { spawn, execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { createServer } from '../frontend/node_modules/vite/dist/node/index.js'

const frontend = fileURLToPath(new URL('../frontend/', import.meta.url))
const baseline = process.argv.includes('--baseline')
const lifecycleBaseline = process.argv.includes('--lifecycle-baseline')
const lifecycle = lifecycleBaseline || process.argv.includes('--lifecycle')
if (lifecycle) process.chdir(frontend) // Tailwind resolves its config from cwd.
const previousEditor = baseline || lifecycleBaseline ? execFileSync('git', ['show', `${baseline ? 'bfbe474' : '4fe3588'}:frontend/src/components/views/AutoloadView.jsx`], { encoding: 'utf8' }) : null
const chrome = process.env.CHROME_BIN || (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : '/usr/bin/google-chrome')
const keptList = 'local/kept-a,!1000,local/kept-b'
let stored = { AUTOLOAD_ENABLED: true, AUTOLOAD_LIST: keptList }
let pending = [], writes = []
const pendingSaves = [], pendingProcesses = [], pendingStatuses = [], pendingAppConfigs = [], requests = []
const pendingIcons = []
let holdIcons = false
const iconCatalog = { icons: { demo: 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jrlsAAAAASUVORK5CYII=' } }
let holdAppConfig = false, holdStatus = false
const fixtureItems = [
  { slot: 'local/kept-a', filename: 'a.elf', name: 'A', kind: 'local' },
  { slot: 'local/kept-b', filename: 'b.elf', name: 'B', kind: 'local' },
  { slot: 'local/next', filename: 'next.elf', name: 'Next', kind: 'local' },
  { slot: 'repo/demo@bbabc123', id: 'demo', filename: 'demo.elf', kind: 'repo', version: '1.2', source_url: 'https://mirror.invalid/payloads.json' },
]
const pendingReadmes = [], deletedSlots = []
let readmeText = null
const html = `<!doctype html><html><body><div id="root"></div><script type="module" src="/tests/${lifecycle ? 'frontendLifecycle' : 'autoloadEditor'}.browser.jsx"></script></body></html>`
const vite = await createServer({ root: frontend, configFile: join(frontend, 'vite.config.js'),
  server: { host: '127.0.0.1', port: 0 }, plugins: [{ name: 'autoload-editor-fixture', enforce: 'pre',
    transform(source, id) {
      if (previousEditor && id.split('?')[0] === join(frontend, 'src/components/views/AutoloadView.jsx')) return previousEditor
    }, configureServer(server) {
    server.middlewares.use(async (req, res, next) => {
      requests.push(req.url)
      if (req.url === '/__editor_page') {
        res.setHeader('Content-Type', 'text/html'); res.end(await server.transformIndexHtml(req.url, html)); return
      }
      if (req.url === '/__editor_config') { pending.push(res); return }
      if (req.url === '/__editor_save' || req.url === '/set_config') {
        const chunks = []; for await (const chunk of req) chunks.push(chunk)
        const update = JSON.parse(Buffer.concat(chunks).toString())
        writes.push(update)
        if (lifecycle && (req.url === '/__editor_save' || (update.FAVORITES !== undefined && req.headers['x-pldmgr-request']))) {
          pendingSaves.push({ update, res }); return
        }
        stored = { ...stored, ...update }; res.end('OK'); return
      }
      if (lifecycle) {
        const json = value => { res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(value)) }
        if (req.url === '/__payload_readme') {
          if (readmeText === null) pendingReadmes.push(res); else res.end(readmeText)
          return
        }
        if (req.url.startsWith('/manage:delete?')) {
          assert.equal(req.method, 'POST'); assert.equal(req.headers['x-pldmgr-request'], '1')
          const slot = new URL(req.url, 'http://fixture').searchParams.get('slot')
          deletedSlots.push(slot)
          const index = fixtureItems.findIndex(item => item.slot === slot)
          assert.ok(index >= 0); fixtureItems.splice(index, 1); json({ ok: true }); return
        }
        if (req.url === '/processes_list') { pendingProcesses.push(res); return }
        if (req.url === '/autoload_status') {
          if (holdStatus) pendingStatuses.push(res); else json({ remaining: -1, state: 'disabled' }); return
        }
        if (req.url === '/get_config') {
          if (holdAppConfig) pendingAppConfigs.push(res)
          else json({ ...stored, FAVORITES: stored.FAVORITES || '', FAVORITES_INITIALIZED: true })
          return
        }
        if (req.url === '/getip' || req.url === '/version') { res.end(req.url === '/getip' ? '127.0.0.1' : '0.5.2'); return }
        if (req.url === '/list_payloads') { json({ items: fixtureItems, payloads: fixtureItems.map(item => item.slot) }); return }
        if (req.url === '/history_list') { json({ history: [] }); return }
        if (req.url === '/repository_payloads') {
          json({ sources: [{ id: 'demo', name: 'Demo Repository', url: 'https://mirror.invalid/payloads.json', icons_revision: 'demo-r1',
            payloads: [{ id: 'demo', name: 'Example Payload', filename: 'demo.elf', version: '1.2', author: 'Example Author', icon_key: 'demo', details_url: `http://${req.headers.host}/__payload_readme` }] }] }); return
        }
        if (req.url.startsWith('/repository_icons?')) {
          if (holdIcons) pendingIcons.push(res); else json(iconCatalog)
          return
        }
        if (req.url.startsWith('/process_kill')) { json({ ok: true }); return }
      }
      next()
    })
  }}] })
await vite.listen()
const wait = ms => new Promise(resolve => setTimeout(resolve, ms))
async function eventually(condition, label) {
  for (let i = 0; i < 200; i++) { if (await condition()) return; await wait(25) }
  throw new Error(label)
}
function releaseConfig(data = stored, status = 200) {
  pending = pending.filter(res => !res.destroyed)
  assert.equal(pending.length, 1)
  const res = pending.shift(); res.statusCode = status; res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(data))
}
const profile = await mkdtemp(join(tmpdir(), 'pldmgr-editor-browser-'))
let child, ws
try {
  child = spawn(chrome, ['--headless', ...(process.platform === 'linux' ? ['--no-sandbox'] : []), '--disable-gpu', '--no-first-run', '--no-default-browser-check',
    '--disable-background-networking', '--disable-component-update', '--disable-sync', '--disable-extensions', '--password-store=basic',
    '--user-data-dir=' + profile, '--remote-debugging-port=0', 'about:blank'])
  let launchError; child.on('error', error => { launchError = error }); child.stdout.resume(); child.stderr.resume()
  let portInfo
  await eventually(async () => { if (launchError) throw launchError; try { portInfo = await readFile(join(profile, 'DevToolsActivePort'), 'utf8'); return true } catch { return false } }, 'Chrome did not start')
  const [port, path] = portInfo.trim().split('\n')
  ws = new WebSocket(`ws://127.0.0.1:${port}${path}`)
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
  let id = 0; const calls = new Map()
  ws.onmessage = event => { const reply = JSON.parse(event.data); const call = calls.get(reply.id); if (call) { calls.delete(reply.id); reply.error ? call.reject(reply.error) : call.resolve(reply.result) } }
  function rpc(method, params = {}, sessionId) {
    return new Promise((resolve, reject) => {
      const call = ++id; calls.set(call, { resolve, reject }); ws.send(JSON.stringify({ id: call, method, params, ...(sessionId ? { sessionId } : {}) }))
      setTimeout(() => { if (calls.delete(call)) reject(new Error('Debugger timeout: ' + method)) }, 5000).unref()
    })
  }
  const url = `http://127.0.0.1:${vite.httpServer.address().port}/__editor_page`
  const { targetId } = await rpc('Target.createTarget', { url })
  const { sessionId } = await rpc('Target.attachToTarget', { targetId, flatten: true })
  if (lifecycle) await rpc('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false }, sessionId)
  async function evaluate(expression) {
    const response = await rpc('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }, sessionId)
    assert.equal(response.exceptionDetails, undefined, JSON.stringify(response.exceptionDetails)); return response.result?.value
  }
  const hasButton = text => evaluate(`Array.from(document.querySelectorAll('button')).some(b => b.textContent.trim() === ${JSON.stringify(text)})`)
  const loading = () => evaluate(`document.querySelector('[role="status"]')?.textContent.includes('Loading autoload settings')`)
  async function reload() {
    await rpc('Page.reload', {}, sessionId); await eventually(() => pending.length === 1, 'config request did not start')
  }
  await eventually(() => pending.filter(res => !res.destroyed).length === 1, 'initial config request did not start')
  if (lifecycle) {
    await wait(100) // Let StrictMode's aborted probe request leave the fixture.
    releaseConfig()
    try { await eventually(() => hasButton('Disable Autoload'), 'StrictMode editor failed to initialize') }
    catch (error) { throw new Error(error.message + ': ' + JSON.stringify(await evaluate(`({text:document.body.textContent,errors:window.__unhandledErrors})`))) }
    assert.equal(writes.length, 0, 'StrictMode initial cleanup saved defaults')
    const click = (title, index = 0) => evaluate(`Array.from(document.querySelectorAll('button[title=${JSON.stringify(title)}]')).filter(b => b.offsetParent !== null)[${index}].click()`)
    const clickText = text => evaluate(`Array.from(document.querySelectorAll('button')).find(b => b.offsetParent !== null && b.textContent.trim() === ${JSON.stringify(text)}).click()`)
    const releaseSave = (status = 200) => {
      assert.equal(pendingSaves.length, 1, 'only one normal save may be in flight')
      const { update, res } = pendingSaves.shift()
      if (status === 200) stored = { ...stored, ...update }
      res.statusCode = status; res.end(status === 200 ? 'OK' : 'failed')
      return update
    }
    if (lifecycleBaseline) {
      await evaluate('window.__rejectSave = true')
      await click('Move Down')
      await eventually(() => evaluate('window.__unhandledErrors.length > 0'), 'old rejected save did not reproduce unhandled rejection')
      assert.equal(await hasButton('Retry'), false)
      console.log('Baseline 4fe3588: actual editor save rejection escapes its handler and has no retry control')
    } else {
      // Sequence rows must leave navigation to their actual controls, skipping disabled moves.
      await evaluate(`Array.from(document.querySelectorAll('button')).find(b => b.offsetParent !== null && b.textContent.trim() === 'Disable Autoload').focus()`)
      for (const title of ['Move Down', 'Remove', 'Move Up']) {
        await rpc('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 }, sessionId)
        await rpc('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Tab', code: 'Tab', windowsVirtualKeyCode: 9 }, sessionId)
        assert.equal(await evaluate(`document.activeElement.tagName`), 'BUTTON')
        assert.equal(await evaluate(`document.activeElement.title`), title, 'card intercepted control focus')
        assert.equal(await evaluate(`(() => {const button=document.activeElement, r=button.getBoundingClientRect();return document.elementFromPoint(r.x+r.width/2,r.y+r.height/2)?.closest('button')===button})()`), true, 'control was covered by another element')
      }
      // Same-turn handlers must compose against the current draft, not a render snapshot.
      await evaluate(`const buttons = title => Array.from(document.querySelectorAll('button')).filter(b => b.title === title && b.offsetParent !== null);
        buttons('Move Down')[0].click(); buttons('Move Down')[0].click(); buttons('Remove')[2].click();`)
      await eventually(() => pendingSaves.length === 1, 'reorder did not save')
      assert.equal(pendingSaves[0].update.AUTOLOAD_LIST, '!1000,local/kept-a,local/kept-b')
      releaseSave(); await eventually(() => pendingSaves.length === 1, 'queued remove did not drain')
      assert.equal(pendingSaves[0].update.AUTOLOAD_LIST, 'local/kept-a,!1000')
      releaseSave(500); await eventually(() => hasButton('Retry'), 'failed save lacked Retry')
      await clickText('Retry'); await eventually(() => pendingSaves.length === 1, 'retry did not save')
      releaseSave(); await eventually(async () => !(await hasButton('Retry')), 'retry error did not clear')

      // Add through the actual available-payload and delay-modal handlers.
      await evaluate(`Array.from(document.querySelectorAll('button')).find(b => b.offsetParent !== null && b.textContent.includes('Next')).click()`)
      await eventually(() => pendingSaves.length === 1, 'add payload did not save')
      assert.equal(pendingSaves[0].update.AUTOLOAD_LIST, 'local/kept-a,!1000,local/next'); releaseSave()
      await eventually(() => pendingSaves.length === 0, 'add save not released'); await wait(50)
      await clickText('Add Delay'); await clickText('3s')
      await eventually(() => pendingSaves.length === 1, 'delay did not save')
      assert.equal(pendingSaves[0].update.AUTOLOAD_LIST, 'local/kept-a,!1000,local/next,!3000'); releaseSave(); await wait(50)
      await evaluate('window.__rejectSave = true'); await click('Move Down')
      await eventually(() => hasButton('Retry'), 'thrown save did not become retryable')
      assert.equal(await evaluate('window.__unhandledErrors.length'), 0)
      await evaluate('window.__rejectSave = false'); await clickText('Retry')
      await eventually(() => pendingSaves.length === 1, 'throw retry did not save'); releaseSave(); await wait(50)

      // A pending earlier save cannot prevent a latest-state pagehide/unmount flush.
      await click('Remove', 3); await eventually(() => pendingSaves.length === 1, 'exit first save absent')
      await click('Remove', 2)
      const beforeExit = writes.length
      await evaluate(`window.dispatchEvent(new Event('pagehide')); window.dispatchEvent(new Event('beforeunload'))`)
      await eventually(() => writes.length === beforeExit + 1, 'exit flush missing/duplicated')
      assert.equal(writes.at(-1).AUTOLOAD_LIST, '!1000,local/kept-a')
      await evaluate('window.editor.mount(false)'); await wait(50)
      assert.equal(writes.length, beforeExit + 1, 'unmount duplicated exit flush')
      releaseSave(); await eventually(() => pendingSaves.length === 1, 'unmounted queued edit did not drain')
      assert.equal(pendingSaves[0].update.AUTOLOAD_LIST, '!1000,local/kept-a'); releaseSave(); await wait(50)
      assert.equal(stored.AUTOLOAD_LIST, '!1000,local/kept-a')
      await evaluate(`window.editor.config(${JSON.stringify(stored)}); window.editor.mount(true)`)
      await eventually(() => hasButton('Disable Autoload'), 'remount lost editor')

      // Countdown phase transitions, zero delay and malformed progress must render finite SVG values.
      holdIcons = true
      await evaluate(`window.editor.itemsLoading(true); window.editor.show('overlay'); window.editor.status({state:'waiting',remaining:5,delay:5,list:'a,b',total:2,done:0})`)
      await eventually(() => pendingIcons.length > 0, 'overlay icon fetch did not start')
      await evaluate(`window.editor.status({state:'countdown',remaining:5,delay:5,list:'repo/demo@bbabc123,local/next',total:2,done:0})`)
      await wait(50)
      assert.equal(await evaluate(`document.querySelectorAll('[data-icon-pending]').length`), 2, 'pending metadata flashed fallback icons')
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="0"]').textContent.includes('demo')`), true)
      assert.equal(await evaluate(`document.body.textContent.includes('@bbabc123')`), false, 'raw repository slot hash flashed before metadata arrived')
      await evaluate(`window.editor.itemsLoading(false)`)
      assert.equal(await evaluate(`!!document.querySelector('[data-payload-index="0"] .lucide-package')`), false)
      assert.equal(await hasButton('Abort Autoload'), true, 'metadata fetch hid the abort control')
      assert.equal(await evaluate(`document.querySelector('.text-8xl')?.textContent`), '5')
      holdIcons = false
      for (const response of pendingIcons.splice(0)) { response.setHeader('Content-Type', 'application/json'); response.end(JSON.stringify(iconCatalog)) }
      await eventually(() => evaluate(`document.querySelector('[data-payload-index="0"] img')?.naturalWidth > 0`), 'resolved metadata did not replace the placeholder')
      assert.equal(await evaluate(`document.querySelectorAll('[data-icon-pending]').length`), 0)
      assert.equal(await evaluate(`!!document.querySelector('[data-payload-index="1"] .lucide-package')`), true, 'payload without an icon lost its fallback')
      await evaluate(`window.editor.status({state:'countdown',remaining:0,delay:0,list:'a,b',total:2,done:0})`)
      await wait(50)
      const finiteSVG = () => evaluate(`Array.from(document.querySelectorAll('circle[stroke-dashoffset]')).every(c => Number.isFinite(Number(c.getAttribute('stroke-dashoffset'))) && Number(c.getAttribute('stroke-dashoffset')) >= 0 && Number(c.getAttribute('stroke-dashoffset')) <= 628)`)
      assert.equal(await finiteSVG(), true)
      await evaluate(`window.editor.status({state:'countdown',remaining:NaN,remaining_ms:Infinity,delay:NaN,total:NaN,done:Infinity,list:'a,b'})`)
      await wait(50); assert.equal(await finiteSVG(), true)
      await evaluate(`window.editor.status({state:'waiting',remaining:0,delay:5,total:2,done:0,list:'a,b'})`); await wait(30)
      await evaluate(`window.editor.status({state:'countdown',remaining:5,delay:5,total:2,done:0,list:'a,b'})`)
      await eventually(() => evaluate(`document.querySelector('.text-8xl')?.textContent === '5'`), 'waiting-to-countdown did not reset')
      await evaluate(`window.editor.status({state:'executing',remaining:0,delay:0,total:2,done:99,list:'a,b'})`)
      await wait(50); assert.equal(await finiteSVG(), true)
      assert.equal(await evaluate(`document.querySelector('.text-6xl')?.textContent`), '100%')

      // Delay rows track their sequence index, animate between status polls and reset for consecutive delays.
      await evaluate(`window.editor.status({state:'executing',list:'local/kept-a,!3000,!1500,local/next',total:2,done:1,outcomes:'sp',current:'DELAY',entry_index:1,entry_duration_ms:3000,entry_remaining_ms:2500})`)
      await wait(50)
      const fill = index => evaluate(`parseFloat(document.querySelector('[data-delay-index="${index}"] [data-delay-fill]').style.width)`)
      const initialFill = await fill(1)
      await wait(300)
      assert.ok(await fill(1) > initialFill + 5, 'delay fill did not animate between polls')
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="3"]').dataset.active`), 'false')
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="0"]').textContent.includes('Success')`), false)
      assert.equal(await evaluate(`!!document.querySelector('[data-payload-index="0"] .lucide-circle-check')`), true)
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="3"]').textContent.includes('Success')`), false)
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="0"]').textContent.includes('Other Source')`), false)
      await evaluate(`window.editor.status({state:'executing',list:'local/kept-a,!3000,!1500,local/next',total:2,done:1,outcomes:'sp',current:'DELAY',entry_index:2,entry_duration_ms:1500,entry_remaining_ms:1500})`)
      await wait(50)
      assert.equal(await fill(1), 100)
      assert.ok(await fill(2) < 20, 'consecutive delay reused the previous timer')
      // Auto-return is off unless explicitly enabled, even for successful completion.
      const successStatus = { state: 'done', remaining: 0, list: 'a,b', total: 2, done: 2, outcomes: 'ss', failed: 0, skipped: 0, current: 'DONE' }
      await evaluate(`window.editor.status(${JSON.stringify(successStatus)})`)
      await wait(3100)
      assert.equal(await evaluate(`window.__overlayFinishes || 0`), 0, 'default setting dismissed successful autoload')
      await evaluate(`window.editor.config({AUTOLOAD_RETURN_TO_DASHBOARD:true})`)
      await evaluate(`window.editor.status({state:'done',list:'local/kept-a,!3000,!1500,local/next',total:2,done:2,outcomes:'fk',failed:1,skipped:1,current:'DONE',entry_index:3})`)
      await wait(50)
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="0"]').textContent.includes('Failed')`), true)
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="3"]').textContent.includes('Skipped')`), true)
      assert.equal(await evaluate(`document.body.textContent.includes('All payloads loaded')`), false)

      // Only successful completion dismisses after three seconds; polling must not reset it.
      await wait(3100)
      assert.equal(await evaluate(`window.__overlayFinishes || 0`), 0, 'failed/skipped autoload dismissed automatically')
      await evaluate(`window.editor.status(${JSON.stringify(successStatus)})`)
      await wait(1600)
      assert.equal(await evaluate(`window.__overlayFinishes || 0`), 0, 'autoload dismissed too soon')
      await evaluate(`window.editor.status(${JSON.stringify(successStatus)})`)
      await eventually(() => evaluate(`window.__overlayFinishes === 1`), 'successful autoload did not dismiss')
      await wait(200)
      assert.equal(await evaluate(`window.__overlayFinishes`), 1, 'autoload dismissed more than once')
      await evaluate(`window.editor.status({state:'executing',remaining:0})`)
      await wait(50)
      await evaluate(`window.editor.status(${JSON.stringify(successStatus)})`)
      await wait(50)
      await evaluate(`window.editor.config({AUTOLOAD_RETURN_TO_DASHBOARD:false})`)
      await wait(3100)
      assert.equal(await evaluate(`window.__overlayFinishes`), 1, 'disabling auto-return did not cancel the timer')
      await evaluate(`window.editor.config({AUTOLOAD_RETURN_TO_DASHBOARD:true})`)
      await wait(50)
      await evaluate(`window.editor.show('logs')`)
      await wait(3100)
      assert.equal(await evaluate(`window.__overlayFinishes`), 1, 'dismiss timer survived unmount')
      await evaluate(`window.editor.show('overlay'); window.editor.status({state:'executing',remaining:0})`)

      // A long sequence follows delays and fast completed entries without scrolling the whole overlay.
      const longList = Array.from({ length: 24 }, (_, index) => index % 2 ? '!3000' : 'local/next').join(',')
      await evaluate(`window.editor.status({state:'executing',list:${JSON.stringify(longList)},total:12,done:0,outcomes:'pppppppppppp',current:'local/next',entry_index:0})`)
      await wait(50)
      const pageScroll = await evaluate(`document.querySelector('.fixed.inset-0').scrollTop`)
      const rowVisible = index => evaluate(`(() => {const row=document.querySelector('[data-sequence-index="${index}"]'), container=row.parentElement, r=row.getBoundingClientRect(), c=container.getBoundingClientRect();return container.scrollTop>0 && r.top>=c.top-1 && r.bottom<=c.bottom+1})()`)
      await evaluate(`window.editor.status({state:'executing',list:${JSON.stringify(longList)},total:12,done:9,outcomes:'sssssssssppp',current:'DELAY',entry_index:17,entry_duration_ms:3000,entry_remaining_ms:3000})`)
      await eventually(() => rowVisible(17), 'active delay was not scrolled into the list viewport')
      const colors = await evaluate(`(() => {const loaded=getComputedStyle(document.querySelector('[data-payload-index="0"]')).backgroundColor, pending=getComputedStyle(document.querySelector('[data-payload-index="20"]')).backgroundColor;return {loaded,pending}})()`)
      assert.notEqual(colors.loaded, colors.pending, 'completed payload did not have a distinct background')
      // Snapshot caught just after launch: no active row exists, but the latest processed row still needs to be visible.
      await evaluate(`window.editor.status({state:'executing',list:${JSON.stringify(longList)},total:12,done:11,outcomes:'sssssssssssp',current:'local/next',entry_index:20})`)
      await eventually(() => rowVisible(20), 'fast completed payload was not followed')
      await evaluate(`window.editor.status({state:'done',list:${JSON.stringify(longList)},total:12,done:12,outcomes:'ssssssssssss',current:'DONE',entry_index:23})`)
      await eventually(() => rowVisible(23), 'final delay was not visible at completion')
      assert.equal(await evaluate(`document.querySelector('.fixed.inset-0').scrollTop`), pageScroll, 'following the list scrolled the whole page')

      // Real repository hydration supplies the icon/title while the installed version remains accurate.
      await evaluate(`window.editor.status({state:'executing',list:'repo/demo@bbabc123,!3000,local/next',total:2,done:1,outcomes:'sp',current:'DELAY',entry_index:1,entry_duration_ms:3000,entry_remaining_ms:3000})`)
      await eventually(() => evaluate(`!!document.querySelector('[data-payload-index="0"] img')?.complete`), 'repository icon did not load')
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="0"] img').naturalWidth > 0`), true)
      for (const text of ['Example Payload', 'Example Author', '1.1']) {
        assert.equal(await evaluate(`document.querySelector('[data-payload-index="0"]').textContent.includes(${JSON.stringify(text)})`), true)
      }
      assert.equal(await evaluate(`document.querySelector('[data-payload-index="0"]').textContent.includes('Demo Repository')`), false)
      for (const [width, height] of [[1920, 1080], [390, 844]]) {
        await rpc('Emulation.setDeviceMetricsOverride', { width, height, deviceScaleFactor: 1, mobile: false }, sessionId)
        await wait(100)
        assert.equal(await evaluate(`document.documentElement.scrollWidth <= innerWidth`), true, 'overlay caused horizontal overflow')
        const sizes = await evaluate(`(() => {const payload=document.querySelector('[data-payload-index="0"]').getBoundingClientRect(), delay=document.querySelector('[data-delay-index="1"]').getBoundingClientRect();return {payload:payload.width,delay:delay.width,payloadHeight:payload.height,delayHeight:delay.height}})()`)
        assert.ok(Math.abs(sizes.delay - sizes.payload) < 1 && sizes.delayHeight < sizes.payloadHeight, 'delay did not stay compact')
        if (process.env.AUTOLOAD_SCREENSHOT_DIR) {
          const screenshot = await rpc('Page.captureScreenshot', { format: 'png' }, sessionId)
          await writeFile(join(process.env.AUTOLOAD_SCREENSHOT_DIR, `autoload-${width}.png`), Buffer.from(screenshot.data, 'base64'))
        }
      }
      await rpc('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false }, sessionId)

      // Reading old logs alone must not claim new activity; new logs must not drag the reader down.
      await evaluate(`window.editor.show('logs')`); await wait(100)
      await evaluate(`const scroller = document.querySelector('[class*="overflow-y-auto"]'); scroller.style.scrollBehavior='auto'; scroller.scrollTop=0; scroller.dispatchEvent(new Event('scroll'))`)
      await wait(50); assert.equal(await hasButton('New Activity Below'), false)
      await evaluate(`window.editor.logs(Array.from({length:101},(_,i)=>'line '+i))`)
      await eventually(() => hasButton('New Activity Below'), 'new log activity was not shown')
      assert.ok(await evaluate(`document.querySelector('[class*="overflow-y-auto"]').scrollTop < 100`), 'new logs dragged the reader to the bottom')
      await clickText('New Activity Below'); await eventually(async () => !(await hasButton('New Activity Below')), 'jump to bottom did not clear notice')

      // Real process component: StrictMode creates one initial request; leaving aborts it.
      await evaluate(`window.editor.show('processes')`)
      await eventually(() => pendingProcesses.filter(res => !res.destroyed).length === 1, 'process initial request absent/duplicated')
      const processWrites = requests.filter(url => url === '/processes_list').length
      pendingProcesses.shift().end(JSON.stringify({ processes: [{ pid: 12, name: 'safe.elf', memory: 1, is_daemon: true }] }))
      await eventually(() => evaluate(`document.body.textContent.includes('safe.elf')`), 'process list did not render')
      assert.equal(requests.filter(url => url === '/processes_list').length, processWrites)
      await click('Kill')
      await eventually(() => evaluate(`document.body.textContent.includes('No processes found')`), 'process kill did not update the list')
      await evaluate(`window.editor.mount(false)`); await wait(50)
      await wait(1100)
      assert.equal(requests.filter(url => url === '/processes_list').length, processWrites, 'post-kill timer survived unmount')
      await evaluate(`window.AbortController = undefined; window.editor.mount(true)`)
      await eventually(() => pendingProcesses.filter(res => !res.destroyed).length === 1, 'fallback process request absent')
      const oldRequest = pendingProcesses.filter(res => !res.destroyed)[0]
      await evaluate(`void window.__pollProcesses()`)
      await eventually(() => pendingProcesses.filter(res => !res.destroyed).length === 2, 'overlapping fallback poll did not start')
      const latest = pendingProcesses.filter(res => !res.destroyed)[1]
      latest.end(JSON.stringify({ processes: [{ pid: 98, name: 'latest.elf', memory: 1, is_daemon: true }] }))
      await eventually(() => evaluate(`document.body.textContent.includes('latest.elf')`), 'latest process response did not render')
      oldRequest.end(JSON.stringify({ processes: [{ pid: 99, name: 'old.elf', memory: 1, is_daemon: true }] }))
      await wait(50)
      assert.equal(await evaluate(`document.body.textContent.includes('old.elf')`), false, 'older response overwrote latest processes')
      await evaluate(`void window.__pollProcesses()`)
      await eventually(() => pendingProcesses.filter(res => !res.destroyed).length === 1, 'final pending poll did not start')
      await evaluate(`window.editor.mount(false)`)
      const stale = pendingProcesses.filter(res => !res.destroyed).shift()
      stale.end(JSON.stringify({ processes: [{ pid: 99, name: 'stale.elf', memory: 1, is_daemon: true }] }))
      await wait(50)
      assert.equal(await evaluate(`document.body.textContent.includes('stale.elf')`), false)

      // Full App under StrictMode: favorite saves serialize and stale config reads cannot replace edits.
      stored = { ...stored, FAVORITES: '' }; holdStatus = true
      await evaluate(`window.editor.show('app'); window.editor.mount(true)`)
      await eventually(() => evaluate(`Array.from(document.querySelectorAll('button')).some(b => b.title === 'Edit Favorites')`), 'App did not render')
      try { await eventually(() => evaluate(`document.body.textContent.toLowerCase().includes('next')`), 'App payloads did not load') }
      catch (error) { throw new Error(error.message + ': ' + JSON.stringify({ requests, page: await evaluate(`({text:document.body.textContent,errors:window.__unhandledErrors})`) })) }
      await click('Edit Favorites')
      const clickCard = name => evaluate(`Array.from(document.querySelectorAll('.glass-card > button')).find(b => b.textContent.toLowerCase().includes(${JSON.stringify(name.toLowerCase())})).click()`)
      // The launch/toggle button must cover the visual card padding on every side.
      const nextCard = `Array.from(document.querySelectorAll('.glass-card')).find(card => card.querySelector('button')?.textContent.toLowerCase().includes('next'))`
      let box = await evaluate(`(() => {const r=${nextCard}.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}})()`)
      await rpc('Input.dispatchMouseEvent', { type: 'mouseMoved', x: box.x + box.width / 2, y: box.y + box.height / 2 }, sessionId)
      await wait(300)
      for (const side of ['left', 'right', 'top', 'bottom']) {
        box = await evaluate(`(() => {const r=${nextCard}.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}})()`)
        const x = box.x + (side === 'left' ? 3 : side === 'right' ? box.width - 3 : box.width / 2)
        const y = box.y + (side === 'top' ? 3 : side === 'bottom' ? box.height - 3 : box.height / 2)
        assert.equal(await evaluate(`document.elementFromPoint(${x},${y})?.closest('button') === ${nextCard}.querySelector('button')`), true, `${side} card padding was not clickable`)
        await rpc('Input.dispatchMouseEvent', { type: 'mousePressed', x, y, button: 'left', clickCount: 1 }, sessionId)
        await rpc('Input.dispatchMouseEvent', { type: 'mouseReleased', x, y, button: 'left', clickCount: 1 }, sessionId)
        await eventually(() => pendingSaves.length === 1, `${side} edge click did not toggle the favorite`)
        releaseSave(); await wait(50)
      }
      await clickCard('Next'); await eventually(() => pendingSaves.length === 1, 'favorite save did not start')
      assert.equal(pendingSaves[0].update.FAVORITES, 'local/next')
      await clickCard('A'); await clickCard('B'); await wait(300)
      assert.equal(pendingSaves.length, 1, 'favorite writes ran concurrently')
      releaseSave(); await eventually(() => pendingSaves.length === 1, 'favorite queue did not drain')
      assert.equal(pendingSaves[0].update.FAVORITES, 'local/next,local/kept-a,local/kept-b')
      releaseSave(); await wait(50)
      assert.equal(await evaluate(`document.querySelector('button button') === null`), true, 'payload controls are nested buttons')
      await evaluate(`Array.from(document.querySelectorAll('.glass-card')).find(card => card.querySelector('button')?.textContent.toLowerCase().includes('next')).querySelector('button[aria-label="Move Right"]').click()`)
      await eventually(() => pendingSaves.length === 1, 'favorite move did not save')
      assert.equal(pendingSaves[0].update.FAVORITES, 'local/kept-a,local/next,local/kept-b', 'move also toggled the favorite')
      releaseSave(); await wait(50)
      holdAppConfig = true
      await clickText('Autoload')
      await eventually(() => pendingAppConfigs.length > 0, 'delayed App config fetch did not start')
      await clickText('Dashboard')
      await eventually(() => evaluate(`document.body.textContent.toLowerCase().includes('next')`), 'dashboard did not return')
      if (!(await evaluate(`Array.from(document.querySelectorAll('button[title="Edit Favorites"]')).some(b => b.className.includes('bg-yellow-400'))`))) await click('Edit Favorites')
      await clickCard('B')
      await eventually(() => pendingSaves.length === 1, 'favorite removal did not save')
      assert.equal(pendingSaves[0].update.FAVORITES, 'local/kept-a,local/next')
      releaseSave(); await wait(50)
      for (const res of pendingAppConfigs) res.end(JSON.stringify({ ...stored, FAVORITES: 'local/kept-a,local/next,local/kept-b', FAVORITES_INITIALIZED: true }))
      holdAppConfig = false; await wait(100)
      assert.equal(await evaluate(`localStorage.getItem('favoritePayloads')`), '["local/kept-a","local/next"]', 'stale config response replaced newer favorite edits')

      // Dashboard-only icon setting persists through navigation and keeps the card dimensions.
      const cardMetrics = () => evaluate(`(() => {const card=${nextCard}, r=card.getBoundingClientRect();return {width:r.width,height:r.height,text:card.querySelector('button .flex-col').getBoundingClientRect().width}})()`)
      const withIcons = await cardMetrics()
      await clickText('Settings')
      await eventually(() => evaluate(`!!document.querySelector('button[role="switch"][aria-label="Dashboard Payload Icons"]')`), 'icon setting was not shown')
      assert.equal(await evaluate(`document.querySelector('button[role="switch"]').getAttribute('aria-checked')`), 'true')
      const returnSwitch = `document.querySelector('button[role="switch"][aria-label="Return After Autoload"]')`
      assert.equal(await evaluate(`${returnSwitch}.getAttribute('aria-checked')`), 'false', 'auto-return should default to off')
      await evaluate(`${returnSwitch}.click()`)
      await eventually(() => stored.AUTOLOAD_RETURN_TO_DASHBOARD === true, 'auto-return setting did not save')
      await eventually(() => evaluate(`${returnSwitch}.getAttribute('aria-checked') === 'true'`), 'auto-return switch did not reflect the saved setting')
      await evaluate(`${returnSwitch}.click()`)
      await eventually(() => stored.AUTOLOAD_RETURN_TO_DASHBOARD === false, 'auto-return setting did not turn off')
      await evaluate(`document.querySelector('button[role="switch"][aria-label="Dashboard Payload Icons"]').click()`)
      await eventually(() => stored.SHOW_DASHBOARD_ICONS === false, 'icon setting did not save')
      await clickText('Dashboard')
      await eventually(() => evaluate(`document.querySelectorAll('.glass-card .lucide-package, .glass-card img').length === 0`), 'dashboard icons stayed visible')
      const withoutIcons = await cardMetrics()
      assert.equal(withoutIcons.width, withIcons.width)
      assert.equal(withoutIcons.height, withIcons.height)
      assert.ok(withoutIcons.text > withIcons.text + 40, 'hiding icons did not free text space')

      // Actual App details: wide README content stays inside its own scrollers; removal uses the installed slot.
      await clickText('Get Payloads')
      await eventually(() => evaluate(`!!document.querySelector('#cloud-repository')`), 'repository did not open')
      await eventually(() => evaluate(`document.querySelector('#cloud-repository img')?.naturalWidth > 0`), 'dashboard preference hid repository icons')
      for (const width of [320, 390, 768]) {
        await rpc('Emulation.setDeviceMetricsOverride', { width, height: 844, deviceScaleFactor: 1, mobile: false }, sessionId)
        for (const searching of [false, true]) {
          if (searching) await click('Search payloads by name or description...')
          await wait(50)
          assert.equal(await evaluate(`document.documentElement.scrollWidth <= innerWidth`), true, `repository toolbar overflowed at ${width}px`)
          assert.equal(await evaluate(`(() => {const upload=document.querySelector('label[title="Upload ELF Payload"]'), r=upload.getBoundingClientRect();return r.left>=0 && r.right<=innerWidth && r.width>0})()`), true, 'upload was outside the portrait viewport')
          if (searching) await click('Close')
        }
      }
      await evaluate(`Array.from(document.querySelectorAll('#cloud-repository button')).find(b => b.textContent.includes('Example Payload')).click()`)
      await eventually(() => pendingReadmes.length > 0, 'details README request did not start')
      assert.equal(await hasButton('Remove Payload'), true)
      assert.equal(await hasButton('Reinstall'), false)
      await rpc('Emulation.setDeviceMetricsOverride', { width: 900, height: 800, deviceScaleFactor: 1, mobile: false }, sessionId)
      await wait(50)
      assert.equal(await evaluate(`document.documentElement.scrollWidth <= innerWidth`), true, 'details overflowed before README')
      const readme = '# Wide README\n\n```\n' + 'long_code_line_'.repeat(200) + '\n```\n\n| Name | Value |\n| --- | --- |\n| ' + 'wide_table_cell_'.repeat(100) + ' | content |'
      readmeText = readme
      for (const response of pendingReadmes.splice(0)) response.end(readme)
      await eventually(() => evaluate(`document.body.textContent.includes('Wide README')`), 'README did not render')
      for (const width of [900, 768, 390]) {
        await rpc('Emulation.setDeviceMetricsOverride', { width, height: 844, deviceScaleFactor: 1, mobile: false }, sessionId)
        await wait(50)
        assert.equal(await evaluate(`document.documentElement.scrollWidth <= innerWidth && document.querySelector('main').scrollWidth <= document.querySelector('main').clientWidth`), true, `README widened the page at ${width}px`)
        assert.equal(await evaluate(`document.querySelector('.markdown-body pre').scrollWidth > document.querySelector('.markdown-body pre').clientWidth`), true, 'wide code should scroll within the README')
      }
      await clickText('Remove Payload')
      await eventually(() => evaluate(`document.body.textContent.includes('Are you sure you want to remove Example Payload?')`), 'remove did not request confirmation')
      assert.equal(deletedSlots.length, 0, 'remove ran before confirmation')
      await clickText('Confirm')
      await eventually(() => deletedSlots.length === 1, 'confirmed removal did not reach the server')
      assert.equal(deletedSlots[0], 'repo/demo@bbabc123')
      await eventually(() => hasButton('Install'), 'details did not update after removal')
      assert.equal(await hasButton('Remove Payload'), false)
      await clickText('Back to Repository')
      const invalidUpload = () => evaluate(`(() => {const input=document.querySelector('input[type="file"]'), transfer=new DataTransfer(); transfer.items.add(new File(['invalid'],'invalid.txt',{type:'text/plain'})); input.files=transfer.files; input.dispatchEvent(new Event('change',{bubbles:true}));})()`)
      for (let i = 0; i < 3; i++) await invalidUpload()
      await eventually(() => evaluate(`document.querySelectorAll('[data-toast-container] [role="alert"]').length === 3`), 'notification burst did not render')
      await evaluate(`window.__firstAlerts = Array.from(document.querySelectorAll('[data-toast-container] [role="alert"]')); void 0`)
      const stackedToasts = () => evaluate(`(() => {const rects=Array.from(document.querySelectorAll('[data-toast-container] > div')).map(node=>node.getBoundingClientRect());return rects.every((rect,index)=>rect.left>=0 && rect.right<=innerWidth && (!index || rect.top>=rects[index-1].bottom))})()`)
      assert.equal(await stackedToasts(), true, 'notifications overlapped or escaped the phone viewport')
      await rpc('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false }, sessionId)
      await wait(50); assert.equal(await stackedToasts(), true, 'desktop notifications overlapped')
      await wait(1800); await invalidUpload() // A new notification re-renders the existing ones.
      await wait(1500)
      assert.equal(await evaluate(`window.__firstAlerts.every(node => !node.isConnected)`), true, 'new notifications reset the older dismissal timers')
      assert.equal(await evaluate(`document.querySelectorAll('[data-toast-container] [role="alert"]').length`), 1, 'newest notification expired along with the older ones')
      await eventually(() => evaluate(`document.querySelectorAll('[data-toast-container] > div').length === 0`), 'notification stack did not expire')
      const statusCount = await evaluate(`window.__fetchRequests.filter(url => url === '/autoload_status').length`)
      await evaluate(`window.editor.mount(false)`)
      for (const res of pendingStatuses) if (!res.destroyed) res.end(JSON.stringify({ remaining: 5, state: 'countdown' }))
      await wait(700)
      assert.equal(await evaluate(`window.__fetchRequests.filter(url => url === '/autoload_status').length`), statusCount, 'unmounted status response scheduled a new poll')
      assert.equal(await evaluate('window.__unhandledErrors.length'), 0)
      console.log('Frontend lifecycle browser: StrictMode, rapid editor/favorite saves, failure/retry, exit/unmount, countdown validation/transitions, log scrolling and stale polling passed')
    }
  } else if (baseline) {
    await eventually(() => hasButton('Enable Autoload'), 'old editor did not reproduce premature Enable screen')
    releaseConfig(); await wait(100)
    assert.equal(await hasButton('Enable Autoload'), true, 'old editor unexpectedly loaded enabled settings')
    await evaluate(`Array.from(document.querySelectorAll('button')).find(b => b.textContent.trim() === 'Enable Autoload').click()`)
    await eventually(() => writes.length > 0, 'old editor did not save')
    assert.deepEqual(writes.at(-1), { AUTOLOAD_ENABLED: true, AUTOLOAD_LIST: '' })
    console.log('Baseline bfbe474 reproduced: delayed settings ignored; Enable clears the persisted list')
  } else {
  await eventually(loading, 'editor did not show loading state')
  assert.equal(await hasButton('Enable Autoload'), false)
  assert.equal(writes.length, 0)
  releaseConfig(); await eventually(() => hasButton('Disable Autoload'), 'enabled config was not loaded')
  assert.equal(writes.length, 0, 'loading config must not save it')
  await reload(); await eventually(loading, 'reloaded editor did not wait for settings')
  assert.equal(await hasButton('Enable Autoload'), false)
  releaseConfig(); await eventually(() => hasButton('Disable Autoload'), 'reload lost enabled state')
  assert.equal(writes.length, 0, 'reload/unload must not write an untouched list')
  await evaluate('window.editor.mount(false)'); await wait(50)
  await evaluate('window.editor.mount(true)'); await eventually(() => hasButton('Disable Autoload'), 'section remount failed')
  assert.equal(writes.length, 0)

  // Navigating away before the request completes must not flush defaults.
  await reload(); await eventually(loading, 'early navigation did not start loading')
  await evaluate('window.editor.mount(false)'); await wait(50)
  assert.equal(writes.length, 0, 'uninitialized unmount saved defaults')
  releaseConfig(); await wait(50); await evaluate('window.editor.mount(true)')
  await eventually(() => hasButton('Disable Autoload'), 'return after delayed config failed')

  // An actually disabled configuration can retain a populated sequence.
  stored = { AUTOLOAD_ENABLED: false, AUTOLOAD_LIST: keptList }
  await reload(); await eventually(loading, 'disabled reload did not wait')
  releaseConfig(); await eventually(() => hasButton('Enable Autoload'), 'disabled config did not show enable')
  await evaluate(`Array.from(document.querySelectorAll('button')).find(b => b.textContent.trim() === 'Enable Autoload').click()`)
  await eventually(() => writes.length === 1, 'enable did not save')
  assert.deepEqual(writes[0], { AUTOLOAD_ENABLED: true, AUTOLOAD_LIST: keptList })
  assert.equal(stored.AUTOLOAD_LIST, keptList)
  await eventually(() => hasButton('Disable Autoload'), 'enable did not update editor')
  const saved = writes.length
  await reload(); await eventually(loading, 'post-enable reload did not wait'); releaseConfig()
  await eventually(() => hasButton('Disable Autoload'), 'saved state did not survive reload')
  assert.equal(writes.length, saved)

  // Failed/incomplete fetches remain noneditable and cannot clear persistence.
  for (const [data, status] of [[{}, 500], [{ AUTOLOAD_ENABLED: true }, 200]]) {
    await reload(); await eventually(loading, 'failed/partial fetch initial loading missing')
    releaseConfig(data, status); await wait(100)
    assert.equal(await hasButton('Enable Autoload'), false); assert.equal(await loading(), true)
    await evaluate('window.editor.mount(false)'); await wait(50)
    assert.equal(writes.length, saved)
    assert.equal(stored.AUTOLOAD_LIST, keptList)
  }
  console.log('Autoload editor browser: delayed config, actual reload, section navigation, early exit, populated disabled enable, failed/partial loads all preserve settings/list')
  }
} finally {
  for (const res of pending) res.end('{}')
  for (const { res } of pendingSaves) res.end('failed')
  for (const res of [...pendingProcesses, ...pendingStatuses, ...pendingAppConfigs]) if (!res.destroyed) res.end('{}')
  ws?.close()
  if (child) {
    const exited = new Promise(resolve => child.once('exit', resolve)); child.kill('SIGTERM'); await Promise.race([exited, wait(2000)])
    if (child.exitCode === null) { child.kill('SIGKILL'); await Promise.race([exited, wait(2000)]) }
  }
  await vite.close(); await rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 })
}
