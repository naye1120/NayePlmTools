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
 *
 * 卡片只绑**当前前台账号**（源工程如此，PIN 也只对前台账号生效）：要操作别的账号，
 * 先在主机上切过去再点刷新。/api/accounts 回来的 16 槽清单在这里只用来挑出那一个。 */

const BTN_PRIMARY = 'btn btn-primary'
const BTN_GHOST = 'btn btn-outline'
const INPUT_CLS = 'field'
/* 内嵌卡底：与同页温控的 FanControlCard 同一档 */
const SUB_CARD = 'bg-black/20 border border-white/10 rounded-xl p-4'

/* 头像裁剪边长：源工程 CROP_SIZE=260，正好是 PS5 头像四档里的 260。 */
const CROP_SIZE = 260
/* PIN 有效期兜底档：正常由接口的 expiresIn 给（服务端 PIN_TTL_SEC 已改 180），
 * 这里只是接口没带回该字段时不至于显示成 5 分钟。 */
const PIN_TTL = 180

/* 取数与解码分离：接口偶尔回非 JSON（例如会话被死进程占住时的 500 文本），
 * 这时保留原始 text 好把主机原话摊开给用户，而不是抛异常吞掉。 */
async function jget(url) {
  try {
    const r = await fetch(url, { cache: 'no-store' })
    const t = await r.text()
    try { return { ok: r.ok, json: JSON.parse(t), text: t } } catch { return { ok: r.ok, text: t } }
  } catch (e) {
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

/* ── 头像裁剪弹窗：方形取景框 + 拖动 + 缩放，确认后由调用方导出 PNG 上传 ──
 * 与源工程同款算法：初始 contain 适配、以框中心为原点缩放、导出 260×260 canvas。
 * 指针事件代替 mouse 事件（遥控器/触屏也拖得动），换算一模一样。 */
function CropModal({ show, src, onClose, onConfirm, busy, t }) {
  const [size, setSize] = useState({ nw: 0, nh: 0 })
  const [view, setView] = useState({ tx: 0, ty: 0, scale: 1 })
  const [minScale, setMinScale] = useState(1)
  const imgRef = useRef(null)
  const dragRef = useRef(null)

  const onImgLoad = (e) => {
    const img = e.currentTarget
    const nw = img.naturalWidth
    const nh = img.naturalHeight
    if (!nw || !nh) return
    /* contain 初始适配：整张图先塞进 260 方框，缩放下限就是这一档 */
    const s = Math.min(CROP_SIZE / nw, CROP_SIZE / nh)
    setSize({ nw, nh })
    setMinScale(s)
    setView({ scale: s, tx: (CROP_SIZE - nw * s) / 2, ty: (CROP_SIZE - nh * s) / 2 })
  }

  useEffect(() => {
    if (!show) return undefined
    const onMove = (e) => {
      const d = dragRef.current
      if (!d) return
      setView((v) => ({ ...v, tx: d.stx + (e.clientX - d.sx), ty: d.sty + (e.clientY - d.sy) }))
    }
    const onUp = () => { dragRef.current = null }
    window.addEventListener('pointermove', onMove)
    window.addEventListener('pointerup', onUp)
    return () => {
      window.removeEventListener('pointermove', onMove)
      window.removeEventListener('pointerup', onUp)
    }
  }, [show])

  const zoomTo = (newScale) => {
    setView((v) => {
      const cx = CROP_SIZE / 2
      const cy = CROP_SIZE / 2
      const relCx = (v.tx + (size.nw * v.scale) / 2) - cx
      const relCy = (v.ty + (size.nh * v.scale) / 2) - cy
      const k = newScale / v.scale
      return {
        scale: newScale,
        tx: cx + relCx * k - (size.nw * newScale) / 2,
        ty: cy + relCy * k - (size.nh * newScale) / 2,
      }
    })
  }

  const confirm = () => {
    const img = imgRef.current
    if (!img) return
    const canvas = document.createElement('canvas')
    canvas.width = CROP_SIZE
    canvas.height = CROP_SIZE
    const ctx = canvas.getContext('2d')
    ctx.drawImage(img, view.tx, view.ty, size.nw * view.scale, size.nh * view.scale)
    onConfirm(canvas)
  }

  if (!show) return null

  return (
    <Modal
      show={show}
      title={t('acctools.crop_title', '裁剪头像')}
      onClose={onClose}
      dismissible={!busy}
      footer={(
        <>
          <button type="button" className={BTN_GHOST} onClick={onClose} disabled={busy}>
            {t('common.cancel', '取消')}
          </button>
          <button type="button" className={BTN_PRIMARY} onClick={confirm} disabled={busy}>
            {busy
              ? t('acctools.uploading', '上传中…')
              : t('acctools.crop_ok', '确认使用')}
          </button>
        </>
      )}
    >
      <div className="flex flex-col items-center">
        <div
          className="relative overflow-hidden bg-black/60 border border-white/15"
          style={{ width: `${CROP_SIZE}px`, height: `${CROP_SIZE}px`, touchAction: 'none' }}
          onPointerDown={(e) => { dragRef.current = { sx: e.clientX, sy: e.clientY, stx: view.tx, sty: view.ty } }}
        >
          <img
            ref={imgRef}
            src={src}
            alt=""
            draggable={false}
            onLoad={onImgLoad}
            style={{
              position: 'absolute',
              left: 0,
              top: 0,
              width: `${size.nw}px`,
              height: `${size.nh}px`,
              transform: `translate(${view.tx}px, ${view.ty}px) scale(${view.scale})`,
              transformOrigin: '0 0',
            }}
          />
        </div>
        <div className="flex items-center w-full max-w-[260px] mt-4">
          <span className="text-[13px] text-zinc-400 mr-3 shrink-0">{t('acctools.crop_zoom', '缩放')}</span>
          <input
            type="range"
            className="flex-1 accent-ps-blue"
            min={minScale}
            max={4}
            step={0.01}
            value={view.scale}
            onChange={(e) => zoomTo(parseFloat(e.target.value))}
          />
        </div>
        <p className="text-[12px] text-zinc-500 mt-2">{t('acctools.crop_hint', '拖动图片调整位置，确认后按 260×260 上传。')}</p>
      </div>
    </Modal>
  )
}

export default function AccountToolsSection({ showConfirm, addToast }) {
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
  /* 刚复制成功的那一格（'hex' | 'b64' | 'dec' | 'pin'）：复制钮就地变色 1.4 秒，
   * 免得只有右上角 Toast 一闪、看不出到底哪一格被复制了 */
  const [copiedKey, setCopiedKey] = useState('')
  const copyFlash = useRef(null)
  const fileRef = useRef(null)

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

  const doSignout = () => {
    if (!acc) return
    const pend = acc.pendingRecords || 0
    const slotTxt = acc.slot ? t('acctools.slot_of', '（槽 {{slot}}）', { slot: acc.slot }) : ''
    const head = pend
      ? t('acctools.signout_head_pend', '登出 {{name}}：只回滚它名下 {{n}} 条原值记录{{slot}}，其他账号的待还原记录不动。', { name: acc.name, n: pend, slot: slotTxt })
      : t('acctools.signout_head_none', '登出 {{name}}{{slot}}：它名下没有本工具的记录，说明这次登录不是我们写的。将按实测基线写回「已激活未登录」，并把 auth.dat / config.dat 抄进备份层后拿掉（写前逐键抄原值，还原时能放回来）。', { name: acc.name, slot: slotTxt })
    showConfirm(t('acctools.signout', '登出'), `${head}\n\n${t('acctools.confirm_q', '确认？')}`, () => {
      setBusy('signout')
      ;(async () => {
        await runAction(`/api/accounts/signout?user=${acc.userId}`, t('acctools.signout_fail', '登出失败'))
        setBusy('')
      })()
    }, true)
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

  const onFile = (e) => {
    const file = e.target.files && e.target.files[0]
    if (!file) return
    const reader = new FileReader()
    reader.onload = (ev) => setCrop({ show: true, src: ev.target.result })
    reader.readAsDataURL(file)
    e.target.value = ''
  }

  const uploadCropped = (canvas) => {
    if (!acc) { setCrop({ show: false, src: '' }); return }
    setCropBusy(true)
    canvas.toBlob(async (blob) => {
      try {
        if (!blob) { notify(t('acctools.noblob', '裁剪失败：浏览器不支持 toBlob'), 'error'); return }
        /* 图已是正方形，mode=crop 的居中裁剪是恒等变换 */
        const up = await fetch('/api/avatar/upload?filename=avatar.png&mode=crop', { method: 'POST', body: blob })
        const upText = await up.text()
        let uj = null
        try { uj = JSON.parse(upText) } catch { /* 保持 null */ }
        if (!uj || !uj.ok) {
          notify(t('acctools.upload_fail', '头像上传失败：{{why}}', {
            why: (uj && (uj.error || uj.message)) || `HTTP ${up.status}`,
          }), 'error')
          return
        }
        const ap = await jget('/api/avatar/apply-current-user')
        const aj = ap.json || {}
        if (!aj.ok) {
          notify(t('acctools.apply_fail', '应用失败：{{why}}', { why: aj.error || ap.text || t('acctools.unknown', '未知错误') }), 'error')
          return
        }
        setCrop({ show: false, src: '' })
        loadAvatar(acc.userId)
        notify(t('acctools.avatar_done', '头像已更新（写入 {{n}} 个文件 · {{who}}）', { n: aj.copied, who: aj.user || acc.name }))
      } catch (err) {
        notify(t('acctools.upload_fail', '头像上传失败：{{why}}', { why: String((err && err.message) || err) }), 'error')
      } finally {
        setCropBusy(false)
      }
    }, 'image/png')
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
      <p className="text-[14px] text-zinc-400 max-w-2xl">
        {t('acctools.desc', '前台账号的离线激活 / 登录状态、账号 ID 三种格式与远程游玩配对 PIN。写注册表前逐键抄原值，点「登出」就按这批记录回滚。')}
      </p>

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
          ) : (
            <span className="text-[15px] text-zinc-500">{t('acctools.pin_none', '暂无 PIN，点击下方按钮生成')}</span>
          )}
        </div>

        {/* [CN-FORK] v0.5.2.99 原来是 break-all：整行在「账号 ID：」中间就被截断换行，读起来像断句；
            换成 break-words，只在空格/分隔符处折行。 */}
        <p className="text-[13px] text-zinc-400 mt-3 break-words">{pinAccountLine}</p>
        {pin.status
          ? <p className={cn('text-[13px] mt-1', toneCls[pin.statusTone] || toneCls.sub)}>{pin.status}</p>
          : <p className="text-[13px] text-zinc-600 mt-1">{t('acctools.pin_status_none', '状态：当前没有进行中的配对')}</p>}

        {pin.waiting && (
          <div className="mt-3">
            <div className="h-2 rounded-full bg-black/40 overflow-hidden">
              <div
                className={cn('h-full rounded-full', pin.remain <= 30 ? 'bg-red-400' : 'bg-ps-blue')}
                style={{ width: `${pct}%`, transition: 'width 1s linear' }}
              />
            </div>
            <div className="flex items-center mt-1.5 text-[12px] text-zinc-500">
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
                title={t('acctools.avatar_tip', '点击上传头像')}
                className="group relative w-16 h-16 rounded-xl overflow-hidden bg-black/40 border border-white/10 shrink-0 hover:border-ps-blue"
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
                      className={cn(BTN_GHOST, 'mr-2')}
                      onClick={acc.signedIn ? doSignout : doSignin}
                      disabled={disabled}
                    >
                      {busy === 'signin' || busy === 'signout'
                        ? t('acctools.working', '处理中…')
                        : acc.signedIn ? t('acctools.signout', '登出') : t('acctools.signin', '登入')}
                    </button>
                    <button
                      type="button"
                      className={BTN_GHOST}
                      onClick={toggleActivation}
                      disabled={disabled}
                    >
                      {busy === 'activate' || busy === 'deactivate'
                        ? t('acctools.working', '处理中…')
                        : acc.activated ? t('acctools.deactivate', '反激活') : t('acctools.activate', '激活')}
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
                      className={cn(INPUT_CLS, 'field-btn-pad')}
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
                  <button type="button" className={cn(BTN_GHOST, 'ml-2 shrink-0')} onClick={syncId} disabled={disabled}>
                    {t('acctools.sync', '同步')}
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

            <div className="flex items-center mt-4">
              <div className="flex-1 min-w-0">
                <div className="text-[12px] text-zinc-400 mb-1.5">
                  {t('acctools.name_field', '账号显示名 · 可编辑')}
                </div>
                <input
                  className={INPUT_CLS}
                  value={renameVal}
                  spellCheck={false}
                  onChange={(e) => setRenameVal(e.target.value)}
                  onKeyDown={(e) => { if (e.key === 'Enter') applyRename() }}
                />
              </div>
              <button type="button" className={cn(BTN_PRIMARY, 'ml-2 shrink-0 mt-6')} onClick={applyRename} disabled={disabled}>
                <Check className="w-4 h-4" />
                {busy === 'rename' ? t('acctools.working', '处理中…') : t('acctools.rename', '修改')}
              </button>
            </div>

            <p className="text-[12px] text-zinc-500 mt-3">
              {t('acctools.acct_hint', '本卡片绑定当前前台账号（PIN 也只对它生效）—— 要操作别的账号，先在主机上切过去再刷新。')}
              {acc.pendingRecords
                ? t('acctools.pending_records', ' 本工具欠它 {{n}} 条原值记录。', { n: acc.pendingRecords })
                : ''}
            </p>
          </>
        )}
        <input ref={fileRef} type="file" accept="image/*" className="hidden" onChange={onFile} />
      </div>

      <CropModal
        show={crop.show}
        src={crop.src}
        busy={cropBusy}
        t={t}
        onClose={() => setCrop({ show: false, src: '' })}
        onConfirm={uploadCropped}
      />
    </Section>
  )
}
