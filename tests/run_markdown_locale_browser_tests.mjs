import assert from 'node:assert/strict'
import { readFile, mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { spawn, execFileSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import { createServer } from '../frontend/node_modules/vite/dist/node/index.js'

const frontend = fileURLToPath(new URL('../frontend/', import.meta.url))
process.chdir(frontend)
const baseline = process.argv.includes('--baseline')
const iconsMode = process.argv.includes('--repository-icons')
let iconRequests = 0, iconRevision = 'r1', failClear = false, brokenIcon = false, clearRequests = 0
const fixtureIcon = 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+aT1cAAAAASUVORK5CYII='
const previousMarkdown = baseline ? execFileSync('git', ['show', 'c799297:frontend/src/utils/markdown.js'], { encoding: 'utf8' }) : null
const chrome = process.env.CHROME_BIN || (process.platform === 'darwin' ? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome' : '/usr/bin/google-chrome')
let readmeRequests = 0
const vite = await createServer({ root: frontend, configFile: join(frontend, 'vite.config.js'),
  server: { host: '127.0.0.1', port: 0 }, plugins: [{ name: 'markdown-locale-fixture', enforce: 'pre',
    transform(source, id) {
      if (previousMarkdown && id.split('?')[0] === join(frontend, 'src/utils/markdown.js')) return previousMarkdown
    }, configureServer(server) {
      server.middlewares.use(async (req, res, next) => {
        if (req.url === '/__markdown_page') {
          res.setHeader('Content-Type', 'text/html')
          res.end(await server.transformIndexHtml(req.url, `<!doctype html><html><body><div id="root"></div><script type="module" src="/tests/${iconsMode ? 'repositoryIcons' : 'markdownLocale'}.browser.jsx"></script></body></html>`))
          return
        }
        if (iconsMode) {
          const json = (value, status = 200) => { res.statusCode = status; res.setHeader('Content-Type', 'application/json'); res.end(JSON.stringify(value)) }
          if (req.url.startsWith('/__icon_control')) {
            const action = new URL(req.url, 'http://fixture').searchParams.get('action');
            if (action === 'new') iconRevision = 'r2';
            if (action === 'fail') failClear = true;
            if (action === 'restore') failClear = false;
            if (action === 'break') { brokenIcon = true; iconRevision = 'broken' }
            if (action === 'repair') { brokenIcon = false; iconRevision = 'repaired' }
            json({ iconRequests, clearRequests }); return
          }
          if (req.url === '/repository_payloads' || req.url === '/repository_refresh') {
            json({ sources: [{ id: 'default', url: 'https://mirror.invalid/payloads.json', icons_revision: iconRevision,
              payloads: [{ id: 'one', name: 'One', icon_key: 'shared' }, ...(iconRevision === 'r2' ? [{id:'two',name:'Two',icon_key:'new'}] : [])] }] }); return
          }
          if (req.url.startsWith('/repository_icons?')) { iconRequests++; json({ icons: {shared: brokenIcon ? 'data:image/png;base64,aGVsbG8=' : fixtureIcon, new: fixtureIcon} }); return }
          if (req.url === '/repository_cache_clear') { clearRequests++; json({ok:!failClear}, failClear ? 500 : 200); return }
        }
        if (req.url === '/__readme') {
          readmeRequests++
          res.setHeader('Cache-Control', 'public, max-age=3600')
          res.end('# Cached README :rocket:\n\nReadable without WAN.\n\n[Docs](https://example.com/docs)')
          return
        }
        next()
      })
    },
  }] })
await vite.listen()
const wait = ms => new Promise(resolve => setTimeout(resolve, ms))
async function eventually(condition, label) {
  for (let i = 0; i < 200; i++) { if (await condition()) return; await wait(25) }
  throw Error(label)
}
const profile = await mkdtemp(join(tmpdir(), 'pldmgr-markdown-browser-'))
let child, ws
try {
  child = spawn(chrome, ['--headless', ...(process.platform === 'linux' ? ['--no-sandbox'] : []), '--disable-gpu', '--no-first-run', '--no-default-browser-check',
    '--disable-background-networking', '--disable-component-update', '--disable-sync', '--disable-extensions', '--password-store=basic',
    '--user-data-dir=' + profile, '--remote-debugging-port=0', 'about:blank'])
  let launchError
  child.on('error', error => { launchError = error }); child.stdout.resume(); child.stderr.resume()
  let portInfo
  await eventually(async () => { if (launchError) throw launchError; try { portInfo = await readFile(join(profile, 'DevToolsActivePort'), 'utf8'); return true } catch { return false } }, 'Chrome did not start')
  const [port, path] = portInfo.trim().split('\n')
  ws = new WebSocket(`ws://127.0.0.1:${port}${path}`)
  await new Promise((resolve, reject) => { ws.onopen = resolve; ws.onerror = reject })
  let id = 0
  const calls = new Map(), remoteRequests = []
  ws.onmessage = event => {
    const reply = JSON.parse(event.data), call = calls.get(reply.id)
    if (call) { calls.delete(reply.id); reply.error ? call.reject(reply.error) : call.resolve(reply.result) }
    if (reply.method === 'Network.requestWillBeSent') {
      const url = reply.params.request.url
      if (/^https?:/.test(url) && !url.startsWith(`http://127.0.0.1:${vite.httpServer.address().port}/`)) remoteRequests.push(url)
    }
    if (reply.method === 'Fetch.requestPaused') {
      // The test never reaches any external host; record attempts via Network above.
      void rpc('Fetch.failRequest', { requestId: reply.params.requestId, errorReason: 'InternetDisconnected' }, reply.sessionId)
    }
  }
  function rpc(method, params = {}, sessionId) {
    return new Promise((resolve, reject) => {
      const call = ++id; calls.set(call, { resolve, reject })
      ws.send(JSON.stringify({ id: call, method, params, ...(sessionId ? { sessionId } : {}) }))
      setTimeout(() => { if (calls.delete(call)) reject(Error('Debugger timeout: ' + method)) }, 5000).unref()
    })
  }
  const { targetId } = await rpc('Target.createTarget', { url: 'about:blank' })
  const { sessionId } = await rpc('Target.attachToTarget', { targetId, flatten: true })
  await rpc('Network.enable', {}, sessionId)
  await rpc('Fetch.enable', { patterns: [{ urlPattern: 'https://*' }, { urlPattern: 'http://evil.test/*' }] }, sessionId)
  await rpc('Page.enable', {}, sessionId)
  async function evaluate(expression) {
    const response = await rpc('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }, sessionId)
    assert.equal(response.exceptionDetails, undefined, JSON.stringify(response.exceptionDetails))
    return response.result?.value
  }
  await rpc('Page.navigate', { url: `http://127.0.0.1:${vite.httpServer.address().port}/__markdown_page` }, sessionId)
  if (iconsMode) {
    await eventually(() => evaluate('window.fixtureReady && document.querySelector("[data-testid=icons] img")?.naturalWidth > 0'), 'cached repository image did not render')
    const control = action => evaluate(`fetch('/__icon_control?action=${action}').then(r=>r.json())`)
    assert.equal((await control('state')).iconRequests, 1)
    await evaluate('window.refreshIcons(true)'); assert.equal((await control('state')).iconRequests, 1)
    await control('new'); await evaluate('window.refreshIcons(true)')
    await eventually(() => evaluate('document.querySelectorAll("[data-testid=icons] img").length === 2'), 'new icon key did not render')
    assert.equal((await control('state')).iconRequests, 2)
    await control('fail')
    const clearButton = `Array.from(document.querySelectorAll('button')).find(b=>b.textContent.includes('Clear Cache'))`
    await evaluate(`${clearButton}.click()`)
    await eventually(() => evaluate(`document.body.textContent.includes('Could not clear repository cache.')`), 'cache failure was not shown')
    assert.equal(await evaluate('document.querySelectorAll("[data-testid=icons] img").length'), 2)
    await control('restore'); await evaluate(`${clearButton}.click()`)
    await eventually(() => evaluate(`document.body.textContent.includes('Repository cache cleared.')`), 'cache success was not shown')
    assert.equal((await control('state')).iconRequests, 3)
    await control('break'); await evaluate('window.refreshIcons(true)')
    await eventually(() => evaluate('document.querySelectorAll("[data-testid=icons] img").length === 0'), 'invalid image failed to show fallback')
    await control('repair'); await evaluate('window.refreshIcons(true)')
    await eventually(() => evaluate('document.querySelector("[data-testid=icons] img")?.naturalWidth > 0'), 'updated icon failed to recover after an image error')
    assert.deepEqual(remoteRequests, [])
    assert.deepEqual(await evaluate('window.__errors'), [])
    console.log('Repository icon browser: separate fetches, revision reuse, new keys, Settings clear success/failure, fallback and image recovery passed')
  } else {
  try {
    await eventually(() => evaluate('window.fixtureReady === true && document.body.textContent.includes("Cached README")'), 'real details panel failed to load README')
  } catch (error) {
    throw Error(error.message + ': ' + JSON.stringify(await evaluate('({text:document.body.textContent,errors:window.__errors,ready:window.fixtureReady})')))
  }
  if (baseline) {
    const serverMarkdown = await vite.ssrLoadModule('/src/utils/markdown.js')
    assert.ok(serverMarkdown.parseMarkdown('<script>evil()</script>').includes('<script>evil()</script>'), 'missing-DOM sanitizer bypass did not reproduce')
    assert.ok(await evaluate('document.querySelectorAll(".markdown-body img.emoji").length > 0'))
    await eventually(() => remoteRequests.some(url => url.includes('cdn.jsdelivr.net')), 'old emoji dependency not reproduced')
    console.log('Baseline c799297: remote CDN emoji requests on unavailable WAN and raw script HTML returned without a DOM sanitizer reproduced')
  } else {
    assert.equal(await evaluate('document.querySelector(".markdown-body img")'), null)
    assert.ok(await evaluate('document.querySelectorAll(".markdown-body .emoji-sprite").length >= 5'), 'bundled emoji were not generated')
    const sprite = await evaluate(`(async () => {
      const node=document.querySelector('.emoji-sprite'), style=getComputedStyle(node);
      const image=new Image(); image.src=style.backgroundImage.slice(5,-2);
      await new Promise((resolve,reject)=>{image.onload=resolve;image.onerror=()=>reject(Error('sprite failed to decode'));if(image.complete&&image.naturalWidth)resolve()});
      const canvas=document.createElement('canvas');canvas.width=canvas.height=20;
      const index=Number([...node.classList].find(c=>/^emoji-\\d+$/.test(c)).slice(6));
      canvas.getContext('2d').drawImage(image,(index%64)*20,Math.floor(index/64)*20,20,20,0,0,20,20);
      return {width:image.naturalWidth,height:image.naturalHeight,box:node.getBoundingClientRect().width,
        indent:style.textIndent,alpha:[...canvas.getContext('2d').getImageData(0,0,20,20).data].filter((v,i)=>i%4===3&&v>0).length};
    })()`)
    assert.equal(sprite.width, 1280); assert.equal(sprite.height, 1200)
    assert.ok(sprite.box > 0 && sprite.alpha > 100)
    assert.equal(sprite.indent, '-9999px', 'native missing glyph is not hidden')
    const positions = await evaluate(`document.querySelector('style[data-emoji-positions]').sheet.cssRules.length`)
    await evaluate(`window.markdown.parseMarkdown(':pig: :pig: :rocket:')`)
    assert.equal(await evaluate(`document.querySelector('style[data-emoji-positions]').sheet.cssRules.length`), positions, 'repeated emoji duplicated position rules')
    assert.deepEqual(remoteRequests, [], 'markdown tried to load external assets')
    const probes = [
      '<script>window.__xss=1</script><p onclick="window.__xss=2">hello</p>',
      '<img src="https://evil.test/image" onerror="window.__xss=3"><picture><source srcset="https://evil.test/source"></picture><video poster="https://evil.test/poster"></video><audio src="https://evil.test/audio"></audio><iframe src="https://evil.test/frame"></iframe><object data="https://evil.test/object"></object><embed src="https://evil.test/embed">',
      '<svg><a xlink:href="javascript:window.__xss=4">SVG</a></svg><math><mtext><table><mglyph><style><!--</style><img title="--><img src=x onerror=window.__xss=5>">',
      '<style>@import "https://evil.test/css";</style><p style="background:url(https://evil.test/bg)">styled</p><link href="https://evil.test/link" rel="stylesheet"><form action="https://evil.test/form"><input autofocus onfocus="window.__xss=6"></form>',
      '[js](javascript:window.__xss=7) [data](data:text/html,evil) [vb](vbscript:evil) <a href="jav&#x61;script:evil()">encoded</a><a href="java\nscript:evil()">newline</a>',
      '![Markdown media](https://evil.test/markdown.png)\n\n<img src="https://evil.test/raw.png">',
      '<span class="emoji-sprite emoji-0" role="img" aria-label="fake" style="background:url(https://evil.test/fake)">ordinary words</span>',
      '# Heading :pig:\n\n**bold** *italic* ~~deleted~~\n\n- item\n\n1. numbered\n\n> quote\n\n```js\n<script>example</script>\n```\n\n| A | B |\n| --- | --- |\n| one | two |\n\n[safe](https://example.com/docs "Title") [relative](./docs) [anchor](#section)\n\n👩🏽‍💻 🇵🇱 1️⃣ ❤️ :rocket: :unknown:',
    ]
    for (const input of probes) {
      const result = await evaluate(`(() => {
        const root = document.createElement('div'); root.id='probe'; root.className='markdown-body'; root.innerHTML=window.markdown.parseMarkdown(${JSON.stringify(input)}); document.body.append(root);
        const forbidden = root.querySelectorAll('script,img,picture,video,audio,source,iframe,object,embed,svg,math,style,link,form,input');
        const badAttributes = [...root.querySelectorAll('*')].flatMap(el => [...el.attributes].filter(a => /^on|^(style|src|srcset|xlink:href)$/i.test(a.name)).map(a=>a.name));
        const links = [...root.querySelectorAll('a[href]')].map(a => a.getAttribute('href'));
        const result={forbidden:forbidden.length,badAttributes,links,html:root.innerHTML,text:root.textContent};root.remove();return result;
      })()`)
      assert.equal(result.forbidden, 0, input)
      assert.deepEqual(result.badAttributes, [], input)
      assert.ok(result.links.every(href => !/^(javascript|vbscript|data):/i.test(href)))
      if (input.startsWith('# Heading')) {
        for (const tag of ['h1', 'strong', 'em', 'del', 'ul', 'ol', 'blockquote', 'pre', 'code', 'table']) assert.ok(result.html.includes(`<${tag}`), tag)
        assert.deepEqual(result.links, ['https://example.com/docs', './docs', '#section'])
        assert.ok(result.text.includes('👩🏽‍💻 🇵🇱 1️⃣ ❤️ 🚀 :unknown:'))
        assert.ok(result.text.includes('<script>example</script>'))
      }
      if (input.includes('aria-label="fake"')) assert.ok(!result.html.includes('emoji-sprite'), 'untrusted markup forged a sprite')
    }
    assert.equal(await evaluate('window.__xss'), undefined)
    for (const purifier of ['null', '{}', '{isSupported:false,sanitize(){throw Error("should not run")}}']) {
      const text = await evaluate(`(() => {const el=document.createElement('div');el.innerHTML=window.markdown.createMarkdownParser(${purifier})('<script>evil()</script> :pig:');return {text:el.textContent,scripts:el.querySelectorAll('script').length}})()`)
      assert.equal(text.scripts, 0); assert.equal(text.text, '<script>evil()</script> 🐷')
    }
    await rpc('Network.emulateNetworkConditions', { offline: true, latency: 0, downloadThroughput: -1, uploadThroughput: -1 }, sessionId)
    await evaluate(`const node=document.createElement('div');node.id='offline-markdown';node.className='markdown-body';node.innerHTML=window.markdown.parseMarkdown('# Offline :pig: :rocket: 👩🏽‍💻');document.body.append(node)`)
    assert.ok(await evaluate('document.getElementById("offline-markdown").textContent.includes("🐷 🚀 👩🏽‍💻")'))
    assert.equal(await evaluate('document.querySelectorAll("#offline-markdown .emoji-sprite").length'), 3)
    assert.ok(await evaluate('getComputedStyle(document.querySelector("#offline-markdown .emoji-sprite")).backgroundImage !== "none"'))
    assert.ok(await evaluate('document.body.textContent.includes("Cached README 🚀")'))
    await wait(100); assert.deepEqual(remoteRequests, [], 'offline rendering attempted external requests')
    // Remount the real panel to exercise the browser's cached README response.
    await evaluate('window.showView("closed")'); await wait(50)
    const beforeCache = readmeRequests
    await evaluate('window.showView("details")')
    await eventually(() => evaluate('document.body.textContent.includes("Cached README 🚀")'), 'cached README did not render')
    assert.equal(readmeRequests, beforeCache, 'README cache unexpectedly hit transport')
    await rpc('Network.emulateNetworkConditions', { offline: false, latency: 0, downloadThroughput: -1, uploadThroughput: -1 }, sessionId)
    // External link confirmation is still owned by the actual detail panel.
    await evaluate(`document.querySelector('.markdown-body a[href="https://example.com/docs"]').click()`)
    await eventually(() => evaluate('document.body.textContent.includes("Open External Link")'), 'external link confirmation absent')
    assert.deepEqual(await evaluate(`(() => {const a=[...document.querySelectorAll('a')].find(a=>a.textContent.trim()==='Open Link');return {href:a.getAttribute('href'),target:a.target,rel:a.rel}})()`), { href: 'https://example.com/docs', target: '_blank', rel: 'noopener noreferrer' })
    await evaluate(`Array.from(document.querySelectorAll('button')).find(b=>b.textContent.trim()==='Cancel').click()`)
    await eventually(() => evaluate('!document.body.textContent.includes("Open External Link")'), 'cancel did not restore details')
    await evaluate(`document.querySelector('.markdown-body a[href="#section"]').click()`)
    assert.equal(await evaluate('document.body.textContent.includes("Open External Link")'), false)
    // Real settings view exposes Dutch, persists explicit selection, and defaults to browser.
    await evaluate('window.showView("settings")')
    await eventually(() => evaluate('!!document.querySelector("select")'), 'settings missing')
    assert.equal(await evaluate(`document.querySelector('option[value="nl-NL"]').textContent`), 'Nederlands')
    await evaluate(`{const select=document.querySelector('select');select.value='nl-NL';select.dispatchEvent(new Event('change',{bubbles:true}))}`)
    await eventually(() => evaluate('document.body.textContent.includes("Instellingen")'), 'Dutch UI did not change')
    assert.equal(await evaluate('localStorage.getItem("i18nextLng")'), 'nl-NL')
    await rpc('Page.reload', {}, sessionId)
    await eventually(() => evaluate('window.fixtureReady === true && window.locale.resolvedLanguage === "nl-NL" && document.body.textContent.includes("Cached README")'), 'Dutch preference did not survive reload')
    await evaluate('window.showView("settings")')
    await eventually(() => evaluate('!!document.querySelector("select")'), 'reloaded settings missing')
    await evaluate('window.locale.changeLanguage("nl")')
    assert.equal(await evaluate('window.locale.resolvedLanguage'), 'nl-NL')
    assert.equal(await evaluate('window.locale.t("nonexistent", {defaultValue:"Fallback"})'), 'Fallback')
    assert.equal(await evaluate('window.locale.t("settings.title", {lng:"unsupported"})'), 'Settings')
    for (const language of ['pl-PL', 'ar-SA', 'zh-CN']) {
      await evaluate(`window.locale.changeLanguage(${JSON.stringify(language)})`)
      assert.ok(await evaluate('document.querySelector("select").options.length >= 18'))
      assert.equal(await evaluate('document.body.textContent.includes("settings.title")'), false)
    }
    await evaluate(`{const select=document.querySelector('select');select.value='__auto__';select.dispatchEvent(new Event('change',{bubbles:true}))}`)
    await eventually(() => evaluate('localStorage.getItem("i18nextLng") === null'), 'system default failed to clear preference')
    assert.deepEqual(remoteRequests, [])
    assert.deepEqual(await evaluate('window.__errors'), [])
    console.log('Markdown/locale browser: real DOM sanitation, malicious URLs/media, unavailable sanitizer, decoded offline emoji sprites, cached README, link confirmation, Dutch selection/fallback and representative locales passed')
  }
  }
} finally {
  ws?.close()
  if (child) {
    const exited = new Promise(resolve => child.once('exit', resolve)); child.kill('SIGTERM'); await Promise.race([exited, wait(2000)])
    if (child.exitCode === null) { child.kill('SIGKILL'); await Promise.race([exited, wait(2000)]) }
  }
  await vite.close(); await rm(profile, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 })
}
