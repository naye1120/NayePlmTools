import test from 'node:test'
import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import {
  GameApiError, getSystemApps, setSystemAppHidden, setSystemAppPinned,
  appIconUrl, appTileBackgroundUrl,
  setAppTileIcon, setAppTileBackground, resetAppTileIcon, resetAppTileBackground,
} from '../src/utils/gameApi.js'

/* [CN-FORK] v0.6.0.179 「个性化软件」弹窗（SystemAppsModal）这一轮的接口层回归。
 *
 * 覆盖面与**不覆盖**面都写在这里，免得下次有人以为它管着界面：
 *   · 覆盖：图标 / 背景 URL 的拼装（含「资源 URL 不带时间戳」这条）、四颗开关 / 上传
 *     按钮发出去的请求形状（裸 PNG body、GET reset、PUT… 没有）、后端回话怎么变成
 *     组件要的字段（bytes / dds_bytes / accounts / idx / message）、失败时抛出的
 *     人话（弹窗拿它当红字 toast，并且**不改行内状态**）、以及 en/zh 两份 overlay 里
 *     appsys.* 的键名与顺序一致。
 *   · 不覆盖：React 那一层的乐观更新与回退、toast 文案本身 —— 这个包没有 DOM
 *     （devDependencies 里没有 jsdom / testing-library，`npm test` 是 `node --test`），
 *     渲染不了组件。那部分的落点在这里断言的**同一个出口**上：回退的条件是
 *     「这里抛出来的 GameApiError」，成功的条件是「这里解析出来的字段」。
 */

/* 与 gameApi 那套 request() 打交道的最小桩：按 URL 记下每次调用，按声明给回应。
 * 不起 http 服务、不引任何测试框架 —— 这几条要断言的是「前端发出去的请求长什么样」
 * 与「后端回什么样的 body 前端怎么处理」，不是网络本身。 */
function stubFetch(routes) {
  const calls = []
  const real = globalThis.fetch
  globalThis.fetch = async (url, opts = {}) => {
    calls.push({ url, opts })
    const hit = routes.find((r) => url.startsWith(r.match))
    if (!hit) throw new Error(`unexpected request: ${url}`)
    const status = hit.status || 200
    return { ok: status >= 200 && status < 300, status, text: async () => JSON.stringify(hit.body || {}) }
  }
  return { calls, restore: () => { globalThis.fetch = real } }
}

test('app icon / background URLs are resource URLs and carry no cache-busting timestamp', () => {
  assert.equal(appIconUrl('NPXS40037'), '/app_icon?tid=NPXS40037')
  assert.equal(appTileBackgroundUrl('NPXS40037'), '/app_tile_bg?tid=NPXS40037')
  /* 资源 URL（<img src>）**不能**带 `_=Date.now()`（见 gameApi v0.5.2.166 那条注释）：
   * 带了的话每次渲染都是一个新字符串 —— 浏览器每次都重下这张图，而且拿它当
   * 「值变了没有」的判据时永远不相等。破缓存靠调用方自己拼的 `&v=<版本号>`。 */
  assert.doesNotMatch(appIconUrl('NPXS40037'), /_=/)
  assert.doesNotMatch(appTileBackgroundUrl('NPXS40037'), /_=/)
  /* tid 必须转义：不转义时 `A B&c=d` 会在 query 里多长出一对键值。 */
  assert.equal(appIconUrl('A B&c=d'), '/app_icon?tid=A%20B%26c%3Dd')
})

test('system_apps returns pinned / custom_icon / custom_bg and keeps the JSON timestamp', async () => {
  const stub = stubFetch([{
    match: '/system_apps',
    body: {
      apps: [{ tid: 'NPXS40037', name: 'PlayStation Plus', hidden: true, pinned: true, controllable: true, custom_icon: 1, custom_bg: 0 }],
      count: 19, accounts: 2, error: '',
    },
  }])
  try {
    const data = await getSystemApps()
    /* JSON 接口这一档仍然带时间戳（与资源 URL 那一档刻意不同，见上面那条断言）。 */
    assert.match(stub.calls[0].url, /^\/system_apps\?_=\d+/)
    assert.equal(data.apps[0].pinned, true)
    assert.equal(data.apps[0].custom_icon, 1)
    assert.equal(data.apps[0].custom_bg, 0)
    assert.equal(data.count, 19)
    assert.equal(data.accounts, 2)
  } finally { stub.restore() }
})

test('icon upload posts the raw PNG blob (not JSON, not multipart) and returns the answer', async () => {
  const stub = stubFetch([{ match: '/app_tile_icon', body: { ok: true, bytes: 1234, message: '图标已写入主机' } }])
  try {
    const blob = new Blob(['fake-png'], { type: 'image/png' })
    const res = await setAppTileIcon('NPXS40037', blob)
    const { url, opts } = stub.calls[0]
    assert.match(url, /^\/app_tile_icon\?tid=NPXS40037&_=\d+/)
    assert.equal(opts.method, 'POST')
    /* 裸 body 与 JSON 那一档的分别就在这里：body 是 Blob 本身，且**不设** Content-Type
     * （浏览器按 blob.type 自己带 image/png）——与 /game_tile_icon 逐字同形。 */
    assert.equal(opts.body, blob)
    assert.equal(opts.headers, undefined)
    assert.equal(res.bytes, 1234)
    assert.equal(res.message, '图标已写入主机')
  } finally { stub.restore() }
})

test('a rejected upload throws the backend message so the row can toast it and stay unchanged', async () => {
  const stub = stubFetch([{
    match: '/app_tile_icon',
    status: 400,
    body: { error: 'EBAD', message: '这不是一张能读的 PNG 图片' },
  }])
  try {
    await assert.rejects(
      () => setAppTileIcon('NPXS40037', new Blob(['not-a-png'])),
      (e) => {
        assert.ok(e instanceof GameApiError)
        /* 弹窗直接把 e.message 当红字 toast（「别报代码，给中文提示」，v0.5.2.111 定下的）。 */
        assert.equal(e.message, '这不是一张能读的 PNG 图片')
        assert.equal(e.code, 'EBAD')
        assert.equal(e.status, 400)
        return true
      },
    )
  } finally { stub.restore() }
})

test('background upload goes to /app_tile_background and reports the DDS size', async () => {
  const stub = stubFetch([{
    match: '/app_tile_background',
    body: { ok: true, bytes: 99, dds_bytes: 1036928, message: '背景已写入主机并转成 DDS' },
  }])
  try {
    const blob = new Blob(['fake-bg-png'], { type: 'image/png' })
    const res = await setAppTileBackground('NPXS40037', blob)
    const { url, opts } = stub.calls[0]
    assert.match(url, /^\/app_tile_background\?tid=NPXS40037&_=\d+/)
    assert.equal(opts.method, 'POST')
    assert.equal(opts.body, blob)
    assert.equal(res.dds_bytes, 1036928)
  } finally { stub.restore() }
})

test('reset routes are plain GETs that hand back a human message', async () => {
  const stub = stubFetch([
    { match: '/app_tile_icon_reset', body: { ok: true, message: '已恢复主机自带的图标' } },
    { match: '/app_tile_background_reset', body: { ok: true, message: '已恢复主机自带的背景' } },
  ])
  try {
    const icon = await resetAppTileIcon('NPXS40037')
    const bg = await resetAppTileBackground('NPXS40037')
    assert.equal(stub.calls[0].opts.method, 'GET')
    assert.match(stub.calls[0].url, /^\/app_tile_icon_reset\?tid=NPXS40037&_=\d+/)
    assert.equal(icon.message, '已恢复主机自带的图标')
    assert.equal(stub.calls[1].opts.method, 'GET')
    assert.match(stub.calls[1].url, /^\/app_tile_background_reset\?tid=NPXS40037&_=\d+/)
    assert.equal(bg.message, '已恢复主机自带的背景')
  } finally { stub.restore() }
})

test('pin writes the host fixed list (accounts / idx) and hide never inverts its direction', async () => {
  const stub = stubFetch([
    {
      match: '/system_app_pin',
      body: { ok: true, tid: 'NPXS40037', pinned: true, accounts: 4, idx: 1, message: '已写入主机固定列表' },
    },
    { match: '/system_app_visible', body: { ok: true, tid: 'NPXS40037', hidden: true, message: '' } },
  ])
  try {
    const pin = await setSystemAppPinned('NPXS40037', true)
    assert.equal(stub.calls[0].opts.method, 'POST')
    assert.equal(stub.calls[0].opts.headers['Content-Type'], 'application/json')
    assert.deepEqual(JSON.parse(stub.calls[0].opts.body), { tid: 'NPXS40037', pinned: true })
    /* 置顶现在是**主机主屏固定列表**上的操作：accounts = 写了几个账号，idx = 置顶后的位置。
     * accounts === 0 是幂等命中（本来就在最前），弹窗据此换一句人话，不是失败。 */
    assert.equal(pin.accounts, 4)
    assert.equal(pin.idx, 1)

    /* 隐藏的方向按控件名走：hidden=true 就是「藏起来」，前端一次都不取反
     * （取反一次，后端再反一次，就成了「开关写着隐藏、按下去显示」）。 */
    const hide = await setSystemAppHidden('NPXS40037', true)
    assert.deepEqual(JSON.parse(stub.calls[1].opts.body), { tid: 'NPXS40037', hidden: true })
    assert.equal(hide.hidden, true)
  } finally { stub.restore() }
})

test('an upload that only reached the panel reports host_ok:false and carries the reason', async () => {
  /* 「不假成功」那条规矩的封条：面板这一份存好了（ok:true、200），但主机 app.db 那一列
   * 没写进去时，后端把原因写进 message、并用 host_ok:false 标出来。弹窗据此走**错误档**
   * 提示（红色 / 9 秒 / alert 语义），而不是那句写死的「已写入主机」。 */
  const stub = stubFetch([{
    match: '/app_tile_icon',
    body: {
      ok: true, bytes: 1234, host_ok: false,
      message: '已存到面板，但主机没写进去：主机固定列表写不进去（库被占用或只读）',
    },
  }])
  try {
    const res = await setAppTileIcon('NPXS40037', new Blob(['fake-png'], { type: 'image/png' }))
    /* 弹窗的判据就是这一句（`r.host_ok !== false`）：字段丢了会当成「写成功了」，
     * 于是用户在主屏上看不出变化时一句解释都没有。 */
    assert.equal(res.host_ok, false)
    assert.match(res.message, /主机没写进去/)
  } finally { stub.restore() }
})

test('appsys overlay keys exist in both languages, in the same order', async () => {
  const read = async (name) => JSON.parse(await readFile(new URL(`../src/locales-overlay/${name}.json`, import.meta.url), 'utf8'))
  const zh = await read('zh-CN')
  const en = await read('en-US')
  const keys = (o) => Object.keys(o).filter((k) => k.startsWith('appsys.'))
  /* 中文是源、英文是译本：**键名与顺序都要一一对应**（顺序也要对齐，不然两边
   * 逐行读下来会对不上，改一条时极易只改一边）。 */
  assert.deepEqual(keys(zh), keys(en))
  for (const name of [
    'title', 'row_desc', 'hint', 'boot_note', 'loading', 'empty', 'saving', 'saving_icon',
    'saving_bg', 'count_hint', 'load_fail', 'save_fail', 'hidden_toast', 'shown_toast',
    'hide', 'pin', 'hide_label', 'pin_label', 'upload_icon', 'upload_bg', 'reset_icon',
    'reset_bg', 'icon_custom', 'bg_custom', 'icon_saved', 'bg_saved', 'icon_reset', 'bg_reset',
    'icon_fail', 'bg_fail', 'reset_fail', 'not_image', 'pinned_toast', 'unpinned_toast',
    'pinned_same', 'unpinned_same',
  ]) {
    assert.equal(typeof zh[`appsys.${name}`], 'string', `zh-CN: appsys.${name}`)
    assert.equal(typeof en[`appsys.${name}`], 'string', `en-US: appsys.${name}`)
  }
  /* 置顶 toast 要把后端回的 idx 说出来（「已写进主机主屏固定列表（第 N 位）」）。 */
  assert.match(zh['appsys.pinned_toast'], /\{\{idx\}\}/)
  assert.match(en['appsys.pinned_toast'], /\{\{idx\}\}/)
})
