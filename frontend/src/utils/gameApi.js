/* [CN-FORK] v0.5.2.112 游戏管理模块的前端接口层
 *
 * 本轮只做**前端**（用户口径：「只做前端 UI + mock 数据」），所以这个文件同时承担两件事：
 *   ① 前端所有网络访问的唯一出口（组件里不出现裸 fetch，便于以后换实现）；
 *   ② **把接口契约定死**，后端照着这个文件实现即可，不用再猜字段名。
 *
 * 契约总览（路径、方法、请求体、响应体）
 * ─────────────────────────────────────────────────────────────────────────────
 * 详情
 *   GET  /game_detail?tid=<TID>
 *        → { tid, name, src, fmt, size, platform, source_type, image_type,
 *            content_id, version, path, runtime_path, appdb_size,
 *            install_time, last_launch, mounted, installed_pkg,
 *            can_uninstall, can_manage_source, saves_count, media_count,
 *            title_icon_custom, title_bg_custom }
 *   GET  /game_list              → { games:[{tid,name,src,fmt,size,icon?,bg?}], count, truncated }
 *        icon / bg 都是「自定义版本号」（number），有值前端才去取对应的自定义图，避免白跑 404
 *
 * 删除（异步任务，参考 ShadowMountPlus 的 /api/v1/games/delete 语义）
 *   POST /game_delete            {tid, confirm:true}
 *        → 202 {job_id, scan_queued}
 *        → 200 {source_missing:true, scan_queued}     源已经不在了（不算失败）
 *        → 409 {error:"EBUSY", reason:"game_active"|"rest_mode"|...}
 *        → 403 {error:"EPERM"}                        已安装 PKG 没有可删的物理源
 *   GET  /game_job_status?job=<id>
 *        → { job_id, name, active, phase, percent, bytes_done, bytes_total,
 *            files_done, files_total, rate, elapsed, idle, done, ok, error, reason }
 *        name = 打包任务的成品文件名（「游戏名称-打包时间戳.zip」；删除任务为空串），
 *               <a download> 的属性值直接用它。
 *        idle = [CN-FORK] v0.5.2.122「距上次有进展多少秒」（任务结束后恒 0）。
 *               不动的进度条分不清「在啃 500 MB 录屏」和「线程早死了」，有了它
 *               界面才能把「正在打包…」补成「已 45 秒没有进展」。
 *        phase: idle|preparing|measuring|packing|deleting|finalizing|done|failed|cancelled
 *               [CN-FORK] v0.5.2.115：packing = 打包任务在写 zip（后端已实现）；
 *               deleting/finalizing 是游戏删除任务的占位，还没接后端 —— 前端只把 phase
 *               原样显示，多出的取值不会出错，但中文映射表里也没有它的词条。
 *   GET  /game_job_cancel?job=<id>          → {ok}
 *
 * 存档
 *   GET  /game_saves?tid=<TID>
 *        → { tid, local_path, auto:{enabled,mode,smb,ftp,http,on_launch,on_exit},
 *            saves:[{id,name,kind,mtime,size,files}] }
 *   POST /game_save_auto         {enabled, mode, smb, ftp, http, on_launch, on_exit} → {ok}
 *   POST /game_save_test         {mode, smb, ftp, http}  → {ok, latency_ms}
 *   GET  /game_save_download?tid=&save=  → 二进制（zip）
 *   POST /game_save_upload?tid=&save=    裸 body（zip） → {ok, files, size}
 *   GET  /game_save_delete?tid=&save=    → {ok}
 *
 * 多媒体
 *   GET  /game_media?tid=<TID>
 *        → { tid, items:[{id,kind,name,dlname,mtime,size,w,h,duration}] }  kind: screenshot|video
 *        name   = 显示名「游戏名称-2026-10-06 12:59」（没改过名则是原文件名）
 *        dlname = **下载**落盘用的名字「游戏名称-20261006_125900.png」。
 *                 与 name 刻意不同：文件名里不能有空格与冒号，而显示名要给人看。
 *                 <a download> 的属性值直接用它，浏览器就不必猜名字。
 *   GET  /game_media_thumb?id=&w=320     → 图片（JPEG；视频没有封面帧时回占位图）
 *   GET  /game_media_file?id=            → 二进制（原图 / 原视频）
 *   GET  /game_media_file?id=&dl=1&tid=  → 同上，但带 Content-Disposition
 *                                        （游戏名称-截图时间.<ext>，RFC 5987 双写）
 *   POST /game_media_pack        {tid, ids:[...]|null}   null=全量 → {job_id}
 *   GET  /game_media_pack_get?job=<id>   → 二进制（zip，Content-Disposition
 *                                          是「游戏名称-打包时间戳.zip」）
 *   GET  /game_media_delete?tid=&id=     → {ok}
 *
 * 改名 / 换磁贴背景
 *   POST /game_rename            {tid, name} → {ok, name}
 *
 *   磁贴图分两层（v0.5.2.112），格式与尺寸各不相同：
 *   POST /game_tile_icon?tid=        裸 body（**PNG**，1:1，前端已缩到 1024×1024） → {ok, bytes}
 *   GET  /game_tile_icon_reset?tid=  → {ok}
 *   GET  /game_tile_icon?tid=       → 图片（自定义磁贴图标；没设过 → 404）
 *
 *   POST /game_tile_background?tid=  裸 body（**PNG**，16:9，前端已缩到 1920×1080） → {ok, bytes}
 *        **后端负责转成 1920×1080 的 DXT1 DDS**（与 make_pic0.py / assets/pic0.dds 同规格，
 *        头布局见该脚本的 build_bc1_header；参考实现见 src/psaccounts/avatar.c 的 write_dxt5_dds）。
 *        前端不自己编 DDS：PS5 老内核对 1920×1080 跑一遍压块要慢一拍，而且编码器在后端已有两份。
 *   GET  /game_tile_background_reset?tid=  → {ok}
 *   GET  /game_tile_bg?tid=         → **PNG** 预览（浏览器要给 <img> 显示，DDS 显示不了；
 *                                     后端把上传的 PNG 留着或转回 PNG 供这一路）
 *   GET  /game_tile_bg_dds?tid=     → DDS 原始字节（1920×1080 DXT1，128 + 480*270*8 = 1,036,928 B）
 *                                     给「下载/核对」用，也方便回归直接验规格
 *   注：图标与背景是两层，可各自独立设置/还原（v0.5.2.112）。
 *
 * 运行时层（与 ShadowMountPlus 的 games/mount|unmount|uninstall 同语义）
 *   POST /game_mount             {tid, mode:"ro"|"rw"} → {ok}
 *   POST /game_unmount           {tid} → {ok}
 *   POST /game_uninstall         {tid} → {ok, scan_queued}
 * ─────────────────────────────────────────────────────────────────────────────
 *
 * 错误约定：后端失败时回 `{error:"<机器码>", message:"<中文人话>"}`。
 * 前端**只展示 message**，读不到 message 才退回 HTTP 状态码 —— 这条是 v0.5.2.111 定下的
 * 「别报代码，给中文提示」的直接延续。
 */

/* ── 请求基础设施 ─────────────────────────────────────────────────────────
 * 与 AccountToolsSection 的 fetchWithTimeout 同一套做法（老内核没有 AbortSignal.timeout，
 * 手工接 AbortController；外部 signal 与内部超时任一触发都 abort）。
 * 之所以不抽成公共模块：那一份是账号模块的内部工具，抽出去要动已验证过的代码，
 * 收益不值这个风险。两份实现保持同构，改动时一起改。 */
export class GameApiError extends Error {
  constructor(message, info = {}) {
    super(message)
    this.name = 'GameApiError'
    this.status = info.status || 0
    this.code = info.code || ''
    this.reason = info.reason || ''
    this.body = info.body || null
  }
}

async function fetchWithTimeout(url, opts = {}, ms = 20000) {
  const ctl = new AbortController()
  const timer = setTimeout(() => { try { ctl.abort() } catch { /* 老内核偶发抛错，忽略 */ } }, ms)
  const outer = opts.signal
  const relay = () => { try { ctl.abort() } catch { /* 同上 */ } }
  if (outer) {
    if (outer.aborted) relay()
    else if (outer.addEventListener) outer.addEventListener('abort', relay)
  }
  try {
    return await fetch(url, { ...opts, signal: ctl.signal })
  } finally {
    clearTimeout(timer)
    if (outer && outer.removeEventListener) outer.removeEventListener('abort', relay)
  }
}

const TIMEOUTS = { get: 15000, post: 20000, upload: 120000, download: 120000 }

/* 统一请求。body 是对象 → JSON；是 Blob/File → 裸字节（对应后端的流式 raw body 上传，
 * 与 /manage:upload、/api/avatar/upload 同一套约定，后端不用引入 multipart 解析）。 */
async function request(path, { method = 'GET', body, signal, timeoutMs, raw = false } = {}) {
  const opts = { method, signal, cache: 'no-store' }
  if (body instanceof Blob || body instanceof ArrayBuffer) {
    opts.body = body
  } else if (body !== undefined && body !== null) {
    opts.body = JSON.stringify(body)
    opts.headers = { 'Content-Type': 'application/json' }
  }
  const ms = timeoutMs || (method === 'GET' ? TIMEOUTS.get : TIMEOUTS.post)

  let res
  try {
    res = await fetchWithTimeout(path, opts, ms)
  } catch {
    /* 区分「用户主动中断」与「真超时」：前者不该弹错误提示（关窗了就该安静）。
     * 这里用可选 catch 绑定（仓库约定，见 eslint no-unused-vars 不覆盖 catch 绑定）。 */
    if (signal && signal.aborted) throw new GameApiError('已取消', { code: 'ABORTED' })
    throw new GameApiError('连接不上主机，请确认面板还在运行', { code: 'NETWORK' })
  }

  if (raw) {
    if (!res.ok) {
      const info = await readErrorBody(res)
      throw new GameApiError(info.message, info)
    }
    return res
  }

  let data = null
  const text = await res.text().catch(() => '')
  if (text) { try { data = JSON.parse(text) } catch { data = null } }

  if (!res.ok) {
    const info = {
      status: res.status,
      code: (data && data.error) || '',
      reason: (data && data.reason) || '',
      body: data,
    }
    info.message = (data && (data.message || data.error)) || fallbackMessage(res.status)
    throw new GameApiError(info.message, info)
  }
  if (!data) throw new GameApiError('主机返回了无法识别的内容', { status: res.status, code: 'BADJSON' })
  return data
}

/* 错误响应体也要尽力读出来（有些后端只在 4xx 时带 message，正常响应才带 ok） */
async function readErrorBody(res) {
  const text = await res.text().catch(() => '')
  let data = null
  if (text) { try { data = JSON.parse(text) } catch { data = null } }
  return {
    status: res.status,
    code: (data && data.error) || '',
    reason: (data && data.reason) || '',
    body: data,
    message: (data && (data.message || data.error)) || fallbackMessage(res.status),
  }
}

/* 读不到后端 message 时的兜底中文。刻意**不含 HTTP 数字**给用户看之前先过一遍 t()，
 * 所以这里写成完整句子，调用方直接展示。 */
function fallbackMessage(status) {
  if (status === 404) return '主机上没有这个接口（后端尚未提供）'
  if (status === 403) return '这个操作不被允许'
  if (status === 409) return '主机当前正忙，请稍后再试'
  if (status === 413) return '文件太大了'
  if (status === 507) return '主机空间不足'
  if (status >= 500) return '主机内部出错了'
  return `请求失败（HTTP ${status}）`
}

/* ── 带 query 的小工具：所有 GET 都带时间戳防缓存 ───────────────────────────
 * 后端每个 GET 都不带 Cache-Control，而 PS5 的 WebKit 对同 URL 的 GET 会吃缓存 ——
 * /game_list 的既有写法就是 `?_=${Date.now()}`，这里沿用同一条约定。 */
function q(params) {
  const parts = []
  const all = { ...params, _: Date.now() }
  Object.keys(all).forEach((k) => {
    const v = all[k]
    if (v === undefined || v === null || v === '') return
    parts.push(`${encodeURIComponent(k)}=${encodeURIComponent(v)}`)
  })
  return parts.length ? `?${parts.join('&')}` : ''
}

/* ── 游戏详情 ─────────────────────────────────────────────────────────── */
export function getGameDetail(tid, opts) {
  return request(`/game_detail${q({ tid })}`, opts)
}

/* ── 删除（异步任务） ──────────────────────────────────────────────────── */
export function deleteGame(tid, opts) {
  return request('/game_delete', { ...opts, method: 'POST', body: { tid, confirm: true } })
}

export function getJobStatus(jobId, opts) {
  return request(`/game_job_status${q({ job: jobId })}`, opts)
}

export function cancelJob(jobId, opts) {
  return request(`/game_job_cancel${q({ job: jobId })}`, opts)
}

/* 轮询任务进度。返回一个 stop()；任务结束（成功/失败/取消）会自动停。
 * 1s 一次而不是更快：后端只有**一个**后台 worker（与 ShadowMountPlus 同构），
 * 高频轮询只会白烧主机的 CPU，而这个页面的进度条 1s 一跳已经足够顺。
 * 出错不抛给调用方 —— 轮询期间的网络抖动不该把已经开始的删除任务判死，
 * 由 onTick 拿到 error 字段自行决定怎么显示。
 *
 * [CN-FORK] v0.5.2.122 两处硬化，都是冲着「打包一直卡在这个页面」去的：
 *
 *  ① **onTick 抛异常绝不能带走轮询。** 原版 `onTick(st)` 裸写在 try 里，catch
 *     里又调一次 onTick —— 第二次再抛就从 async 函数里逃出去，底下那行
 *     `setTimeout(tick, …)` 再也执行不到，**轮询当场死掉**。表现正是用户报的：
 *     界面永远停在第一次 tick 的那份数字上（体积 0 B / 58.7 MB、用时 0 秒、
 *     进度条定住不动），而且**一条报错都不显示** —— 因为异常是从轮询器里逃的，
 *     不是渲染出来的。现在 onTick 一律包一层，抛了只当没这回事。
 *
 *  ② **连续失败要有尽头。** 主机侧真的没响应时，光靠 1s 一次的重试能把面板
 *     无限期吊在「正在打包…」上。现在连续失败累计到 STALL_GIVE_UP_MS 就收工，
 *     并把最后一句话交给 onTick（done:true）—— 宁可明确告诉用户「失联了，
 *     任务状态未知」，也不要让他对着一个永远不动的进度条等下去。 */
export function watchJob(jobId, onTick, { intervalMs = 1000 } = {}) {
  let stopped = false
  let timer = null
  let failSince = 0          // 连续失败是从什么时候开始的（0 = 当前没有失败）
  const STALL_GIVE_UP_MS = 60000

  const safeTick = (st) => {
    try { onTick(st) } catch { /* 回调自己的问题，不该拖死轮询 */ }
  }

  const tick = async () => {
    if (stopped) return
    try {
      const st = await getJobStatus(jobId)
      if (stopped) return
      failSince = 0
      safeTick(st)
      if (st && (st.done || st.active === false)) { stopped = true; return }
    } catch (e) {
      if (stopped) return
      const now = Date.now()
      if (!failSince) failSince = now
      const lost = now - failSince >= STALL_GIVE_UP_MS
      safeTick({
        job_id: jobId, done: !!lost, ok: false, active: !lost,
        error: e.message, transient: true, lost,
      })
      if (lost) { stopped = true; return }
    }
    if (!stopped) timer = setTimeout(tick, intervalMs)
  }
  tick()
  return () => { stopped = true; if (timer) clearTimeout(timer) }
}

/* ── 存档 ─────────────────────────────────────────────────────────────── */
export function getSaves(tid, opts) {
  return request(`/game_saves${q({ tid })}`, opts)
}

export function saveAutoConfig(tid, cfg, opts) {
  return request('/game_save_auto', { ...opts, method: 'POST', body: { tid, ...cfg } })
}

export function testRemote(tid, cfg, opts) {
  return request('/game_save_test', { ...opts, method: 'POST', body: { tid, ...cfg } })
}

export function saveDownloadUrl(tid, saveId) {
  return `/game_save_download${q({ tid, save: saveId })}`
}

export function uploadSave(tid, saveId, blob, opts) {
  return request(`/game_save_upload${q({ tid, save: saveId })}`, {
    ...opts, method: 'POST', body: blob, timeoutMs: TIMEOUTS.upload,
  })
}

export function deleteSave(tid, saveId, opts) {
  return request(`/game_save_delete${q({ tid, save: saveId })}`, opts)
}

/* ── 多媒体 ───────────────────────────────────────────────────────────── */
export function getMedia(tid, opts) {
  return request(`/game_media${q({ tid })}`, opts)
}

export function mediaThumbUrl(id, w = 320) {
  return `/game_media_thumb${q({ id, w })}`
}

export function mediaFileUrl(id, opts = {}) {
  /* dl=1 走「下载」语义：后端据此下发 Content-Disposition（游戏名称-截图时间.<ext>）。
   * 预览（<img>/<video>）不传 —— 对预览也挂 attachment，有的浏览器就不渲染了。
   * tid 只在 dl=1 时带：后端要用它取游戏名，取名规则与列表里的 dlname 同源。 */
  if (opts.dl) return `/game_media_file${q({ id, dl: 1, tid: opts.tid })}`
  return `/game_media_file${q({ id })}`
}

/* ids 传 null = 全量打包。两者走同一条后端路径，只是筛选范围不同。 */
export function packMedia(tid, ids, opts) {
  return request('/game_media_pack', { ...opts, method: 'POST', body: { tid, ids: ids || null } })
}

export function mediaPackUrl(jobId) {
  return `/game_media_pack_get${q({ job: jobId })}`
}

export function deleteMedia(tid, id, opts) {
  return request(`/game_media_delete${q({ tid, id })}`, opts)
}

/* ── 改名 / 磁贴图标 / 磁贴背景 ───────────────────────────────────────────
 * [CN-FORK] v0.5.2.112 用户口径：「你只把修改磁贴图标放出来了，还要增加一个修改背景的」
 * —— 磁贴上的图**拆成两层**，两层的语义、尺寸与格式都不同，所以是两套独立接口：
 *
 *   ① 磁贴图标（icon）—— 游戏封面那一格方图，默认取 app 的 icon0.png。
 *      换掉它就是自定义封面。**1:1 PNG**，铺满磁贴上半部。
 *   ② 磁贴背景（background）—— 整张卡片（含名称与 TID 那一行）的底图。
 *      **16:9**，图标压在它上面，所以背景只在图标四周与文字区露出来，是「氛围」不是「内容」。
 *
 * 格式上**前端只传 PNG，DDS 由后端转**（用户选定）：与头像那条路完全一致（前端传图片、
 * `avatar.c` 转 DXT5）。前端不自己编 DDS 的理由见上方契约块。
 *
 * 两个 GET 都是「没设过就 404」，调用方用 onError 退回默认 —— 走 URL 而不是 fetch，
 * 因为磁贴上是 <img src>，让浏览器自己管缓存与解码最省事。
 * 注意 /game_tile_bg 回的是 **PNG 预览**（浏览器显示不了 DDS），DDS 原始字节走 /game_tile_bg_dds。 */
export function renameGame(tid, name, opts) {
  return request('/game_rename', { ...opts, method: 'POST', body: { tid, name } })
}

export function setTileIcon(tid, blob, opts) {
  return request(`/game_tile_icon${q({ tid })}`, {
    ...opts, method: 'POST', body: blob, timeoutMs: TIMEOUTS.upload,
  })
}

export function resetTileIcon(tid, opts) {
  return request(`/game_tile_icon_reset${q({ tid })}`, opts)
}

export function tileIconUrl(tid) {
  return `/game_tile_icon${q({ tid })}`
}

export function setTileBackground(tid, blob, opts) {
  return request(`/game_tile_background${q({ tid })}`, {
    ...opts, method: 'POST', body: blob, timeoutMs: TIMEOUTS.upload,
  })
}

export function resetTileBackground(tid, opts) {
  return request(`/game_tile_background_reset${q({ tid })}`, opts)
}

export function tileBackgroundUrl(tid) {
  return `/game_tile_bg${q({ tid })}`
}

/* 后端转出来的 DDS 原始字节。**当前界面没有消费它**（<img> 显示不了 DDS），留在这里的理由
 * 是「路径只在这一个文件里写」这条约定 —— 以后要加「下载这张背景」的按钮，直接用它，
 * 不必再回来补一个 URL 拼装。回归脚本验规格时也走这个路径（`/game_tile_bg_dds`），
 * 期望 1920×1080 DXT1 = 128 + 480*270*8 = 1,036,928 字节，与 assets/pic0.dds 同规格。 */
export function tileBackgroundDdsUrl(tid) {
  return `/game_tile_bg_dds${q({ tid })}`
}

/* ── 挂载 / 卸载（运行时层，对应 ShadowMountPlus 的 games/mount|unmount）─── */
export function mountGame(tid, mode, opts) {
  return request('/game_mount', { ...opts, method: 'POST', body: { tid, mode } })
}

export function unmountGame(tid, opts) {
  return request('/game_unmount', { ...opts, method: 'POST', body: { tid } })
}

/* ── 卸载（走系统 AppInstUtil，对应 ShadowMountPlus 的 games/uninstall）─── */
export function uninstallGame(tid, opts) {
  return request('/game_uninstall', { ...opts, method: 'POST', body: { tid } })
}
