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
 *        [CN-FORK] v0.5.2.127 这几个字段的语义（对齐 ShadowMountPlus 1.7）：
 *          src            = **从哪枚举到的**（installed=app.db / dump=扫描路径）。
 *                           它**不是**来源类型：ShadowMount 的镜像/裸目录游戏同样
 *                           注册在 app.db 里，src 也是 installed。只配给「已安装/转储」
 *                           那种「列表里怎么找到它的」徽标用。
 *          source_type    = **物理源类型**，只看 /user/app/<TID>/ 下那两个 lnk 文件：
 *                           pkg（两个都没有）/ image（mount_img.lnk）/ folder（mount.lnk）。
 *          image_type     = 镜像扩展名（ffpkg/exfat/ffpfs/ffpfsc），非镜像为空串。
 *          path           = 物理源：镜像=那个镜像文件，裸目录=源目录，pkg=app 目录。
 *          mounted        = 此刻**真的挂进系统**了（/system_ex/app/<TID>/sce_sys/param.json
 *                           存在）。不是「runtime_path 存在」—— 裸目录游戏的源目录一直在。
 *          installed_pkg  = 真·内置安装的 PKG（= source_type=="pkg"）。
 *          can_manage_source = !installed_pkg，后端判定，前端只认这个字段。
 *   GET  /game_list[?force=1]    → { games:[{tid,name,src,fmt,size,icon?,bg?}], count, truncated }
 *     后端有 60s 快照缓存（app.db 全库 + 扫描路径是重 IO）。进页面那次 load 吃缓存秒开；
 *     点右上角「刷新」要带 force=1，否则用户连点十次也只是等快照自然过期 ——
 *     外部改了 app.db（FTP、SMP 扫进新游戏）会「点了刷新看不到」，体感就是按钮坏了。
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
 *        → { tid, local_path, auto:{enabled,mode,smb,ftp,http,on_launch,on_exit,snap},
 *            auto_state:{running,last:{action,ok,at,message}|null},
 *            saves:[{id,name,kind,mtime,size,files}],
 *            snap_used, snapshots:[{uid,ts,mtime,size,files}] }
 *        saves[] 每条 = **一个存过这个游戏的用户**（id/name 都是 uid）；一台主机可以
 *        有多个用户都存过同一个游戏，所以不止一条，互不覆盖。
 *        auto.snap = 本地快照配置 {enabled, interval(秒), keep(份)}。
 *        snapshots[] = 已经存在的本地快照，**新的在前**。
 *        auto_state = [CN-FORK] v0.5.2.142 **自动存档引擎的运行期状态**（只读，不是配置）：
 *          running   此刻这款游戏是不是正在跑（后端**当场探测**，不是守护线程记的滞后值）
 *          last      最近一次自动/手动同步：action=push|pull、ok、at（本地时间字符串）、
 *                    message（失败原因，成功时是空串）。从没同步过则是 null。
 *          没接后端时它是 {}（前端一律当「没有状态」处理，不要假设字段在）。
 *        ⚠ auto.mode 的 "smb" 是**历史遗留**：SDK 那份 libcurl 没编 smb，真机上选了它
 *        传不出去。新配置默认 ftp，前端把 SMB 那一档做成禁用并给出原因。
 *   POST /game_save_auto         {enabled, mode, smb, ftp, http, on_launch, on_exit, snap} → {ok}
 *   POST /game_save_test         {mode, smb, ftp, http}  → {ok, latency_ms}
 *   POST /game_save_sync         {tid, action:"push"|"pull", uid?} → {ok, action, uids, files, size, failed, note}
 *     [CN-FORK] v0.5.2.142 **立即同步**：不等「启动/退出游戏」那两个字，手动推一次 / 拉一次。
 *     与自动走的是**同一套**后端代码（同一个互斥锁 —— 「整份替换」并发会互相删掉对方
 *     刚改名的目录）。uid 不传 = 这个游戏在主机上的全部用户各来一份。
 *     远端还没有对应文件时回 **ok:true** + note「远程还没有这个游戏的存档」（首次使用
 *     必然遇到，不该是一条红报错）；真的连不上 / 协议不支持才是 400 + message。
 *     note 是给人话的总结，界面直接展示。
 *   GET  /game_save_download?tid=&save=  → 二进制（zip）
 *   POST /game_save_upload?tid=&save=    裸 body（zip） → {ok, files, size}
 *   GET  /game_save_delete?tid=&save=    → {ok}
 *   GET  /game_save_remote_info?tid=&save= → {configured,exists,mode?,size?,files?,mtime?,error?}
 *        [CN-FORK] v0.5.2.145 远程存档只读元数据（大小/文件数/修改时间），供「远程游戏」区块展示。
 *   POST /game_save_snap         {tid, action:"run"}                  → {ok, made}
 *                                {tid, action:"restore", uid, ts}     → {ok}
 *                                {tid, action:"delete",  uid, ts}     → {ok}
 *     [CN-FORK] v0.5.2.140 本地快照 —— 不依赖网络的兜底：守护线程按每款游戏自己的
 *     间隔把存档整份复制到主机 /data 下，超保留数删最旧。run 是「立即备份」
 *     （内容没变就不新增，made=0 是正常结果）；restore 走的是与上传**同一条**
 *     整份替换逻辑（旧的先留一手，失败能退回去）。uid 与 ts 是路径的最后两段，
 *     非法一律 400 —— 别在别处再拼一次这个路径。
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
 *   POST /game_rename            {tid, name} → {ok, name, persisted, persist_note}
 *     v0.5.2.132 起带 persisted：名字**源**（游戏包里的 sce_sys/param.json）也改成功
 *     才为 true —— 只有它才是真的不回退。ShadowMount 重扫时会用自己的源重建
 *     app.db/appinfo.db，所以只改面板层与主机索引层的那两版（≤.131）一定会被冲回去。
 *     persisted=false 时 persist_note 说明原因（镜像游戏 / 系统内置 / 源写失败）。
 *     **不要**把 false 当错误：面板层与主机索引层已经生效，改名本身是成功的，
 *     只是重启 SMP 之后电视主屏会回到原名。
 *
 *   POST /game_title_sync        {} → {ok, scanned, applied, persisted, failed, note}
 *     [CN-FORK] v0.5.2.137 名字回填：把面板里**所有**改过的名字重放到主机各层。
 *     改名只在改名那一刻写主机，所以历史遗留（改名发生在 .132 写源之前）与
 *     「SMP 重启重新注册把 app.db 冲回原名」都只能靠事后补 —— 这条就是那个补。
 *     幂等（已一致的款后端一次写都不发），失败也不改变 ok:true：它是 best-effort 的重放。
 *     真机实测 SMP 的 title_name 读的是系统 app.db，所以镜像类（源写不了）同样能对齐。
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

  /* [CN-FORK] v0.5.2.125 **必须是函数声明，不能写成 `const tick = async () => {…}` 再在体内
   * `setTimeout(tick, intervalMs)` 自引用。**
   *
   * 构建 target 是 ['es2015','safari12']，es2015 没有 async，Babel 于是把 async 降级成
   * generator 包装。对「自引用的 const 箭头函数」，Babel 会把整个 `const tick = …` 换成一个
   * 立即调用并丢弃的 IIFE，却**不重写函数体里的那个自引用** —— 产物里留下一句
   * `setTimeout(tick, a)`，而 `tick` 这个绑定已经不存在了。
   *
   * 真机症状（v0.5.2.124 实测）：第一次 `/game_job_status` 轮询正常返回，随后 `setTimeout`
   * 抛 ReferenceError: tick is not defined，轮询**永久停摆**。界面停在「正在打包… 0%」，
   * 「下载打包文件」按钮永远不出现 —— 而后端其实 1.2 秒就把 61MB 的包打好了。
   *
   * 为什么本地回归没抓到：game112.mjs 跑的是 vite dev server，发出去的是原生 ESM
   * （async/await 不降级），这条 bug 只在**构建产物**里存在。见 HANDOVER §6。
   *
   * 函数声明的名字绑定在外层作用域，降级后自引用仍然有效。 */
  async function tick() {
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

/* [CN-FORK] v0.5.2.145 取某用户远程存档的只读元数据（大小 / 文件数 / 修改时间），
 * 供「存档管理」弹窗的「远程游戏」区块展示。后端直出 JSON：
 *   { configured, exists, mode?, size?, files?, mtime?, error? } */
export function getRemoteSaveInfo(tid, saveId, opts) {
  return request(`/game_save_remote_info${q({ tid, save: saveId })}`, opts)
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

/* [CN-FORK] v0.5.2.140 本地快照。action = 'run' | 'restore' | 'delete'；
 * restore/delete 还要带 { uid, ts }（ts 是快照时间戳，原样回传，别自己拼）。 */
export function snapAction(tid, action, extra, opts) {
  return request('/game_save_snap', {
    ...opts, method: 'POST', body: { tid, action, ...(extra || {}) },
  })
}

/* [CN-FORK] v0.5.2.142 立即同步（手动推 / 拉）。action = 'push' | 'pull'；
 * uid 不传 = 这个游戏在主机上的全部用户各来一份（后端自己枚举）。 */
export function syncSaves(tid, action, uid, opts) {
  return request('/game_save_sync', {
    ...opts,
    method: 'POST',
    body: { tid, action, ...(uid ? { uid } : {}) },
    /* 整份存档可能要几十 MB，走上传那一档超时（20s 不够 —— 那是给普通 JSON 的）。 */
    timeoutMs: TIMEOUTS.upload,
  })
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

/* [CN-FORK] v0.5.2.137 名字回填：把面板里所有改过的名字（name.txt）重放到主机各层。
 *
 * 为什么需要它 —— 改名只在**改名那一刻**写主机，于是有两种情况主机侧会落后：
 *   ① 改名发生在 .132（写游戏源 param.json）之前：当时只写了面板 + app.db，
 *      之后 app.db 又被 SMP 重扫按源重建，中文名被冲回英文原名；
 *   ② 镜像类（.ffpfsc）的源锁在镜像里，任何一次 SMP 重新注册都可能让名字回退。
 * 两种情况用户都只看到「面板是对的、SMP 与电视主屏是错的」，而逐款手点改名谁也不愿意。
 *
 * 真机实测（v0.5.2.137）：SMP 1.7beta4 的 title_name 读的是系统 app.db —— 这条链写完
 * app.db 后 SMP 的 UI 立刻跟着变，**镜像类也一样**；所以这不是「只对某类游戏有效」的补丁。
 *
 * 无请求体、无参数：它读的是我们自己的 name.txt，不接受外部指定要改谁（也就没法被拿来
 * 改别人的游戏）。响应：
 *   { ok:true, scanned, applied, persisted, failed, note }
 *     scanned   有 name.txt 的款数
 *     applied   主机索引（app.db/appinfo.db）已一致或已写好的款数
 *     persisted 名字**源**（sce_sys/param.json）也写成功的款数（只有 folder 类可能）
 *     failed    写了但失败的款数
 *     note      一句给人话的总结，UI 直接展示即可
 * 幂等：已经一致的款后端一次写都不发，所以可以放心反复点。 */
export function syncTitleNames(opts) {
  return request('/game_title_sync', { ...opts, method: 'POST', body: {} })
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
