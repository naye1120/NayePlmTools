import React, { useState, useEffect, useCallback, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { Users, RefreshCw, Copy, Check, Camera } from 'lucide-react'
import { cn } from '../../utils/helpers'
import Section from '../ui/Section'
import Modal from '../ui/Modal'
import LoadingState from '../ui/LoadingState'

/* [CN-FORK] v0.5.2.99 「账号 / 远程游玩配对」板块 —— PS5 Account Tools 源码并入后的原生界面。
 *
 * 接口与交互流程逐条对齐源工程 assets/index.html（真机测过的就是那一套），载体改了三点：
 *   1. 请求打到本 payload 自己的端口（同源 /api/…），不再有第二个进程与 iframe；
 *   2. prompt/confirm 换成站内的确认弹窗与 Toast（v0.5.2.93 归一后的打断式规范）；
 *   3. 类名换成本仓库骨架层的 .btn / .field / .icon-btn，底色沿用温控页内嵌卡那一档。
 * [CN-FORK] v0.5.2.99 用户实测后的两处有意偏离源工程（都是他的明确要求，不是自创）：
 *   · 「登入」不再走「只读预览 → 手打 SIGNIN」那道网页闸，点击即执行；服务端的
 *     confirm=online 这道独立的门保留，所以真正的防误写还在，少的只是弹窗这一步。
 *   · PIN 有效期从 300 秒改 180 秒（见 src/psaccounts/accounts.c 的 PIN_TTL_SEC）。
 * [CN-FORK] v0.5.2.100 按用户指令再收三项：登出/激活这类操作前的确认弹窗全部去掉（点击即发，
 *   结果仍用 Toast 报）；板块顶部那段说明文字与「欠 N 条原值记录」那句不再显示；
 *   两个可编辑框（十六进制 ID / 账号显示名）的字色单独成一档，与只读的换算结果一眼分开。
 *
 * 卡片只绑**当前前台账号**（源工程如此，PIN 也只对前台账号生效）：要操作别的账号，
 * 先在主机上切过去再点刷新。/api/accounts 回来的 16 槽清单在这里只用来挑出那一个。 */

const BTN_PRIMARY = 'btn btn-primary'
const BTN_GHOST = 'btn btn-outline'
/* [CN-FORK] v0.5.2.102 功能分色（样张 A + I2）：「同步」是从主机重读，属信息工具类 → 青实心 */
/* [CN-FORK] v0.5.2.104 收敛档下 .btn-info 已是中性档，用户要求「同步也要有颜色」——
 * 同步的实质是「把换算后的 ID 写回注册表」（未激活时才只换算），归「写入」语义 → 金档。 */
const BTN_SYNC = 'btn btn-warn'
/* [CN-FORK] v0.5.2.104 账号状态动作分色（用户：「这两个按钮需要反应他的状态，用特殊颜色」）：
 *   建立状态（登入 / 激活）= 绿档，撤销状态（登出 / 反激活）= 红档。
 *   与上方「已登录 / 已激活」绿徽标同源呼应 —— 绿按钮 = 点了会进入绿色那个状态，
 *   红按钮 = 点了会退出当前状态，方向一眼可辨。 */
const BTN_GRANT = 'btn btn-ok'
const BTN_REVOKE = 'btn btn-danger'
const INPUT_CLS = 'field'
/* [CN-FORK] v0.5.2.100 可编辑框一档字色（.field-edit，见 index.css）：与只读换算结果（.field-ro 的
 * 辅助字色 + 虚线边）拉开明显对比，用户要求「可编辑的两个文本框字体颜色做个区分」。 */
const INPUT_EDIT = 'field field-edit'
/* 内嵌卡底：与同页温控的 FanControlCard 同一档 */
const SUB_CARD = 'bg-black/20 border border-white/10 rounded-xl p-4'

/* 头像裁剪取景框边长（CSS px）。源工程 CROP_SIZE=260。 */
const CROP_SIZE = 260
/* [CN-FORK] v0.5.2.107 导出边长。原来导出的是 260×260 —— 而 PS5 头像有 64/128/260/440 四档，
 * 最大的 440 档只能由 260 放大出来，发虚。改成按 440 导出，四档全部原生清晰。
 * （取景框仍显示 260px，导出的画布按 EXPORT_SIZE/CROP_SIZE 等比放大，构图完全一致。） */
const EXPORT_SIZE = 440
/* 缩放上限 = 「刚好盖满取景框」的多少倍。 */
const MAX_ZOOM = 4
/* PIN 有效期兜底档：正常由接口的 expiresIn 给（服务端 PIN_TTL_SEC 已改 180），
 * 这里只是接口没带回该字段时不至于显示成 5 分钟。 */
const PIN_TTL = 180

/* 取数与解码分离：接口偶尔回非 JSON（例如会话被死进程占住时的 500 文本），
 * 这时保留原始 text 好把主机原话摊开给用户，而不是抛异常吞掉。
 * [CN-FORK] v0.5.2.107 加可选 opts：传 { signal } / { timeoutMs } 时改走 fetchWithTimeout，
 * 让「应用到当前账号」这一步也能被关窗中断（abort 会以 AbortError 抛出，交给调用方判静默）。 */
async function jget(url, opts = {}) {
  const { timeoutMs, signal, ...rest } = opts
  try {
    const init = { cache: 'no-store', ...rest, signal }
    const r = (timeoutMs || signal)
      ? await fetchWithTimeout(url, init, timeoutMs || 20000)
      : await fetch(url, init)
    const t = await r.text()
    try { return { ok: r.ok, json: JSON.parse(t), text: t } } catch { return { ok: r.ok, text: t } }
  } catch (e) {
    /* 调用方自己 abort 的（用户关窗）要原样抛出去，让上层静默收场；
     * 否则会被降级成一句「接口不可达」，关个窗还弹红条。 */
    if (signal && signal.aborted) throw e
    return { ok: false, text: String((e && e.message) || e) }
  }
}

const formatPin = (s) => String(s || '').slice(0, 4) + ' ' + String(s || '').slice(4)
const fmtTime = (sec) => {
  const m = Math.floor(sec / 60)
  const r = sec % 60
  return (m < 10 ? '0' + m : m) + ':' + (r < 10 ? '0' + r : r)
}

/* 十六进制 → Base64（Chiaki 用小端字节序）/ 十进制（大端 u64），纯前端换算。 */
function hexToIds(value) {
  const raw = String(value == null ? '' : value).trim().toLowerCase().replace(/^0x/, '')
  if (!/^[0-9a-f]{16}$/.test(raw)) return null
  const be = []
  for (let i = 0; i < 8; i++) be.push(parseInt(raw.slice(i * 2, i * 2 + 2), 16))
  let bin = ''
  be.slice().reverse().forEach((b) => { bin += String.fromCharCode(b) })
  let dec = ''
  try { dec = BigInt('0x' + raw).toString(10) } catch { dec = '' }
  return { raw, b64: typeof btoa === 'function' ? btoa(bin) : '', dec }
}

/* PS5 内置浏览器没有安全的 clipboard（http 站点），原工程就是 clipboard + execCommand
   两条路；照搬，否则实机上「复制」是个死按钮。 */
function fallbackCopy(text) {
  const ta = document.createElement('textarea')
  ta.value = text
  ta.style.position = 'fixed'
  ta.style.opacity = '0'
  document.body.appendChild(ta)
  ta.select()
  try { document.execCommand('copy') } catch { /* 老内核不支持就算了 */ }
  document.body.removeChild(ta)
}

function copyToClipboard(text) {
  if (!text) return Promise.resolve(false)
  if (navigator.clipboard && navigator.clipboard.writeText) {
    return navigator.clipboard.writeText(text).then(
      () => true,
      () => { fallbackCopy(text); return true }
    )
  }
  fallbackCopy(text)
  return Promise.resolve(true)
}

/* [CN-FORK] v0.5.2.107 canvas → PNG Blob。
 * 老内核的 canvas.toBlob 偶发**不回调**，而旧代码的整个上传流程都写在这个回调里 ——
 * 一不回调，cropBusy 就永远为 true，弹窗 dismissible=false，用户被锁死在里面（「上传卡死」）。
 * 所以给它一个 4 秒兜底：超时就退回 toDataURL 手工转 Blob；两者都失败才 resolve(null)。 */
function canvasToPngBlob(canvas) {
  return new Promise((resolve) => {
    const fromDataUrl = () => {
      try {
        const dataUrl = canvas.toDataURL('image/png')
        const bin = atob(String(dataUrl).split(',')[1] || '')
        const u8 = new Uint8Array(bin.length)
        for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i)
        resolve(new Blob([u8], { type: 'image/png' }))
      } catch { resolve(null) }
    }
    if (typeof canvas.toBlob !== 'function') { fromDataUrl(); return }
    let done = false
    const timer = setTimeout(() => { if (!done) { done = true; fromDataUrl() } }, 4000)
    canvas.toBlob((b) => { if (!done) { done = true; clearTimeout(timer); resolve(b || null) } }, 'image/png')
  })
}

/* [CN-FORK] v0.5.2.107 fetch + 超时。
 * 旧代码两条 fetch（上传、应用到当前账号）都没有超时，主机侧一停就永远挂住，
 * 而 cropBusy 期间弹窗是锁死的 —— 这就是「上传卡死」的第二半。
 * 老内核没有 AbortSignal.timeout，用 AbortController 手工接；外部 signal（用户关窗）与
 * 内部超时任一触发都会 abort。 */
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

/* ── 头像裁剪弹窗：方形取景框 + 拖拽 + 缩放，确认后由调用方导出 PNG 上传 ──
 * [CN-FORK] v0.5.2.107 重写（用户实测：「头像上传、裁剪功能异常，部分图片会严重变形，上传卡死，
 *   需增加拖拽裁剪」）。四个**互相独立**的根因，逐条对应：
 *
 * ① 变形 —— 这个组件**不随弹窗卸载**（父级写的是常挂载的 `<CropModal show={crop.show} />`，
 *    `show=false` 只是 `return null`），而 size/view 是组件内 state。裁完第一张再裁第二张时，
 *    第一张的自然尺寸还留着，新图被硬拉进旧尺寸的盒子里 —— 这就是「部分图片严重变形」。
 *    更要命的是旧 src 是 FileReader 出来的 data: URL，加载是**同步**完成的，React 的 onLoad
 *    常常根本不触发，旧尺寸便一直没机会被纠正。
 *    对策：src/show 一变就清空全部 state；改用**脱离文档的 new Image()**（先挂 onload 再赋 src）
 *    + `img.complete` 兜底来取自然尺寸；**取到尺寸之前不渲染 <img>**，从根上杜绝拿旧尺寸布局新图。
 *
 * ② 卡死 —— cropBusy 期间弹窗 `dismissible={!busy}`：叉、遮罩、Esc、取消键全部锁死，
 *    而上传 / 应用到账号两条 fetch 都没有超时。主机侧一停，用户就被永久关在弹窗里。
 *    对策：弹窗**恒可关闭**、取消键恒可用；关窗即 abort 在途请求；两条请求各带超时。
 *
 * ③ 卡死（第二处，性能）—— 旧代码用 `drawImage(img, dx, dy, dw, dh)` 五参数式，把**整张源图**
 *    缩放到目标矩形。一张 6000×4000 的照片即使只露出一角，也要整张光栅化，老内核上就是几秒到
 *    几十秒的假死。对策：改九参数式，只取取景框对应的那块源矩形 —— 源矩形尺寸受 CROP_SIZE/scale
 *    约束，与源图多大无关。
 *
 * ④ 拖不动 —— 旧代码只挂了 Pointer Events。老内核（PS5 的 WebKit）对 pointer* 支持不完整就完全
 *    拖不动。对策：pointer / mouse / touch 三套都接（有 PointerEvent 就走 pointer，否则 mouse+touch，
 *    避免双份），统一进同一个 move 处理器；并加**边界夹取**（图片恒盖满取景框，拖不出白边）、
 *    滚轮缩放、双指捏合。
 *
 * 另两处顺带改掉：初始适配从 contain 改成 **cover**（短边铺满、长边溢出）—— contain 会在方框里
 * 留白边，导出的头像会带上透明条，这不是头像该有的取景方式；导出边长从 260 提到 440，覆盖 PS5
 * 头像四档里最大的一档（原来 440 档是 260 放大出来的糊图）。 */
function CropModal({ show, src, onClose, onConfirm, busy, t }) {
  const [nat, setNat] = useState(null)              // {w,h} 源图自然尺寸；null = 还没解码好
  const [view, setView] = useState({ tx: 0, ty: 0, scale: 1 })
  const [minScale, setMinScale] = useState(1)
  const [failed, setFailed] = useState('')
  const [note, setNote] = useState('')

  const imgRef = useRef(null)
  const boxRef = useRef(null)
  const natRef = useRef(null)      // 与 nat 同步的 ref：事件处理器里要读最新值，不能靠闭包
  const viewRef = useRef(view)
  const dragRef = useRef(null)
  const pinchRef = useRef(null)

  /* ref 只在 effect 里同步（render 期写 ref 会触发 react-hooks/refs）。
   * 读这些 ref 的全是**事件处理器**，它们必定在 effect 冲刷之后才跑，所以没有陈旧值问题。 */
  useEffect(() => { viewRef.current = view }, [view])

  /* 有 PointerEvent 就走 pointer 一套，否则 mouse + touch（避免 pointer 与 mouse 双份处理） */
  const HAS_PE = typeof window !== 'undefined' && 'PointerEvent' in window

  /* 位移夹取：图片必须恒盖满整个取景框 —— 不露底，也不让用户把图拖到框外 */
  const clampView = useCallback((v, n) => {
    if (!n) return v
    const dw = n.w * v.scale
    const dh = n.h * v.scale
    return {
      ...v,
      tx: Math.min(0, Math.max(CROP_SIZE - dw, v.tx)),
      ty: Math.min(0, Math.max(CROP_SIZE - dh, v.ty)),
    }
  }, [])

  /* 换图重置（变形的根因，见 ①）——用**父级 key={src} 强制重挂**来做，而不是在 effect 里
   * setState：后者会多渲染一轮，且 react-hooks/set-state-in-effect 会报错。重挂等于天然
   * 把 nat / view / minScale / failed / note 全部归零，比手写清理更不容易漏。 */

  /* 取自然尺寸。用脱离文档的 new Image()：先挂 onload 再赋 src，这样即便加载是同步完成的
     （data: URL）也不会漏掉回调；再补一次 complete 检查兜底。 */
  useEffect(() => {
    if (!show || !src) return undefined
    let alive = true
    const apply = (w, h) => {
      if (!alive || !w || !h) return
      const n = { w, h }
      natRef.current = n
      setNat(n)
      /* cover：短边铺满取景框、长边溢出（contain 会留白边，导出后是透明条） */
      const s = Math.max(CROP_SIZE / w, CROP_SIZE / h)
      setMinScale(s)
      setView(clampView({ scale: s, tx: (CROP_SIZE - w * s) / 2, ty: (CROP_SIZE - h * s) / 2 }, n))
    }
    const probe = new Image()
    probe.onload = () => apply(probe.naturalWidth, probe.naturalHeight)
    probe.onerror = () => { if (alive) setFailed(t('acctools.crop_load_fail', '这张图读不出来（格式不支持或文件已损坏）')) }
    probe.src = src
    if (probe.complete && probe.naturalWidth) apply(probe.naturalWidth, probe.naturalHeight)
    return () => { alive = false }
  }, [show, src, clampView, t])

  /* 缩放：以取景框中心为锚点，把「框中心对应的那个图上点」钉住 */
  const zoomTo = useCallback((next) => {
    const n = natRef.current
    if (!n) return
    setView((v) => {
      const s = Math.min(minScale * MAX_ZOOM, Math.max(minScale, next))
      if (s === v.scale) return v
      const cx = CROP_SIZE / 2
      const cy = CROP_SIZE / 2
      const relCx = (v.tx + (n.w * v.scale) / 2) - cx
      const relCy = (v.ty + (n.h * v.scale) / 2) - cy
      const k = s / v.scale
      return clampView({
        scale: s,
        tx: cx + relCx * k - (n.w * s) / 2,
        ty: cy + relCy * k - (n.h * s) / 2,
      }, n)
    })
  }, [clampView, minScale])

  const startDrag = useCallback((cx, cy) => {
    dragRef.current = { sx: cx, sy: cy, tx: viewRef.current.tx, ty: viewRef.current.ty }
  }, [])

  const moveDrag = useCallback((cx, cy) => {
    const d = dragRef.current
    const n = natRef.current
    if (!d || !n) return
    setView((v) => clampView({ ...v, tx: d.tx + (cx - d.sx), ty: d.ty + (cy - d.sy) }, n))
  }, [clampView])

  const endDrag = useCallback(() => { dragRef.current = null; pinchRef.current = null }, [])

  /* ④ window 上接 move/up，保证指针移出取景框后也跟得住 */
  useEffect(() => {
    if (!show) return undefined
    const onMove = (e) => { if (dragRef.current) moveDrag(e.clientX, e.clientY) }
    const onUp = () => { dragRef.current = null }
    window.addEventListener('pointermove', onMove)
    window.addEventListener('pointerup', onUp)
    window.addEventListener('pointercancel', onUp)
    if (!HAS_PE) {
      window.addEventListener('mousemove', onMove)
      window.addEventListener('mouseup', onUp)
    }
    return () => {
      window.removeEventListener('pointermove', onMove)
      window.removeEventListener('pointerup', onUp)
      window.removeEventListener('pointercancel', onUp)
      window.removeEventListener('mousemove', onMove)
      window.removeEventListener('mouseup', onUp)
    }
  }, [show, moveDrag, HAS_PE])

  /* 滚轮缩放：要 non-passive 才能 preventDefault（否则弹窗内容会跟着一起滚），
     所以用原生监听而不是 React 的 onWheel。 */
  useEffect(() => {
    const el = boxRef.current
    if (!show || !el) return undefined
    const onWheel = (e) => {
      e.preventDefault()
      zoomTo(viewRef.current.scale * (e.deltaY < 0 ? 1.12 : 1 / 1.12))
    }
    el.addEventListener('wheel', onWheel, { passive: false })
    return () => el.removeEventListener('wheel', onWheel)
  }, [show, zoomTo])

  const touchDist = (touches) => Math.hypot(
    touches[0].clientX - touches[1].clientX,
    touches[0].clientY - touches[1].clientY,
  )

  const onPointerDown = (e) => {
    if (e.pointerType === 'touch') return    // 触屏交给下面的 touch 分支，避免双份
    startDrag(e.clientX, e.clientY)
  }
  const onMouseDown = (e) => { e.preventDefault(); startDrag(e.clientX, e.clientY) }
  const onTouchStart = (e) => {
    if (e.touches.length >= 2) {
      dragRef.current = null
      pinchRef.current = { d: touchDist(e.touches), scale: viewRef.current.scale }
    } else if (e.touches.length === 1) {
      pinchRef.current = null
      startDrag(e.touches[0].clientX, e.touches[0].clientY)
    }
  }
  const onTouchMove = (e) => {
    if (e.touches.length >= 2) {
      const p = pinchRef.current
      if (!p || !p.d) return
      e.preventDefault()
      zoomTo((p.scale * touchDist(e.touches)) / p.d)
      return
    }
    if (e.touches.length === 1 && dragRef.current) {
      e.preventDefault()
      moveDrag(e.touches[0].clientX, e.touches[0].clientY)
    }
  }

  const confirm = () => {
    const n = natRef.current
    const img = imgRef.current
    if (!n || !img) return
    const v = viewRef.current
    const canvas = document.createElement('canvas')
    canvas.width = EXPORT_SIZE
    canvas.height = EXPORT_SIZE
    const ctx = canvas.getContext('2d')
    if (!ctx) { setNote(t('acctools.crop_export_fail', '导出失败：浏览器不支持 canvas 2D')); return }
    try {
      /* ③ 九参数式：只取取景框对应的那块源矩形。
         屏幕 (0,0) 对应图上 (-tx/scale, -ty/scale)，取 CROP_SIZE/scale 见方 —— 与源图多大无关，
         6000×4000 的照片也只光栅化这一小块。 */
      ctx.drawImage(
        img,
        -v.tx / v.scale, -v.ty / v.scale, CROP_SIZE / v.scale, CROP_SIZE / v.scale,
        0, 0, EXPORT_SIZE, EXPORT_SIZE,
      )
    } catch (err) {
      setNote(t('acctools.crop_export_fail2', '导出失败：{{why}}', { why: String((err && err.message) || err) }))
      return
    }
    setNote('')
    onConfirm(canvas)
  }

  if (!show) return null

  const zoomRatio = nat && minScale > 0 ? view.scale / minScale : 1

  return (
    <Modal
      show={show}
      title={t('acctools.crop_title', '裁剪头像')}
      onClose={onClose}
      /* ② 恒可关闭：旧代码 busy 期间把叉 / 遮罩 / Esc / 取消键全锁死，主机一慢用户就出不来 */
      dismissible
      footer={(
        <>
          <button type="button" className={BTN_GHOST} onClick={onClose}>
            {t('common.cancel', '取消')}
          </button>
          <button type="button" className={BTN_PRIMARY} onClick={confirm} disabled={!nat || !!failed || busy}>
            {busy
              ? t('acctools.uploading', '上传中…')
              : t('acctools.crop_ok', '确认使用')}
          </button>
        </>
      )}
    >
      <div className="flex flex-col items-center">
        <div
          ref={boxRef}
          className="relative overflow-hidden bg-black/60 border border-white/15 select-none"
          style={{
            width: `${CROP_SIZE}px`,
            height: `${CROP_SIZE}px`,
            /* ★ 取景框必须让**内容盒**正好等于 CROP_SIZE。Tailwind preflight 全局开了
             * `box-sizing: border-box`，那条 1px 边框会从 260 里吃掉 2px —— 可视区只有 258×258，
             * 而 clampView / zoomTo / 导出全按 260 算，于是预览比导出每边少 1px。
             * 改成 content-box 后内容盒 == CROP_SIZE，预览与导出严格对齐。 */
            boxSizing: 'content-box',
            touchAction: 'none',
            cursor: nat ? 'grab' : 'default',
          }}
          onPointerDown={HAS_PE ? onPointerDown : undefined}
          onMouseDown={HAS_PE ? undefined : onMouseDown}
          onTouchStart={onTouchStart}
          onTouchMove={onTouchMove}
          onTouchEnd={endDrag}
        >
          {failed ? (
            <div className="flex items-center justify-center w-full h-full px-4 text-center text-[13px] text-red-400">
              {failed}
            </div>
          ) : nat ? (
            /* nat 到手之前不渲染 <img>：否则新图会先按上一张的尺寸布局一帧，就是那个「严重变形」 */
            <img
              ref={imgRef}
              src={src}
              alt=""
              draggable={false}
              style={{
                position: 'absolute',
                left: 0,
                top: 0,
                width: `${nat.w}px`,
                height: `${nat.h}px`,
                /* ★ 关键：Tailwind preflight 里有一条 `img { max-width: 100%; height: auto }`，
                 * 它会把这枚绝对定位的图**横向掐到容器宽**（260 - 2px 边框 = 258px），而高度仍按
                 * 自然高撑开 —— 3200×1200 的图于是被压成 258×1200，等比缩下来只剩 55.9px 宽。
                 * 这就是用户报的「部分图片会严重变形」的真正根因（图越宽压得越狠，方形图几乎看不出来）。
                 * 必须显式把 max-width / max-height 打回 none，inline style 才压得住 preflight。 */
                maxWidth: 'none',
                maxHeight: 'none',
                transform: `translate(${view.tx}px, ${view.ty}px) scale(${view.scale})`,
                transformOrigin: '0 0',
                willChange: 'transform',
                pointerEvents: 'none',
              }}
            />
          ) : (
            <div className="flex items-center justify-center w-full h-full text-[13px] text-zinc-500">
              {t('acctools.crop_loading', '正在解码图片…')}
            </div>
          )}
        </div>
        <div className="flex items-center w-full max-w-[260px] mt-4">
          <span className="text-[13px] text-zinc-400 mr-3 shrink-0">{t('acctools.crop_zoom', '缩放')}</span>
          {/* 滑杆走「相对倍数」(1×~4×) 而不是绝对 scale：图片一大，绝对 scale 只有 0.06 量级，
              滑杆整条区间都挤在头几像素里，根本没法调。 */}
          <input
            type="range"
            className="flex-1 accent-ps-blue"
            min={1}
            max={MAX_ZOOM}
            step={0.01}
            value={zoomRatio}
            disabled={!nat}
            onChange={(e) => zoomTo(minScale * parseFloat(e.target.value))}
          />
        </div>
        <p className="text-[12px] text-zinc-500 mt-2">
          {t('acctools.crop_hint', '拖动图片调整位置，滚轮或双指缩放；确认后按方形取景框裁剪并上传。')}
        </p>
        {note ? <p className="text-[12px] text-red-400 mt-1">{note}</p> : null}
      </div>
    </Modal>
  )
}

export default function AccountToolsSection({ addToast }) {
  const { t } = useTranslation()
  const notify = useCallback((text, type = 'success') => addToast(text, type), [addToast])

  /* ── 账号卡状态 ─────────────────────────────────────────── */
  const [acc, setAcc] = useState(null)
  const [loading, setLoading] = useState(true)
  const [bootHint, setBootHint] = useState(t('acctools.booting', '正在读取主机状态…'))
  const [hexId, setHexId] = useState('')
  const [b64Id, setB64Id] = useState('')
  const [decId, setDecId] = useState('')
  const [renameVal, setRenameVal] = useState('')
  const [busy, setBusy] = useState('') // '' | signin | signout | activate | deactivate | rename | sync | pin
  const [avatarSrc, setAvatarSrc] = useState('')
  const [avatarFailed, setAvatarFailed] = useState(false)
  const [crop, setCrop] = useState({ show: false, src: '' })
  const [cropBusy, setCropBusy] = useState(false)
  const [dragOver, setDragOver] = useState(false)   // 头像框拖入高亮（.107 拖放选图）
  /* 刚复制成功的那一格（'hex' | 'b64' | 'dec' | 'pin'）：复制钮就地变色 1.4 秒，
   * 免得只有右上角 Toast 一闪、看不出到底哪一格被复制了 */
  const [copiedKey, setCopiedKey] = useState('')
  const copyFlash = useRef(null)
  const fileRef = useRef(null)
  /* [CN-FORK] v0.5.2.107 待裁剪图的 object URL（关窗要 revoke，否则每裁一次漏一张位图）
   * 与在途上传的 AbortController（关窗即 abort，免得请求挂着把 busy 拖住）。 */
  const objectUrlRef = useRef('')
  const uploadAbort = useRef(null)

  const loadAvatar = useCallback((userId) => {
    setAvatarFailed(false)
    setAvatarSrc(`/api/avatar?id=${userId}&_=${Date.now()}`)
  }, [])

  const refreshAccount = useCallback(async () => {
    const r = await jget('/api/accounts')
    const j = r.json
    if (!j || !Array.isArray(j.accounts)) {
      setAcc(null)
      setLoading(false)
      setBootHint(t('acctools.read_fail', '读不到账号列表：') + (r.text || t('acctools.api_unreachable', '接口不可达')))
      notify(t('acctools.read_fail', '读不到账号列表：') + (r.text || t('acctools.api_unreachable', '接口不可达')), 'error')
      return
    }
    const a = j.accounts.find((x) => x.current) ||
              j.accounts.find((x) => x.userId === j.currentUserId) ||
              null
    setAcc(a)
    setLoading(false)
    if (!a) {
      setBootHint(t('acctools.no_foreground', '读不到前台账号 —— 在主机上登进一个账号后刷新页面'))
      return
    }
    setBootHint(t('acctools.synced', '已同步主机状态 · 前台账号 {{name}}', { name: a.name }))
    const raw = a.accountId || a.derivedId || ''
    setHexId(raw)
    const conv = hexToIds(raw)
    setB64Id(a.activated ? (a.accountIdB64 || '') : (conv ? conv.b64 : ''))
    setDecId(a.activated ? (a.accountIdDec || '') : (conv ? conv.dec : ''))
    setRenameVal(a.name || '')
    loadAvatar(a.userId)
  }, [loadAvatar, notify, t])

  /* ── PIN 状态机（与源工程一致：本地倒计时 + 每秒问一次进度） ── */
  const [pin, setPin] = useState({
    raw: '', total: PIN_TTL, remain: 0, waiting: false,
    status: '', statusTone: 'sub',
  })
  const cdTimer = useRef(null)
  const pollTimer = useRef(null)

  const stopPinTimers = useCallback(() => {
    if (cdTimer.current) { clearInterval(cdTimer.current); cdTimer.current = null }
    if (pollTimer.current) { clearInterval(pollTimer.current); pollTimer.current = null }
  }, [])

  const endWaiting = useCallback((status, j) => {
    stopPinTimers()
    const label = status === 'paired' ? t('acctools.pin_paired', '配对成功')
      : status === 'failed' ? t('acctools.pin_failed', '配对失败')
      : status === 'expired' ? t('acctools.pin_expired', '已过期')
      : t('acctools.pin_ended', '配对已结束')
    setPin((p) => ({
      ...p,
      waiting: false,
      status: j.message || label,
      statusTone: status === 'paired' ? 'ok' : 'err',
    }))
  }, [stopPinTimers, t])

  const pollPin = useCallback(async () => {
    const pr = await jget('/api/remoteplay/pin/status')
    const s = pr.json
    if (!s) {
      stopPinTimers()
      setPin((p) => ({ ...p, waiting: false, statusTone: 'err', status: t('acctools.pin_status_fail', '状态接口不可达：') + (pr.text || 'no json') }))
      return
    }
    if (!s.ok) {
      stopPinTimers()
      setPin((p) => ({ ...p, waiting: false, statusTone: 'err', status: s.message || t('acctools.pin_failed', '配对失败') }))
      return
    }
    if (s.status !== 'waiting') { endWaiting(s.status, s); return }
    setPin((p) => ({
      ...p,
      remain: typeof s.remaining === 'number' ? s.remaining : p.remain,
      status: s.message || p.status,
    }))
  }, [endWaiting, stopPinTimers, t])

  const startWaiting = useCallback((j) => {
    stopPinTimers()
    const total = typeof j.expiresIn === 'number' ? j.expiresIn : PIN_TTL
    const remain = typeof j.remaining === 'number' ? j.remaining : total
    setPin((p) => ({
      ...p,
      raw: j.pinRaw || p.raw,
      total,
      remain,
      waiting: true,
      statusTone: 'ok',
      status: j.message || t('acctools.pin_waiting', '状态：等待配对中，请在客户端输入 PIN + Account ID。'),
    }))
    cdTimer.current = setInterval(() => {
      setPin((p) => (p.remain > 0 ? { ...p, remain: p.remain - 1 } : p))
    }, 1000)
    pollTimer.current = setInterval(() => { pollPin() }, 1000)
  }, [pollPin, stopPinTimers, t])

  /* 进页面：先读账号，再把主机上仍在进行的配对进度接回来（刷新页面不丢 PIN） */
  useEffect(() => {
    let alive = true
    ;(async () => {
      await refreshAccount()
      if (!alive) return
      const pr = await jget('/api/remoteplay/pin/status')
      const s = pr.json
      if (s && s.ok && s.status === 'waiting' && s.pinRaw) {
        startWaiting(s)
        notify(t('acctools.pin_resumed', '接回进行中的配对：还剩 {{sec}} 秒', { sec: s.remaining || 0 }))
      }
    })()
    return () => { alive = false; stopPinTimers() }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const genPin = async () => {
    stopPinTimers()
    setBusy('pin')
    try {
      const r = await jget('/api/remoteplay/pin')
      const j = r.json
      if (!j) {
        setPin((p) => ({ ...p, waiting: false, statusTone: 'err', status: t('acctools.pin_api_fail', '接口不可达：') + (r.text || 'no json') }))
        return
      }
      if (!j.ok) {
        setPin((p) => ({ ...p, waiting: false, statusTone: 'err', status: j.message || t('acctools.pin_gen_fail', '生成失败') }))
        return
      }
      /* Chiaki 那行不在这里定格：它跟着下面 b64Id 的现值走（等价源工程的 updatePinLine），
       * 所以「同步」或手改十六进制之后，PIN 卡里显示的账号 ID 会一起跟上。 */
      startWaiting(j)
    } finally {
      setBusy('')
    }
  }

  /* Chiaki / PXPlay 要的账号 ID：激活后是服务端给的，未激活时按框里现算的值显示，
     与下方三格永远一致（源工程就是这个行为，刷新页面后也不会各说各话）。 */
  const pinIdValue = b64Id || (acc ? acc.accountIdB64 : '') || ''
  const pinAccountLine = acc
    ? t('acctools.pin_account', '账号 {{name}} · 槽位 {{slot}} · Chiaki 的账号 ID：{{id}}', {
      name: acc.name,
      slot: acc.slot || '—',
      id: pinIdValue || t('acctools.no_account_id', '（未激活，没有 account_id）'),
    })
    : t('acctools.no_foreground_short', '读不到前台账号')

  /* ── 账号动作 ───────────────────────────────────────────── */
  const onHexChange = (v) => {
    setHexId(v)
    const conv = hexToIds(v)
    if (conv) { setB64Id(conv.b64); setDecId(conv.dec) } else { setB64Id(''); setDecId('') }
  }

  /* 动作类接口一律回 {"ok":..,"message":".."}；HTTP 200 但 ok:false（例如槽位被占用、
   * 撞名被拒）也要按失败报，否则用户看到绿色 Toast 会以为写进去了。 */
  const runAction = async (url, doneLabel) => {
    const r = await jget(url)
    const rejected = r.json && r.json.ok === false
    const msg = (r.json && r.json.message) || r.text || doneLabel
    notify(msg, r.ok && !rejected ? 'success' : 'error')
    await refreshAccount()
  }

  const toggleActivation = () => {
    if (!acc) return
    setBusy('activate')
    ;(async () => {
      let url = acc.activated
        ? `/api/accounts/deactivate?user=${acc.userId}`
        : `/api/accounts/activate?user=${acc.userId}`
      if (!acc.activated) {
        /* 未激活：框里编辑过的 ID 就是要写进去的；空/不合法/与自动值相同则交给服务端生成 */
        const v = hexId.trim()
        if (/^0x[0-9a-fA-F]{16}$/.test(v) && v.toLowerCase() !== String(acc.derivedId || '').toLowerCase()) {
          url += `&accountId=${encodeURIComponent(v)}`
        } else if (acc.derivedId) {
          notify(t('acctools.will_generate', '将自动生成：{{id}}', { id: acc.derivedId }))
        }
      }
      await runAction(url, t('acctools.action_fail', '操作失败'))
      setBusy('')
    })()
  }

  /* [CN-FORK] v0.5.2.107 用户口径（定稿）：「登出 就是只登出，反激活 即 登出并反激活，
   * 但是按钮文字不要改。」根因在服务端：原来的登出走 `safety_restore_scope(uid)`，
   * **按 uid 整段回滚**，而激活写的那 3 个键和登录态的 22 个键记在同一个 uid 名下，
   * 于是一起被退了回去 —— 这就是「登出以后自动反激活」。现在服务端按**来源**再筛一层，
   * 网页侧两条路一一对应：
   *   登出（signout）        → 只退 SIGNIN + 未标注的老记录。账号停在「已激活、未登录」，
   *                            下次点「登入」即可，**不用重新激活**；
   *   反激活（signout-full）→ 不分来源全退，回到未激活。已登录时它天然等于「登出 + 反激活」
   *                            （会话先收掉、激活再退掉），所以这一个按钮就同时干两件事。
   * 界面文字按用户要求**保持「登出」/「反激活」两个词不变**，不出现「登出并反激活」。
   * 已登录时不再单独提供「只反激活」——那会产生不一致状态（未激活但仍登录着，
   * 再点登入会被服务端以「not activated」拒掉）。 */
  const doSignout = () => {
    if (!acc) return
    setBusy('signout')
    ;(async () => {
      await runAction(`/api/accounts/signout?user=${acc.userId}`, t('acctools.signout_fail', '登出失败'))
      setBusy('')
    })()
  }

  const doSignoutFull = () => {
    if (!acc) return
    setBusy('signout-full')
    ;(async () => {
      await runAction(`/api/accounts/signout-full?user=${acc.userId}`, t('acctools.signout_fail', '登出失败'))
      setBusy('')
    })()
  }

  /* [CN-FORK] v0.5.2.99 用户要求「点登入不用弹窗，直接进行操作」：网页侧的预览 + 手打口令
   * 那道闸去掉，点击即发。服务端 confirm=online 仍在（少了它接口直接 409），
   * 写注册表前的逐键抄原值也照旧，所以回滚能力没被这一步拿掉。 */
  const doSignin = () => {
    if (!acc) return
    setBusy('signin')
    ;(async () => {
      await runAction(`/api/accounts/signin?user=${acc.userId}&confirm=online`, t('acctools.signin_fail', '登入失败'))
      setBusy('')
    })()
  }

  const applyRename = () => {
    if (!acc) return
    const name = renameVal.trim()
    if (!name) { notify(t('acctools.name_empty', '名称不能为空'), 'error'); return }
    if (name === acc.name) { notify(t('acctools.name_same', '名字没变')); return }
    setBusy('rename')
    ;(async () => {
      await runAction(`/api/accounts/rename?user=${acc.userId}&to=${encodeURIComponent(name)}`, t('acctools.rename_fail', '改名失败'))
      setBusy('')
    })()
  }

  /* 同步：先把 hex 换算成 Base64/十进制；与主机不一致时按这个 ID 写回注册表。
     未激活时只换算，写入留给「激活」那一步（与源工程一致）。 */
  const syncId = async () => {
    if (!acc) return
    const conv = hexToIds(hexId)
    if (!conv) { notify(t('acctools.hex_bad', '十六进制需为 16 位（不含 0x），例如 7ed3586971779f76'), 'error'); return }
    setHexId('0x' + conv.raw)
    setB64Id(conv.b64)
    setDecId(conv.dec)
    if (!acc.activated) {
      notify(t('acctools.hex_converted', '已换算；该账号未激活 —— 点「激活」时会把这个 ID 写进注册表'))
      return
    }
    if (`0x${conv.raw}` === String(acc.accountId || '').toLowerCase()) {
      notify(t('acctools.hex_same', '与主机一致，Base64 / 十进制已按它换算'))
      return
    }
    setBusy('sync')
    await runAction(`/api/accounts/activate?user=${acc.userId}&accountId=0x${conv.raw}`, t('acctools.sync_fail', '写回失败'))
    setBusy('')
  }

  /* ── 头像：选图 → 裁剪 → 上传 → 应用到当前账号 ─────────── */
  const pickAvatar = () => {
    if (!acc) { notify(t('acctools.no_foreground_short', '读不到前台账号'), 'error'); return }
    if (fileRef.current) fileRef.current.click()
  }

  /* 关窗统一走这里：释放 object URL、掐掉在途上传，再收起弹窗。
   * [CN-FORK] v0.5.2.107 之前 onClose 只写 setCrop({show:false}) —— 请求还挂着、
   * cropBusy 还 true、object URL 还占着内存。 */
  const closeCrop = useCallback(() => {
    if (objectUrlRef.current) {
      URL.revokeObjectURL(objectUrlRef.current)
      objectUrlRef.current = ''
    }
    if (uploadAbort.current) {
      /* [CN-FORK] v0.5.2.109 打标「是用户主动关的窗」，再 abort。
       * 不打标的话，uploadCropped 的 catch 分不清「用户取消」和「我们自己那 30 秒超时」
       * —— 两者都是 AbortError，旧代码一律静默 return，于是超时在界面上表现为
       * 「一直显示上传中、不报错也不结束」（实机就是这么被误判成卡死的）。 */
      uploadAbort.current.userCancelled = true
      uploadAbort.current.abort()
      uploadAbort.current = null
    }
    setCropBusy(false)
    setCrop({ show: false, src: '' })
  }, [])

  /* [CN-FORK] v0.5.2.107 选图统一入口：点选（<input type=file>）与拖放（drop）都走这里。
   * 用 object URL 替代 readAsDataURL —— 后者要把整张图 base64 化（体积 ×1.33）再整个塞进
   * state，手机拍的十几 MB 原图足以把 PS5 老内核卡住，这是「上传卡死」的第一个来源。
   * object URL 只给浏览器一个内存指针，零拷贝、瞬时。 */
  const openCrop = useCallback((file) => {
    if (!file) return
    if (file.type && !/^image\//.test(file.type)) {
      notify(t('acctools.not_image', '请选择图片文件（PNG / JPG / WebP）'), 'error')
      return
    }
    if (objectUrlRef.current) URL.revokeObjectURL(objectUrlRef.current)
    const url = URL.createObjectURL(file)
    objectUrlRef.current = url
    setCrop({ show: true, src: url })
  }, [notify, t])

  const onFile = (e) => {
    const file = e.target.files && e.target.files[0]
    e.target.value = ''            // 先清空：同一张图再选一次也得触发 change
    openCrop(file)
  }

  /* 拖放选图：PC 浏览器上比点「选文件」顺手（PS5 遥控器上用不到，但不冲突） */
  const onDrop = (e) => {
    e.preventDefault()
    setDragOver(false)
    if (!acc) { notify(t('acctools.no_foreground_short', '读不到前台账号'), 'error'); return }
    const dt = e.dataTransfer
    const file = dt && dt.files && dt.files[0]
    if (file) openCrop(file)
  }

  const uploadCropped = async (canvas) => {
    if (!acc) { closeCrop(); return }
    setCropBusy(true)
    if (uploadAbort.current) uploadAbort.current.abort()
    const ctl = new AbortController()
    uploadAbort.current = ctl
    try {
      const blob = await canvasToPngBlob(canvas)
      if (ctl.signal.aborted) return
      if (!blob) {
        notify(t('acctools.noblob', '裁剪失败：无法导出图片数据'), 'error')
        return
      }
      /* 图已是正方形，mode=crop 的居中裁剪是恒等变换。两条请求都带超时 + 可中断 */
      const up = await fetchWithTimeout(
        '/api/avatar/upload?filename=avatar.png&mode=crop',
        { method: 'POST', body: blob, signal: ctl.signal },
        30000,
      )
      const upText = await up.text()
      let uj = null
      try { uj = JSON.parse(upText) } catch { /* 保持 null */ }
      if (!uj || !uj.ok) {
        notify(t('acctools.upload_fail', '头像上传失败：{{why}}', {
          why: (uj && (uj.error || uj.message)) || `HTTP ${up.status}`,
        }), 'error')
        return
      }
      const ap = await jget('/api/avatar/apply-current-user', { signal: ctl.signal, timeoutMs: 20000 })
      const aj = ap.json || {}
      if (!aj.ok) {
        notify(t('acctools.apply_fail', '应用失败：{{why}}', { why: aj.error || ap.text || t('acctools.unknown', '未知错误') }), 'error')
        return
      }
      closeCrop()
      loadAvatar(acc.userId)
      notify(t('acctools.avatar_done', '头像已更新（写入 {{n}} 个文件 · {{who}}）', { n: aj.copied, who: aj.user || acc.name }))
    } catch (err) {
      /* [CN-FORK] v0.5.2.109 只有「用户自己关窗/点取消」才静默收场。
       * 我们自己那 30s/20s 超时触发的 abort 也长成 AbortError，必须报出来 ——
       * 否则界面上就是「一直显示上传中、不报错也不结束」，实机被当成卡死就是这个原因。 */
      if (ctl.userCancelled) return
      if (err && err.name === 'AbortError') {
        notify(t('acctools.upload_timeout', '头像上传超时（30 秒没等到主机响应）—— 主机端可能卡在图片转换或写盘，请重试'), 'error')
        return
      }
      notify(t('acctools.upload_fail', '头像上传失败：{{why}}', { why: String((err && err.message) || err) }), 'error')
    } finally {
      if (uploadAbort.current === ctl) uploadAbort.current = null
      setCropBusy(false)
    }
  }

  /* 复制并让对应那一格就地变色 1.4 秒：光靠 Toast 说不清复制了哪一格（用户实测反馈） */
  const copy = (text, label, key) => {
    if (!text) { notify(t('acctools.nothing_copy', '没有可复制的内容'), 'error'); return }
    copyToClipboard(text).then(() => {
      if (key) {
        setCopiedKey(key)
        if (copyFlash.current) clearTimeout(copyFlash.current)
        copyFlash.current = setTimeout(() => setCopiedKey(''), 1400)
      }
      notify(t('acctools.copied', '已复制{{label}}：{{text}}', { label: label ? `（${label}）` : '', text }))
    })
  }

  useEffect(() => () => { if (copyFlash.current) clearTimeout(copyFlash.current) }, [])

  /* [CN-FORK] v0.5.2.107 卸载兜底：页面切走 / 热更新时把 object URL 与在途请求一起收掉，
   * 免得留下悬空的位图引用和永不 resolve 的 fetch。 */
  useEffect(() => () => {
    if (objectUrlRef.current) { URL.revokeObjectURL(objectUrlRef.current); objectUrlRef.current = '' }
    if (uploadAbort.current) {
      /* 卸载时同样打标：组件都没了，不该再弹 Toast（见 closeCrop 里的同一处说明） */
      uploadAbort.current.userCancelled = true
      uploadAbort.current.abort()
      uploadAbort.current = null
    }
  }, [])

  const toneCls = { sub: 'text-zinc-400', ok: 'text-green-400', err: 'text-red-400' }
  const pct = pin.total > 0 ? Math.max(0, Math.min(100, (pin.remain / pin.total) * 100)) : 0
  const disabled = !acc || !!busy

  return (
    <Section
      id="account-tools"
      icon={Users}
      title={t('acctools.title', '账号 / 远程游玩配对')}
      bodyClass="space-y-4"
      actions={(
        <button
          type="button"
          onClick={refreshAccount}
          disabled={loading}
          title={t('common.refresh', '刷新')}
          aria-label={t('common.refresh', '刷新')}
          className="icon-btn"
        >
          <RefreshCw className={cn(loading && 'animate-spin')} />
        </button>
      )}
    >
      {/* [CN-FORK] v0.5.2.100 用户指令：这段板块说明文字取消（acctools.desc 词条留在语言包里，
          只是不再渲染）。要看行为差异去 HANDOVER §2.1。 */}

      {/* 源工程页顶的同步提示：账号卡与 PIN 卡都只绑前台账号，这行是当场证明「网页看到的是主机现在的状态」 */}
      <p className="text-[12px] text-zinc-500">{bootHint}</p>

      {/* ── 远程游玩配对 PIN ── */}
      <div className={SUB_CARD}>
        <div className="text-[14px] font-bold text-white">{t('acctools.pin_card', '远程游玩配对 PIN')}</div>

        <div className="mt-3 flex items-center flex-wrap">
          {pin.raw ? (
            <>
              <span className="text-3xl font-bold text-white tabular-nums tracking-wider">
                {formatPin(pin.raw)}
              </span>
              {/* [CN-FORK] v0.5.2.99 复制钮挪到 PIN 数字紧跟的后面（原来在卡片右上角，隔着一整行看不见）。
                  复制的是 8 位原始码，不带中间那个显示用的空格 —— Chiaki 就是按 8 位连续数字输的。 */}
              <button
                type="button"
                className={cn('icon-btn ml-3 shrink-0', copiedKey === 'pin' && 'is-copied')}
                title={t('acctools.copy_pin', '复制原始 PIN')}
                aria-label={t('acctools.copy_pin', '复制原始 PIN')}
                onClick={() => copy(pin.raw, t('acctools.pin_raw', '原始 PIN'), 'pin')}
              >
                {copiedKey === 'pin' ? <Check /> : <Copy />}
              </button>
              <span className="text-[13px] font-normal text-zinc-500 ml-3">
                {pin.waiting ? t('acctools.pin_waiting_short', '等待配对') : pin.status}
              </span>
            </>
          ) : null /* [CN-FORK] v0.5.2.104 用户指令：「暂无 PIN，点击下方按钮生成」这句废话不要。
                       没有 PIN 时这里什么都不显示 —— 下方那颗「生成 PIN」按钮本身就是答案，
                       再写一句「点击下方按钮生成」只是把同一个意思说两遍。 */}
        </div>

        {/* [CN-FORK] v0.5.2.99 原来是 break-all：整行在「账号 ID：」中间就被截断换行，读起来像断句；
            换成 break-words，只在空格/分隔符处折行。 */}
        <p className="text-[13px] text-zinc-400 mt-3 break-words">{pinAccountLine}</p>
        {pin.status
          ? <p className={cn('text-[13px] mt-1', toneCls[pin.statusTone] || toneCls.sub)}>{pin.status}</p>
          : <p className="text-[13px] text-zinc-600 mt-1">{t('acctools.pin_status_none', '状态：当前没有进行中的配对')}</p>}

        {pin.waiting && (
          <div className="mt-3">
            {/* [CN-FORK] v0.5.2.105 用户指令：「生成 pin 的进度条要高一些，炫酷一些，颜色可以用别的」。
                从 h-2 的纯色条换成 .pin-bar / .pin-bar-fill（定义见 index.css）：
                加厚到 --pin-bar-h（PC 14px / 电视 20px），走「青→蓝→紫」冷色渐变 + 外发光 + 一条循环扫过的流光；
                剩余 ≤30 秒自动切「琥珀→红」渐变并整体脉冲 —— 「快过期了」由颜色与动效两条路同时给。
                宽度过渡写在 .pin-bar-fill 里（transition: width 1s linear），这里只给宽度。 */}
            <div className={cn('pin-bar', pin.remain <= 30 && 'is-critical')}>
              <div className="pin-bar-fill" style={{ width: `${pct}%` }} />
            </div>
            <div className="flex items-center mt-2 text-[12px] text-zinc-500">
              <span>{t('acctools.pin_ttl', 'PIN 有效期')}</span>
              <span className="ml-auto tabular-nums">{fmtTime(pin.remain)}</span>
            </div>
          </div>
        )}

        <div className="flex items-center mt-4">
          <button type="button" className={BTN_PRIMARY} onClick={genPin} disabled={!!busy && busy !== 'pin'}>
            {busy === 'pin' ? t('acctools.pin_generating', '生成中…')
              : pin.raw ? t('acctools.pin_regenerate', '重新生成') : t('acctools.pin_generate', '生成 PIN')}
          </button>
          <span className="text-[13px] text-zinc-400 ml-3 tabular-nums">
            {pin.waiting ? t('acctools.pin_left', '剩余时间 {{time}}', { time: fmtTime(pin.remain) }) : ''}
          </span>
        </div>
        <p className="text-[12px] text-zinc-500 mt-2">
          {t('acctools.pin_hint', 'PIN 只对当前前台账号有效，与主机菜单「设定 → 系统 → 远程游玩 → 与装置配对」显示的是同一个码；约 5 分钟内有效。')}
        </p>
      </div>

      {/* ── 前台账号卡 ── */}
      <div className={SUB_CARD}>
        {loading && !acc ? (
          <LoadingState label={t('acctools.loading', '正在读取账号状态…')} />
        ) : !acc ? (
          <p className="text-[13px] text-red-400">
            {t('acctools.no_foreground_desc', 'sceUserServiceGetForegroundUser 失败或没有登录用户。在主机上登进一个账号后点右上角刷新。')}
          </p>
        ) : (
          <>
            <div className="flex items-center">
              <button
                type="button"
                onClick={pickAvatar}
                title={t('acctools.avatar_tip', '点击上传头像（也可把图片拖到这里）')}
                onDragOver={(e) => { e.preventDefault(); if (!dragOver) setDragOver(true) }}
                onDragLeave={() => setDragOver(false)}
                onDrop={onDrop}
                className={cn(
                  'group relative w-16 h-16 rounded-xl overflow-hidden bg-black/40 border shrink-0',
                  dragOver ? 'border-ps-blue ring-2 ring-ps-blue/60' : 'border-white/10 hover:border-ps-blue',
                )}
              >
                {avatarSrc && !avatarFailed ? (
                  <img src={avatarSrc} alt="" className="w-full h-full object-cover" onError={() => setAvatarFailed(true)} />
                ) : (
                  <span className="flex items-center justify-center w-full h-full text-2xl font-bold text-zinc-500">
                    {String(acc.name || '?').charAt(0).toUpperCase()}
                  </span>
                )}
                {/* [CN-FORK] v0.5.2.99 头像框底部一条相机角标：这格能点这件事不能只靠 title
                    （实机浏览器与遥控器都不弹 title），常驻可见才认得出来 */}
                <span className="absolute inset-x-0 bottom-0 h-[18px] bg-black/65 flex items-center justify-center">
                  <Camera className="w-3.5 h-3.5 text-white group-hover:text-ps-blue" />
                </span>
              </button>

              <div className="ml-4 flex-1 min-w-0">
                <div className="flex items-center min-w-0">
                  <span className="text-[17px] font-bold text-white truncate min-w-0">{acc.name || '?'}</span>
                  <span className="text-[12px] text-zinc-500 ml-2 shrink-0 tabular-nums">
                    {acc.userIdHex} · {t('acctools.slot', '槽位')} {acc.slot || '—'}
                  </span>
                </div>
                <div className="flex items-center flex-wrap mt-2">
                  <span className={cn('text-[12px] px-2 py-0.5 rounded-full mr-2',
                    acc.activated ? 'bg-green-500/15 text-green-400' : 'bg-white/5 text-zinc-400')}>
                    {acc.activated ? t('acctools.activated', '已激活') : t('acctools.not_activated', '未激活')}
                  </span>
                  <span className={cn('text-[12px] px-2 py-0.5 rounded-full mr-2',
                    acc.signedIn ? 'bg-ps-blue/15 text-ps-blue' : 'bg-white/5 text-zinc-400')}>
                    {acc.signedIn ? t('acctools.signed_in', '已登录') : t('acctools.not_signed_in', '未登录')}
                  </span>
                  {acc.autoLogin && (
                    /* [CN-FORK] v0.5.2.99 自动登录单独给一档鲜色（暖黄）：它不是「状态」而是
                       「这台主机会自己登进去」的提示，混在旁边绿/橙的状态芯片里看不出来。
                       bg-amber-500/20 由 index.css 收口层翻成 --color-warn-tint，一档不新增 */
                    <span className="text-[12px] px-2 py-0.5 rounded-full mr-2 bg-amber-500/20 text-amber-400 font-bold">
                      ★ {t('acctools.auto_login', '自动登录')}
                    </span>
                  )}
                  <span className="ml-auto flex items-center">
                    <button
                      type="button"
                      /* [CN-FORK] v0.5.2.104 颜色跟着状态走：已登录 → 登出（红档·撤销）；未登录 → 登入（绿档·建立） */
                      className={cn(acc.signedIn ? BTN_REVOKE : BTN_GRANT, 'mr-2')}
                      onClick={acc.signedIn ? doSignout : doSignin}
                      disabled={disabled}
                      title={acc.signedIn
                        ? t('acctools.signout_tip', '退出登录；账号保持已激活，下次可直接登入')
                        : t('acctools.signin_tip', '把账号登入到系统')}
                    >
                      {busy === 'signin' || busy === 'signout'
                        ? t('acctools.working', '处理中…')
                        : acc.signedIn ? t('acctools.signout', '登出') : t('acctools.signin', '登入')}
                    </button>
                    <button
                      type="button"
                      /* [CN-FORK] v0.5.2.104 用户口径：「登入 和激活 是绿档，登出 是红档，反激活是灰档」。
                          所以撤销类里只有「登出」吃红档，这一格回中性档 —— 它份量低于登出，
                          不该跟它抢同一档红。
                         [CN-FORK] v0.5.2.107 用户口径：「登出 就是只登出，反激活 即 登出并反激活，
                          但是按钮文字不要改」。所以：
                            · 已登录时这一格 = 「反激活」，点下去走 signout-full（登出 + 反激活一步到位）；
                            · 未登录时这一格 = 「反激活」，点下去走 deactivate。
                          两种情况**文字一样**（都叫「反激活」），只是已登录时多带一步退登录。 */
                      className={acc.activated || acc.signedIn ? BTN_GHOST : BTN_GRANT}
                      onClick={acc.signedIn ? doSignoutFull : toggleActivation}
                      disabled={disabled}
                      title={acc.signedIn
                        ? t('acctools.signout_full_tip', '退出登录并清掉离线激活 —— 已登录时「反激活」就是这两步一起做')
                        : acc.activated
                          ? t('acctools.deactivate_tip', '清掉离线激活，账号回到未激活')
                          : t('acctools.activate_tip', '写入离线激活信息，之后才能登入')}
                    >
                      {busy === 'activate' || busy === 'deactivate' || busy === 'signout-full'
                        ? t('acctools.working', '处理中…')
                        : (acc.activated || acc.signedIn)
                          ? t('acctools.deactivate', '反激活')
                          : t('acctools.activate', '激活')}
                    </button>
                  </span>
                </div>
              </div>
            </div>

            <div className="grid grid-cols-1 sm:grid-cols-3 gap-3 mt-4">
              <div>
                <div className="text-[12px] text-zinc-400 mb-1.5">
                  {t('acctools.id_hex', '账号 ID（十六进制）· 可编辑')}
                </div>
                <div className="flex items-center">
                  {/* [CN-FORK] v0.5.2.99 复制钮嵌进框内右端（原来吊在框外，三格三钮看不出谁是谁的），
                      点一下钮本身变动作色 + 图标换成勾，1.4 秒后复位 */}
                  <div className="relative flex-1 min-w-0">
                    <input
                      className={cn(INPUT_EDIT, 'field-btn-pad')}
                      value={hexId}
                      spellCheck={false}
                      onChange={(e) => onHexChange(e.target.value)}
                      onKeyDown={(e) => { if (e.key === 'Enter') syncId() }}
                    />
                    <button type="button" className={cn('field-btn', copiedKey === 'hex' && 'is-copied')}
                      title={t('acctools.copy', '复制')}
                      aria-label={t('acctools.copy', '复制')} onClick={() => copy(hexId, '', 'hex')}>
                      {copiedKey === 'hex' ? <Check className="w-4 h-4" /> : <Copy className="w-4 h-4" />}
                    </button>
                  </div>
                  {/* [CN-FORK] v0.5.2.105 用户指令：「同步按钮也加个小图标，跟修改按钮一样大，一样的风格」。
                      照「修改」那颗的做法：图标在前（w-4 h-4）+ 文字，间距由 .btn > * + * 给（老内核 gap 不生效）；
                      忙时文案切「处理中…」与「修改」一致，图标留着不换（与 Check 一样是静态图标）。 */}
                  <button type="button" className={cn(BTN_SYNC, 'ml-2 shrink-0')} onClick={syncId} disabled={disabled}>
                    <RefreshCw className="w-4 h-4" />
                    {busy === 'sync' ? t('acctools.working', '处理中…') : t('acctools.sync', '同步')}
                  </button>
                </div>
              </div>
              {[[t('acctools.id_b64', 'Chiaki 格式（Base64）'), b64Id, 'b64'],
                [t('acctools.id_dec', 'PXPlay 格式（十进制）'), decId, 'dec']].map(([label, val, key]) => (
                <div key={label}>
                  <div className="text-[12px] text-zinc-400 mb-1.5">{label}</div>
                  <div className="relative">
                    {/* .field-ro：换算结果只读，虚线边 + 辅助字色，与左边可编辑那格分开 */}
                    <input className={cn(INPUT_CLS, 'field-ro field-btn-pad')} value={val} readOnly spellCheck={false} />
                    <button type="button" className={cn('field-btn', copiedKey === key && 'is-copied')}
                      title={t('acctools.copy', '复制')}
                      aria-label={t('acctools.copy', '复制')} onClick={() => copy(val, '', key)}>
                      {copiedKey === key ? <Check className="w-4 h-4" /> : <Copy className="w-4 h-4" />}
                    </button>
                  </div>
                </div>
              ))}
            </div>

            {/* [CN-FORK] v0.5.2.104 用户指令：「这个框缩短，将修改按钮和上边同步按钮对齐」。
                原来这一行是 flex-1 铺满整宽：输入框一路顶到最右，修改按钮吊在卡片右边缘，
                与上方「同步」不在同一条竖线上。现在套用与 ID 三格完全相同的那张 3 列栅格，
                字段落在第 1 列、按钮贴本列右缘 —— 右缘自然与「同步」对齐（窄屏 <640px 仍是整宽单列）。
                结构也与上面十六进制那格对齐：外层 flex + 内层 relative flex-1 min-w-0 包住输入框。 */}
            <div className="grid grid-cols-1 sm:grid-cols-3 gap-3 mt-4">
              <div>
                <div className="text-[12px] text-zinc-400 mb-1.5">
                  {t('acctools.name_field', '账号显示名 · 可编辑')}
                </div>
                <div className="flex items-center">
                  <div className="relative flex-1 min-w-0">
                    <input
                      className={INPUT_EDIT}
                      value={renameVal}
                      spellCheck={false}
                      onChange={(e) => setRenameVal(e.target.value)}
                      onKeyDown={(e) => { if (e.key === 'Enter') applyRename() }}
                    />
                  </div>
                  <button type="button" className={cn(BTN_PRIMARY, 'ml-2 shrink-0')} onClick={applyRename} disabled={disabled}>
                    <Check className="w-4 h-4" />
                    {busy === 'rename' ? t('acctools.working', '处理中…') : t('acctools.rename', '修改')}
                  </button>
                </div>
              </div>
            </div>

            {/* [CN-FORK] v0.5.2.100 「欠 N 条原值记录」那句按用户指令不再渲染（只留绑定关系这句）。 */}
            <p className="text-[12px] text-zinc-500 mt-3">
              {t('acctools.acct_hint', '本卡片绑定当前前台账号（PIN 也只对它生效）—— 要操作别的账号，先在主机上切过去再刷新。')}
            </p>
          </>
        )}
        <input ref={fileRef} type="file" accept="image/*" className="hidden" onChange={onFile} />
      </div>

      <CropModal
        /* key 绑 src：换图即重挂，杜绝上一张的自然尺寸/位移残留（「部分图片严重变形」的根因）。
           src 为空串时用 'closed' 占位，避免 key 为空。 */
        key={crop.src || 'closed'}
        show={crop.show}
        src={crop.src}
        busy={cropBusy}
        t={t}
        onClose={closeCrop}
        onConfirm={uploadCropped}
      />
    </Section>
  )
}
