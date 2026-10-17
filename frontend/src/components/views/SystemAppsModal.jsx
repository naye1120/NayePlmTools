import React, { useCallback, useEffect, useRef, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { AppWindow, AlertTriangle, Image as ImageIcon, ImagePlus, RotateCcw } from 'lucide-react'
import Modal from '../ui/Modal'
import Switch from '../ui/Switch'
import { cn } from '../../utils/helpers'
import { Chip, MiniEmpty, Note } from '../game/parts'
import {
  getSystemApps, setSystemAppHidden, setSystemAppPinned,
  appIconUrl, appTileBackgroundUrl,
  setAppTileIcon, resetAppTileIcon,
  setAppTileBackground, resetAppTileBackground,
} from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.158 「个性化软件」弹窗 —— 主机内置软件在主屏上的显示 / 隐藏。
 *
 * 一行一个软件，右侧一个**隐藏开关**：开 = 从主屏藏起来，关 = 显示出来。
 * 方向以主机为准（app.db 的 visible 列），本文件不做任何取反 ——
 * 前端一旦自己反一次，后端再反一次就变成「开关写着隐藏、按下去显示」。
 *
 * 状态每次打开都重新拉：真值就在主机那一列里，缓存一份只会多出「面板说藏了、
 * 主屏还画着」这类对不上账的排查。
 *
 * [CN-FORK] v0.5.2.160 后端会回 `controllable:false`（面板改不动的条目，真机实测那一列
 * 对它们不起作用）。`.160` 那版把它们单独列一组并写明原因；`.161` 按用户口径改成
 * **整组不显示**：「其他的不能编辑的选项直接就不用在前端显示了」。
 * 名单仍然留在后端而不是前端抄一份 —— 前端自己判「能不能改」迟早和后端漂。
 *
 * [CN-FORK] v0.5.2.164 每行多了第二个开关：**置顶**。
 * 与「隐藏」刻意分成两个开关，因为它们是两件事、落到两处存储。
 *
 * [CN-FORK] v0.6.0.179 本文件的第二轮（用户口径：加图标与背景 + 版式重排）：
 *
 *  ① 每行最前面是**主机自己的图标**（`/app_icon?tid=`）——有自定义回我们那份，
 *     没有回主机自带那张。图裂了退回中性占位，而且**占位与真图占同一个固定方框**：
 *     图标 404 的那一行如果变矮，整列会当场错位（一屏 19 行，错位一眼看得出来）。
 *
 *  ② 两颗**上传**按钮（图标 / 背景）放在两颗开关**前面**（用户点名：「上传背景的按钮
 *     放在 置顶 前面」）。上传的图先在浏览器里缩到目标尺寸再走 `POST /app_tile_*`：
 *       · 图标 → 1:1，512×512（内置软件在主屏上就是一格方图）；
 *       · 背景 → 16:9，1920×1080（后端要转成同尺寸的 BC1 DDS，见 gameApi.js 顶部契约）。
 *     **不做「先暂存、再点应用」那一步**（RenameModal 是那样）：那套是给一个弹窗管三层
 *     大图用的，这里一行两件事，多一次确认只会让遥控器多按一下。
 *
 *  ③ 设过自定义图的那一层给「已自定义…」徽标 + 一颗「恢复…」按钮（`*_reset` 路由）。
 *     恢复按钮**只在设过时出现**，不做第三颗常驻按钮：一屏 19 行，每行三颗常驻按钮
 *     会把真正要看的「置顶 / 隐藏」挤到看不见。
 *
 *  ④ **乐观更新只用在两颗开关上**（原来的行为，一字不改）：拨完到主机回话之间把开关
 *     先动起来，失败整行退回去。上传**不能**乐观 —— 图有没有写进去只有后端知道，
 *     先标「已自定义」再回滚，用户会看到徽标闪一下又消失，比等一会儿更糟。
 *     所以上传是「后端回话之后才改行内状态」，失败只有一句红字 + 列表原样不动。
 *
 *  ⑤ 置顶的语义变了（后端同一轮落地）：`.164` 只写本面板的 app_pins.json，主机压根不知道，
 *     用户看到的就是「置顶按钮在 PS5 里没有生效」；现在写的是**主机主屏的真·固定列表**
 *     （app.db 的 tbl_group_<uid>，MyHome 组），与游戏磁贴同一套机制，真机取证见
 *     include/tile_group.h。写完主屏同样要等重启 / 待机唤醒才重排。
 *     另一处修复落在后端：面板**每次开机都会把这两项重新对齐到主机**，
 *     主机自己改写 app.db 那几行（SMP 重扫、新增账号）不再会把用户的选择吃掉 ——
 *     这正是「关机开机后被重置」那一条。界面把这两件事写在提示条里（见下面的 Note）。
 *
 * 用 layout="panel"：这是一张纯列表的工作区，塞进居中卡片一屏只看得到三四行
 * （与存档配置弹窗同一判断，见 ArchiveConfigModal）。 */

/* 两张图的输出尺寸与格式（与 RenameModal 的 fitCover 同一套设置）：
 *   · 图标 512×512 —— 内置软件在主屏上就是一格方图，主机侧那张也就是这个尺寸；
 *     游戏封面用 1024 是因为它要铺满磁贴大图，这里传大了只是让用户多等几秒；
 *   · 背景 1920×1080 —— 与游戏背景同规格（后端要转成同尺寸的 BC1 DDS）。
 * 格式一律 PNG：图标要保透明与边缘（JPEG 会在边缘糊出一圈脏边），
 * 而 DDS 由后端转（前端不自己编 —— 那一份编码器后端已有两处，见 gameApi.js 顶部契约）。 */
const ICON_SIZE = 512
const BG_W = 1920
const BG_H = 1080

/* 按短边铺满、长边居中溢出地缩到 outW×outH —— 与 CSS `object-cover` 等价的几何。
 *
 * 为什么是**复制**一份而不是 import RenameModal 里那个同名同形的函数：
 * 它是那个文件里的私有函数，抽成公共模块要动一个 900 行、真机验证过的弹窗
 * （那个文件顶部整段写着版式沿革与踩过的坑，改动风险与收益不成比例）。
 * 复制时带上这条注释，改一边时另一边能搜到（全仓库搜 `fitCover` 只有这两处）。
 *
 * canvas 不可用时（老内核极少数情况）退回原图直传：宁可大一点，也不能让「换图标」直接失败。 */
function fitCover(file, outW, outH, mime = 'image/png') {
  return new Promise((resolve) => {
    let url = null
    try { url = URL.createObjectURL(file) } catch { resolve(file); return }
    const img = new Image()
    const done = (out) => { try { URL.revokeObjectURL(url) } catch { /* 忽略 */ } resolve(out) }
    img.onload = () => {
      try {
        const w = img.naturalWidth || img.width
        const h = img.naturalHeight || img.height
        if (!w || !h) { done(file); return }
        /* 目标比例决定裁切窗口：方形目标取短边，16:9 目标按 outW/outH 反推。 */
        const targetRatio = outW / outH
        let sw = w
        let sh = h
        if (w / h > targetRatio) sw = Math.round(h * targetRatio)   // 原图更宽 → 裁两侧
        else sh = Math.round(w / targetRatio)                       // 原图更高 → 裁上下
        const sx = Math.round((w - sw) / 2)
        const sy = Math.round((h - sh) / 2)
        const canvas = document.createElement('canvas')
        canvas.width = outW
        canvas.height = outH
        const ctx = canvas.getContext('2d')
        if (!ctx) { done(file); return }
        ctx.drawImage(img, sx, sy, sw, sh, 0, 0, outW, outH)
        if (typeof canvas.toBlob !== 'function') { done(file); return }
        canvas.toBlob((b) => done(b || file), mime)
      } catch {
        done(file)
      }
    }
    img.onerror = () => done(file)
    img.src = url
  })
}

export default function SystemAppsModal({ show, onClose, addToast }) {
  const { t } = useTranslation()
  const [apps, setApps] = useState([])
  const [count, setCount] = useState(0)
  const [accounts, setAccounts] = useState(0)
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)
  /* 正在写的那一个：拨完到主机回话之间把这一行的开关禁掉，
   * 不然遥控器连按两下会把两条相反的请求都发出去。
   * busyWhat 只是给这一行换一句人话（在写图标 / 在写背景 / 在写主机），
   * 判断「忙不忙」一律看 busyTid —— 两个 state 只有一个真值来源。 */
  const [busyTid, setBusyTid] = useState('')
  const [busyWhat, setBusyWhat] = useState('')
  /* 图标 / 背景预览加载失败的 tid：主机上没有这一条、或者老机型压根没给图标。
   * 按 tid 记账而不是一个布尔：一屏 19 行，一行失败不能把全列表的图都换成占位。 */
  const [iconFailed, setIconFailed] = useState({})
  const [bgFailed, setBgFailed] = useState({})
  /* 每行两个隐藏的文件选择器（图标 / 背景）。键 `${tid}:icon` / `${tid}:bg`：
   * 用一张 ref 表而不是 2N 个 state —— 这些是 DOM 句柄，放 state 只会多一轮渲染。 */
  const fileRefs = useRef({})
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false }
  }, [])

  const load = useCallback(() => {
    setLoading(true)
    setError('')
    setApps([])
    setIconFailed({})
    setBgFailed({})
    return getSystemApps()
      .then((data) => {
        if (!aliveRef.current) return
        setApps((data && data.apps) || [])
        setCount((data && data.count) || 0)
        setAccounts((data && data.accounts) || 0)
        setError((data && data.error) || '')
      })
      .catch((e) => {
        if (aliveRef.current) setError(e.message || t('appsys.load_fail', '读取失败'))
      })
      .finally(() => { if (aliveRef.current) setLoading(false) })
  }, [t])

  useEffect(() => {
    if (!show) return
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开时拉一次主机的内置软件
    load()
  }, [show, load])

  if (!show) return null

  /* 后端没给 controllable 字段（老面板 / mock 漏改）时按「可控」处理：
   * 少一组比少一个开关更糟 —— 那样用户会以为这几条凭空消失了。
   * `.161`：改不动的那几条**根本不列出来**（用户口径），所以这里只做过滤，不再分组。 */
  const shown = apps.filter((a) => a.controllable !== false)

  /* 后端的 count 是**不过滤**的总数（含面板改不动的那几条），列表里只画 shown；
   * 两个数字不一致时以列表为准，免得出现「说 19 条、只列出 12 条」这种对不上账的提示。 */
  const shownCount = shown.length || count

  /* 拨开关 / 上传之前统一进这个状态：busyTid 非空时全列表的控件都禁掉。
   * 真机上后端写的是同一张 app.db，两条相反的请求并发到主机上，落盘先后是随机的 ——
   * 遥控器连按两下就能撞上。 */
  const beginBusy = (tid, what) => { setBusyTid(tid); setBusyWhat(what) }
  const endBusy = () => {
    if (!aliveRef.current) return
    setBusyTid('')
    setBusyWhat('')
  }

  /* 这一行此刻在写什么（徽标位上的小字）。 */
  const busyText = (tid) => {
    if (busyTid !== tid) return ''
    if (busyWhat === 'icon') return t('appsys.saving_icon', '正在写入图标…')
    if (busyWhat === 'bg') return t('appsys.saving_bg', '正在写入背景…')
    return t('appsys.saving', '正在写入主机…')
  }

  const setHidden = async (tid, hidden) => {
    const prev = apps
    /* 乐观更新：电视端一次 POST 往返要几百毫秒，等回话再动开关的话，
     * 用户会以为没按到又按一次。失败再整行退回去。 */
    setApps((list) => list.map((a) => (a.tid === tid ? { ...a, hidden } : a)))
    beginBusy(tid, 'hide')
    try {
      await setSystemAppHidden(tid, hidden)
      addToast?.(hidden
        ? t('appsys.hidden_toast', '已从主屏隐藏，重启或待机唤醒后生效')
        : t('appsys.shown_toast', '已设为显示，重启或待机唤醒后生效'))
    } catch (e) {
      setApps(prev)
      addToast?.(e.message || t('appsys.save_fail', '设置失败'), 'error')
    } finally {
      endBusy()
    }
  }

  /* [CN-FORK] v0.6.0.179 置顶 = 写进**主机主屏的固定列表**（app.db 的 MyHome 组），
   * 不再是本面板自己的 app_pins.json（那是 `.164` 的旧语义，主机看不见）。
   * 与「隐藏」仍是两个开关、两处存储（隐藏写 visible 列），只是现在置顶也落到主机上了。
   * 同一条乐观更新 + 失败回退；toast 里带上后端回的 idx（置顶后的真实位置）。 */
  const setPinned = async (tid, pinned) => {
    const prev = apps
    setApps((list) => list.map((a) => (a.tid === tid ? { ...a, pinned } : a)))
    beginBusy(tid, 'pin')
    try {
      const r = await setSystemAppPinned(tid, pinned)
      /* accounts === 0 = 幂等命中（本来就在最前 / 本来就不在组里），**不是失败**：
       * 说「已置顶」不算错，但说清「本来就在最前」更像人话 —— 同族提示见 v0.5.2.142
       * 「远程还没有这个游戏的存档」那条（首次使用必然遇到，不该是一条红报错）。 */
      const idx = (r && r.idx) || 0
      if (pinned) {
        addToast?.(r && r.accounts === 0
          ? t('appsys.pinned_same', '本来就在主屏固定列表最前')
          : t('appsys.pinned_toast', '已写进主机主屏固定列表（第 {{idx}} 位），重启或待机唤醒后主屏才重排', { idx: idx || 1 }))
      } else {
        addToast?.(r && r.accounts === 0
          ? t('appsys.unpinned_same', '本来就不在主屏固定列表里')
          : t('appsys.unpinned_toast', '已从主机主屏固定列表移除，重启或待机唤醒后生效'))
      }
    } catch (e) {
      setApps(prev)
      addToast?.(e.message || t('appsys.save_fail', '设置失败'), 'error')
    } finally {
      endBusy()
    }
  }

  const fileKey = (tid, kind) => `${tid}:${kind}`

  const pickFile = (tid, kind) => {
    const el = fileRefs.current[fileKey(tid, kind)]
    if (!el) return
    /* 先清空：同一个文件连选两次时 change 不触发，用户会以为「点了没反应」。 */
    el.value = ''
    el.click()
  }

  /* 上传：选图 → 在浏览器里缩到目标尺寸（**还没碰主机**）→ 直接 POST 原始 PNG。
   * 失败时**什么都不改**（不像两颗开关那样乐观 + 回退）：后端没回话之前，
   * 列表上没有一处能证明这张图已经写进去了 —— 提前标「已自定义」就是谎报。 */
  const onFile = async (tid, kind, ev) => {
    const file = ev.target.files && ev.target.files[0]
    if (!file) return
    if (!/^image\//.test(file.type || '')) {
      /* 一处人话提示就够：`accept="image/*"` 只是筛选器，用户照样能改成「所有文件」再选。 */
      addToast?.(t('appsys.not_image', '请选择图片文件（PNG / JPEG）'), 'error')
      return
    }
    beginBusy(tid, kind)
    try {
      const blob = kind === 'icon'
        ? await fitCover(file, ICON_SIZE, ICON_SIZE)
        : await fitCover(file, BG_W, BG_H)
      if (!aliveRef.current) return
      const r = kind === 'icon'
        ? await setAppTileIcon(tid, blob)
        : await setAppTileBackground(tid, blob)
      if (!aliveRef.current) return
      /* 后端回的 message 是**权威文案**，原样显示 —— 主机那一列没写进去时它写的是
       * 「已存到面板，但主机没写进去：…」。过去这里用一句写死的「已写入主机」盖在前面，
       * 那正是「不假成功」要防的事（`.164` 置顶那个老病）。`host_ok:false` 时按错误档
       * 提示：红色、9 秒、alert 语义，用户在主屏上看不出变化时至少知道该去查什么。 */
      const hostOk = !r || r.host_ok !== false
      const text = (r && r.message) || (kind === 'icon'
        ? t('appsys.icon_saved', '图标已写入主机，重启或待机唤醒后主屏生效')
        : t('appsys.bg_saved', '背景已写入主机（后端已转成 DDS），重启或待机唤醒后主屏生效'))
      /* 后端回了 ok 才把这一层标成自定义。custom_icon / custom_bg 是**版本号**：
       * 自增一次就够让 <img> 的 `&v=` 变一次，把缓存里的旧图赶走 —— 不变的话
       * PS5 的老内核会继续显示换图之前那一张，用户看到的就是「换了没生效」。 */
      setApps((list) => list.map((a) => (a.tid === tid
        ? { ...a, ...(kind === 'icon'
          ? { custom_icon: (a.custom_icon || 0) + 1 }
          : { custom_bg: (a.custom_bg || 0) + 1 }) }
        : a)))
      if (kind === 'icon') setIconFailed((m) => ({ ...m, [tid]: false }))
      addToast?.(text, hostOk ? 'success' : 'error')
    } catch (e) {
      addToast?.(e.message || (kind === 'icon'
        ? t('appsys.icon_fail', '上传图标失败')
        : t('appsys.bg_fail', '上传背景失败')), 'error')
    } finally {
      endBusy()
    }
  }

  /* 恢复默认 = 删掉这一层的自定义图（`*_reset`）。同样不做乐观更新：
   * 删没删掉只有后端知道，而这一层的表现是「图变了」，提前变等于先给用户看一眼假的。 */
  const onReset = async (tid, kind) => {
    beginBusy(tid, kind)
    try {
      if (kind === 'icon') {
        await resetAppTileIcon(tid)
        if (!aliveRef.current) return
        setApps((list) => list.map((a) => (a.tid === tid ? { ...a, custom_icon: 0 } : a)))
        /* 失败标记一并清掉：恢复默认之后 <img> 要重新去读**主机自带那张**，
         * 不清的话上一次的「加载失败」会把占位图一直留在屏幕上。 */
        setIconFailed((m) => ({ ...m, [tid]: false }))
        addToast?.(t('appsys.icon_reset', '已恢复主机自带的图标'))
      } else {
        await resetAppTileBackground(tid)
        if (!aliveRef.current) return
        setApps((list) => list.map((a) => (a.tid === tid ? { ...a, custom_bg: 0 } : a)))
        setBgFailed((m) => ({ ...m, [tid]: false }))
        addToast?.(t('appsys.bg_reset', '已恢复主机自带的背景'))
      }
    } catch (e) {
      addToast?.(e.message || t('appsys.reset_fail', '恢复默认失败'), 'error')
    } finally {
      endBusy()
    }
  }

  return (
    <Modal
      show={show}
      title={t('appsys.title', '个性化软件')}
      onClose={onClose}
      layout="panel"
    >
      <div className="panel-body leading-relaxed font-normal flex flex-col gap-4">
        <Note tone="info">
          {t('appsys.hint', '这里只列主机自带的内置软件。右侧两颗按钮换这一条在主屏上的图标与背景图；「置顶」= 把它写进主机主屏的固定列表（排在最前），「隐藏」= 把它从主屏藏起来。改完主屏不会立刻重排，重启主机或待机唤醒后才生效。面板改不动的那几条不列在这里。')}
        </Note>

        {/* [CN-FORK] v0.6.0.179 这一条是「关机开机后被重置」那个修复的用户可见面：
            面板每次开机都会把这两项重放到主机上。用户不必知道实现，但要能预期
            「主机自己把 app.db 改写回去时，我的选择会不会丢」——答案是不会。 */}
        <Note tone="ok">
          {t('appsys.boot_note', '「置顶 / 隐藏」的状态面板每次开机都会重新对齐到主机 —— 主机自己改写 app.db 里那几行（SMP 重扫、新增账号）也不会把它们冲掉。')}
        </Note>

        {error && (
          <Note tone="error" icon={AlertTriangle}>
            {error}
          </Note>
        )}

        {loading && (
          <p className="panel-note text-zinc-500">{t('appsys.loading', '正在读取主机的内置软件…')}</p>
        )}

        {!loading && !shown.length && !error && (
          <MiniEmpty icon={AppWindow}>
            {t('appsys.empty', '主机上没有找到可管理的内置软件')}
          </MiniEmpty>
        )}

        {!loading && shown.length > 0 && (
          <>
            {/* accounts 是「写入时一次写平几个账号」——写出来用户才知道为什么改一条
                会在每个账号上都生效（内置软件的显隐是**按账号**存的）。 */}
            <p className="panel-tiny text-zinc-500">
              {t('appsys.count_hint', '共 {{n}} 条内置软件，主机上有 {{accounts}} 个账号（改一次会同时写到每个账号）。', {
                n: shownCount, accounts: accounts || 1,
              })}
            </p>
            <div className="flex flex-col gap-2">
              {shown.map((a) => {
                const customIcon = !!a.custom_icon
                const customBg = !!a.custom_bg
                const rowBusy = busyTid === a.tid
                /* 版本号进 URL（`&v=`）：换完图 URL 一变，PS5 的老内核才不会继续拿旧图。
                 * 没设过时拼 v=0 —— 那张是主机自带图，本来就不会变。 */
                const iconSrc = `${appIconUrl(a.tid)}&v=${a.custom_icon || 0}`
                const bgSrc = customBg && !bgFailed[a.tid]
                  ? `${appTileBackgroundUrl(a.tid)}&v=${a.custom_bg}`
                  : ''
                return (
                  /* [CN-FORK] v0.6.0.179 一行 = 卡片（原来是一行两列的白线框）。
                   * 每一行左边是图标 + 名称 / TID，右边是控件簇：
                   *   [上传图标][恢复图标?][上传背景][恢复背景?] | [置顶][隐藏]
                   * 两组之间只用一根竖发丝线分隔，不加新颜色、不加底 —— 上传是「换样子」，
                   * 置顶 / 隐藏是「改主屏」，本来就是两件事，但都在这一行里完成（用户口径：
                   * 上传背景的按钮要放在置顶前面）。
                   * 窄屏（手机）由 flex-wrap 让控件簇落到第二行，不挤压名称。 */
                  <div
                    key={a.tid}
                    className={cn(
                      'relative rounded-xl border border-white/10 bg-white/[0.02] overflow-hidden',
                      rowBusy && 'opacity-60',
                    )}
                  >
                    {/* 自定义背景是主机主屏上那张氛围图（16:9），直接铺在这一行的底上 ——
                        与 GameSection 的磁贴同一手法，压暗层数值也完全一致：这样「哪几条
                        设过背景」不用读徽标，扫一眼就看出来了。
                        onError 把整层摘掉：404 时留着压暗层会变成一块纯黑卡片。
                        pointer-events-none：它是装饰，别把点击从按钮上抢走。 */}
                    {bgSrc && (
                      <div className="absolute inset-0 pointer-events-none" aria-hidden="true">
                        <img
                          src={bgSrc}
                          alt=""
                          loading="lazy"
                          className="w-full h-full object-cover"
                          onError={() => setBgFailed((m) => ({ ...m, [a.tid]: true }))}
                        />
                        <div
                          className="absolute inset-0"
                          style={{ background: 'linear-gradient(180deg, rgba(6,8,12,.32) 0%, rgba(6,8,12,.58) 55%, rgba(6,8,12,.86) 100%)' }}
                        />
                      </div>
                    )}

                    <div className="relative p-2.5 flex flex-wrap items-center gap-3">
                      {/* 图标位：尺寸写在最外层这一格上，占位图与真图**同一格** ——
                          图 404 的行不能变矮，否则整个列表当场错位。 */}
                      <div className="relative w-14 h-14 shrink-0 rounded-xl overflow-hidden bg-white/5 border border-white/10 flex items-center justify-center">
                        {iconFailed[a.tid] ? (
                          <AppWindow className="w-7 h-7 text-zinc-600" aria-hidden="true" />
                        ) : (
                          <img
                            src={iconSrc}
                            alt=""
                            loading="lazy"
                            className="w-full h-full object-cover"
                            onError={() => setIconFailed((m) => ({ ...m, [a.tid]: true }))}
                          />
                        )}
                      </div>

                      <div className="min-w-0 flex-1">
                        <p className="text-white panel-note font-semibold truncate">{a.name || a.tid}</p>
                        <p className="text-zinc-500 panel-tiny mt-0.5 leading-snug font-mono tracking-wide truncate">
                          {rowBusy ? busyText(a.tid) : a.tid}
                        </p>
                        {/* 哪一层设过自定义，逐层写出来（用户口径其一：「这一行要能看出
                            是哪一样被改过」）。恢复按钮在右侧控件簇里、紧挨着对应的上传按钮。 */}
                        {(customIcon || customBg) && (
                          <div className="flex flex-wrap items-center gap-1.5 mt-1">
                            {customIcon && <Chip tone="info">{t('appsys.icon_custom', '已自定义图标')}</Chip>}
                            {customBg && <Chip tone="info">{t('appsys.bg_custom', '已自定义背景')}</Chip>}
                          </div>
                        )}
                      </div>

                      <div className="flex flex-wrap items-center gap-2 xl:justify-end">
                        <input
                          type="file"
                          accept="image/*"
                          className="hidden"
                          ref={(el) => { fileRefs.current[fileKey(a.tid, 'icon')] = el }}
                          onChange={(e) => onFile(a.tid, 'icon', e)}
                        />
                        <input
                          type="file"
                          accept="image/*"
                          className="hidden"
                          ref={(el) => { fileRefs.current[fileKey(a.tid, 'bg')] = el }}
                          onChange={(e) => onFile(a.tid, 'bg', e)}
                        />

                        <button
                          type="button"
                          className="btn btn-outline"
                          disabled={busyTid !== ''}
                          onClick={() => pickFile(a.tid, 'icon')}
                        >
                          <ImagePlus className="w-4 h-4" />
                          <span>{t('appsys.upload_icon', '上传图标')}</span>
                        </button>
                        {customIcon && (
                          <button
                            type="button"
                            className="btn btn-ghost"
                            disabled={busyTid !== ''}
                            /* 可见文字只有「恢复图标」，读屏念出来不知道是恢复谁 —— 与
                               RenameModal 的 applyLabel 同一考虑，补一个带应用名的 aria-label。 */
                            aria-label={`${a.name || a.tid} ${t('appsys.reset_icon', '恢复图标')}`}
                            onClick={() => onReset(a.tid, 'icon')}
                          >
                            <RotateCcw className="w-4 h-4" />
                            <span>{t('appsys.reset_icon', '恢复图标')}</span>
                          </button>
                        )}

                        <button
                          type="button"
                          className="btn btn-outline"
                          disabled={busyTid !== ''}
                          onClick={() => pickFile(a.tid, 'bg')}
                        >
                          <ImageIcon className="w-4 h-4" />
                          <span>{t('appsys.upload_bg', '上传背景')}</span>
                        </button>
                        {customBg && (
                          <button
                            type="button"
                            className="btn btn-ghost"
                            disabled={busyTid !== ''}
                            aria-label={`${a.name || a.tid} ${t('appsys.reset_bg', '恢复背景')}`}
                            onClick={() => onReset(a.tid, 'bg')}
                          >
                            <RotateCcw className="w-4 h-4" />
                            <span>{t('appsys.reset_bg', '恢复背景')}</span>
                          </button>
                        )}

                        {/* 「换样子」与「改主屏」之间只有一根发丝线（既有 border 档，不是新颜色）：
                            分组靠间距与这道线，不靠颜色。 */}
                        <span className="w-px h-8 bg-white/10 mx-1" aria-hidden="true" />

                        {/* [CN-FORK] v0.5.2.164 一行两个开关。
                         * 每个开关前面带一枚小字标签 —— 电视端没有 hover，光看两颗长得一样的
                         * 滑块分不出哪个是哪个（`.178` 版式改成卡片后这一条依然成立）。
                         * 方向都不取反：开=置顶 / 开=隐藏。 */}
                        <div className="flex items-center gap-2">
                          <span className="panel-tiny font-semibold text-zinc-400">{t('appsys.pin', '置顶')}</span>
                          <Switch
                            checked={!!a.pinned}
                            disabled={busyTid !== ''}
                            label={`${a.name || a.tid} ${t('appsys.pin_label', '置顶（写进主机主屏固定列表，排在最前）')}`}
                            onChange={(v) => setPinned(a.tid, v)}
                          />
                        </div>
                        <div className="flex items-center gap-2">
                          <span className="panel-tiny font-semibold text-zinc-400">{t('appsys.hide', '隐藏')}</span>
                          <Switch
                            checked={!!a.hidden}
                            disabled={busyTid !== ''}
                            label={`${a.name || a.tid} ${t('appsys.hide_label', '隐藏（从主屏藏起来）')}`}
                            onChange={(v) => setHidden(a.tid, v)}
                          />
                        </div>
                      </div>
                    </div>
                  </div>
                )
              })}
            </div>
          </>
        )}
      </div>
    </Modal>
  )
}
