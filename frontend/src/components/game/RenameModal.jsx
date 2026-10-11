import React, { useState, useEffect, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { Type, ImagePlus, RotateCcw, Upload, AlertTriangle, Check, Gamepad2, X, Music, Info } from 'lucide-react'
import Modal from '../ui/Modal'
import { Block, Chip, Note } from './parts'
import {
  renameGame, setTileIcon, resetTileIcon, tileIconUrl,
  setTileBackground, resetTileBackground, tileBackgroundUrl,
  setGameMusic, resetGameMusic, gameMusicUrl,
} from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.163 「名称与背景」重排（用户口径）：
 *   · 磁贴图标占**左侧大格**（占两行高），图标预览显著放大；
 *   · 右侧**上下两栏**：上 = 游戏名称，下 = 背景音乐，二者各占一行；
 *   · 原来的「磁贴背景」整幅块整段保留在**下方**（不在两侧并排里），
 *     这样「氛围图」与「图标 / 名称 / 音乐」三类互不打架。
 *
 * 图像规格（与上一版一致）：
 *   · 磁贴图标：1:1 PNG → 1024×1024；
 *   · 磁贴背景：16:9 PNG → 1920×1080（后端转 DXT1 DDS）；
 *   · 背景音乐：MP3 / OGG / WAV，体积由后端约束（默认 ≤30MB），
 *     落点 `/data/nayeplmtools/music/<TID>.mp3`，由后端 `POST /game_music` / `reset` 提供。

/* [CN-FORK] v0.5.2.112 名称修改 + 磁贴图标 + 磁贴背景（磁贴第三颗按钮）
 *
 * 用户口径：「第三个代表名称修冷（改名称），可以改名称，改磁贴背景。」
 * 之后追加一轮：「修改名称也是铺满，但是你只把修改磁贴图标放出来了，还要增加一个修改背景的。」
 * 再一轮定下形态与格式：「将磁贴图标与游戏名称放在一行，磁贴背景放在下面，
 * 磁贴背景预览图是16:9的dds图片。图标是png图片。」
 *
 * 三件事各自的一个关键决定：
 *
 * ① 改名 —— 只改**显示名**，不碰 TITLE_ID。这不是随口定的：v0.5.2.111 刚因为「按名字派生
 *    账号 ID」栽过一次（改名把 ID 也改了）。同一个教训在这里再落一次地 —— TID 是游戏在系统里的
 *    身份，名字只是标签。所以这个弹窗把 TID 显示在副标题里、改名区写明「只改显示名」，
 *    而且提交时只发 name，绝不发 tid 之外的任何标识。
 *
 * ② 磁贴图标 与 ③ 磁贴背景 —— 这是**两层**，不是一件事。上一版把「换封面」当成「换背景」，
 *    结果用户一换背景就把游戏的官方封面也丢了，而且没有任何办法只换其中一样。
 *    现在拆开（接口也拆成 /game_tile_icon 与 /game_tile_background 两套，见 gameApi.js）：
 *      · 图标 = 封面那一格方图（默认 icon0.png），**1:1 PNG**；
 *      · 背景 = 整张卡片（含名称与 TID 那行）的底图，**16:9**，最终以 DDS 落到主机上，
 *        图标压在它上面，所以它只在图标四周与文字区露出来 —— 是「氛围」不是「内容」。
 *    两者可以各自独立设置与还原。
 *
 * 版面上（v0.5.2.114 版式沿革）：
 *   · 名称 + 图标同一行（各占半屏，图标预览 `w-[8.25rem]` 方图在左、按钮说明在右）
 *   · 磁贴背景另起一行整幅，内部**也**是「图左、按钮说明右」的左右分栏 —— 与图标块同结构
 *   · 背景块内部 v.112 有过一轮竖排（图片独占一整行 → 按钮 → 说明），因为竖排时预览能开到
 *     最大；但整块太高，按钮要滚才看得见。v.114 用户又改回左右分栏：「磁贴背景的按钮和
 *     说明放到图片右边」。这一版布局与图标块完全对称，两个块并排时视觉一致。
 *
 * 两层图都是「**先暂存、再应用**」（用户：「这里点击也可以上传图片，上传完要有一个应用的动作」）：
 *   · 预览图本身就是一个可点的按钮（hover 会浮出一层暗罩 + 一颗上传图标），
 *     所以「点图片」与「点上传图片按钮」是同一条路径 —— 用户不必先找到按钮；
 *   · 选完文件先在浏览器里缩好、用 objectURL 就地预览，**不直接推到主机**；
 *     要点「应用」才提交后端。多这一步的理由很实在：
 *       ① 选错图（比如把竖图当 16:9 选进来）不会先污染主机上的磁贴；
 *       ② 1920×1080 的 PNG 传到 PS5 要好几秒，传完才发现选错就得再传一次；
 *       ③ 「上传」与「生效」本来是两件事，界面上就该是两步。
 *   · 「取消」丢掉暂存图并回收 objectURL；「恢复默认」照旧，顺带丢掉未应用的暂存图。
 *
 * 上传前在浏览器里先缩好再编码成 PNG，**避免把一张 10MB 的 4K 原图推给主机**
 * （PS5 的 WebKit 编码慢，主机侧也没有必要存原图）；canvas 不可用时（老内核极少数情况）
 * 退回原图直传，宁可大一点也不能失败。
 * 不做拖拽取景框：那个交互（见 AccountToolsSection 的 CropModal）是给头像做的，有 200 行，
 * 而磁贴图是「氛围图」不是「证件照」，居中裁切够用。
 */

/* 两张图的输出尺寸与格式。图标是 1:1 PNG（磁贴上半部本来就是方图）；
 * 背景是 **16:9**（用户口径：「磁贴背景预览图是16:9的dds图片。图标是png图片」）。
 *
 * 为什么背景是 1920×1080 而不是别的 16:9：仓库里控制台已验证的那张参考底图
 * （`assets/pic0.dds`，由 `make_pic0.py` 生成，见 HANDOVER §3 的素材规范）就是
 * 1920×1080 DXT1。背景这一层最终是要交给后端转成同规格的 DDS 的，尺寸与它对齐
 * 才不会在转换那一步被再缩一次。
 *
 * 格式分工（本轮的口径）：
 *   · 图标 → 浏览器里编成 **PNG** 再上传（要保边缘与透明，JPEG 会在图标边缘糊出一圈脏边）
 *   · 背景 → 浏览器里编成 **PNG** 上传，**DXT1 DDS 的转换交给后端**
 *     （与头像那条路完全一致：前端只传图片，`avatar.c` 负责 png→DXT5）。
 *     前端不自己编 DDS：PS5 老内核对 1920×1080 跑一遍压块要慢一拍，
 *     而且那份编码器在后端已经有两处实现（`avatar.c` 与 `make_pic0.py`），没必要再来第三份。 */
const ICON_SIZE = 1024
const BG_W = 1920
const BG_H = 1080

/* ── 音乐上传（v0.5.2.163 新增）────────────────────────────────────────────
 * 与 icon / bg 同样的「先暂存、再应用」流程：选文件 → 在浏览器里只读不传 →
 * objectURL 本地预览 → 「应用」才走 POST /game_music。后端负责把文件落
 * /data/nayeplmtools/music/<TID>.<ext>，并在主机索引里登记。
 * 「恢复默认」走 /game_music_reset（删主机上已有的那份）。
 * 体积上限 30MB（后端约束；前端先裁掉过大的文件，省一次上传往返）。 */
const MUSIC_SIZE_LIMIT_MB = 30

/* 按短边铺满、长边居中溢出地缩到 outW×outH —— 与 CSS `object-cover` 的等价几何。
 * mime 决定编码格式：图标与背景都要 PNG（见上方格式分工）。 */
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

/* 回收一张暂存图的 objectURL。被回收的可能是 null（没暂存过）、也可能 url 为空串
 * （老内核创建 objectURL 失败，见 onImageFile）—— 两种都要安静放过。 */
function dropStagedUrl(entry) {
  if (!entry || !entry.url) return
  try { URL.revokeObjectURL(entry.url) } catch { /* 忽略 */ }
}

export default function RenameModal({ show, game, onClose, addToast, onChanged }) {
  const { t } = useTranslation()
  const tid = game && game.tid
  const [name, setName] = useState('')
  const [savingName, setSavingName] = useState(false)
  const [iconCustom, setIconCustom] = useState(false)
  const [iconVer, setIconVer] = useState(0)
  const [iconBusy, setIconBusy] = useState(false)
  const [iconFailed, setIconFailed] = useState(false)
  const [bgCustom, setBgCustom] = useState(false)
  const [bgVer, setBgVer] = useState(0)
  const [bgBusy, setBgBusy] = useState(false)
  const [bgFailed, setBgFailed] = useState(false)
  /* [CN-FORK] v0.5.2.163 背景音乐：custom=主机已有自定义音乐；ver=版本号破缓存；
   * busy/failed 同图标 / 背景；staged.music 是「待应用」blob+url（不直接传主机）。 */
  const [musicCustom, setMusicCustom] = useState(false)
  const [musicVer, setMusicVer] = useState(0)
  const [musicBusy, setMusicBusy] = useState(false)
  const [musicFailed, setMusicFailed] = useState(false)
  /* 三层各自的「待应用」图/音乐：{ blob, url } | null。选了文件先落在这里，
   * 预览走本地 objectURL（此时还没碰过主机），点「应用」才 uploadTile。
   * 三层的键名与 pickImage / tile 接口同一套命名，免得再加第四层时要现起名字。 */
  const [staged, setStaged] = useState({ icon: null, bg: null, music: null })
  const [err, setErr] = useState('')
  /* v0.5.2.132 名字源持久化结果。{ state: 'ok'|'warn', note } | null。
   * 改名成功但源改不动（镜像 / 系统内置）时，这个提示必须留在弹窗里而不是只走 toast ——
   * toast 几秒就没了，而「重启 SMP 之后会不会又变回去」是用户下次重扫时才会撞上的问题，
   * 只在成功那一刻弹一下等于没说。
   *
   * [CN-FORK] v0.5.2.165 从「一条」改成「一列」：改名写三层，每层都可能出问题，
   * 而它们的原因与后果完全不同（源没写 → 重扫会回退；主机库没写 → PS5 主屏压根不变）。
   * 挤成一条会互相盖掉，用户就只剩一句「没生效」。数组顺序 = 由下到上的层次：
   * 先源、后主机库，与界面上「磁贴图标 / 名称 / 背景音乐」的排布方向无关，纯按因果。 */
  const [notes, setNotes] = useState([])
  const iconFileRef = useRef(null)
  const bgFileRef = useRef(null)
  /* [CN-FORK] v0.5.2.163 音乐文件选择器（独立 ref，与 icon/bg 分开）。 */
  const musicFileRef = useRef(null)
  const aliveRef = useRef(true)
  /* 暂存图的 objectURL 必须有人回收：它们不归浏览器缓存管，组件卸载后还会把整张
   * 1920×1080 的位图钉在内存里（电视端内存尤其紧）。
   * 回收**只有这一处**（见下面的同步 effect）：谁把 staged 里的某一层换掉或清掉，
   * 这里就顺手 revoke 掉那一层的旧 URL，于是「换一张」「应用」「取消」「恢复默认」
   * 「换游戏」「关弹窗」六条路径全都不用各自记得回收 —— 少记一处就是漏一处。 */
  const stagedRef = useRef({ icon: null, bg: null, music: null })

  useEffect(() => {
    aliveRef.current = true
    return () => {
      aliveRef.current = false
      const cur = stagedRef.current
      Object.keys(cur).forEach((k) => dropStagedUrl(cur[k]))
      stagedRef.current = { icon: null, bg: null, music: null }
    }
  }, [])

  useEffect(() => {
    const prev = stagedRef.current
    stagedRef.current = staged
    Object.keys(prev).forEach((k) => {
      const a = prev[k]
      const b = staged[k]
      if (a && a.url && (!b || b.url !== a.url)) dropStagedUrl(a)
    })
  }, [staged])

  /* 打开 / 换游戏时把输入框、三层图的状态、错误全部归零。
   * 写在**渲染期**（键为 tid）而不是 effect 里：effect 体里同步 setState 会引发级联渲染，
   * 而「prop 变了就把这几个 state 重置」本来就属于 React 官方说的「渲染期调整 state」。
   * 注意这里**不能碰 ref**（react-hooks/refs 会报「Cannot access refs during render」）——
   * 所以暂存图的 objectURL 回收交给上面那条同步 effect 去做，它按「上一轮有、这一轮没了」判。 */
  const openKey = show && tid ? tid : ''
  const [loadedKey, setLoadedKey] = useState('')
  if (openKey !== loadedKey) {
    setLoadedKey(openKey)
    setName((game && game.name) || '')
    /* 「已经设过没有」要认**列表项**给的两个版本号字段（icon / bg），不是详情接口的
     * title_icon_custom / title_bg_custom —— 这个弹窗是磁贴按钮打开的，拿到的就是列表项。
     * 认错字段的症状很隐蔽：换完图之后重新打开，两块都显示「默认」、两颗「恢复默认」都点不动，
     * 而磁贴上明明已经是自定义图了。两个都认一遍，任一处给了就不算默认。 */
    setIconCustom(!!(game && (game.icon || game.title_icon_custom)))
    setBgCustom(!!(game && (game.bg || game.title_bg_custom)))
    /* v0.5.2.163 音乐字段读 game.music（列表项带了版本号就有自定义）。 */
    setMusicCustom(!!(game && game.music))
    setErr('')
    setNotes([])
    setIconVer(0); setBgVer(0); setMusicVer(0)
    setIconBusy(false); setBgBusy(false); setMusicBusy(false)
    setIconFailed(false); setBgFailed(false); setMusicFailed(false)
    setSavingName(false)
    /* 换游戏/关闭时把上一款游戏的暂存图丢掉（否则它会跟着预览显示到下一款上，
     * 用户会以为「这款游戏已经有自定义图了」）。 */
    setStaged({ icon: null, bg: null, music: null })
  }

  if (!show || !game) return null

  const trimmed = name.trim()
  const nameChanged = trimmed && trimmed !== (game.name || '')

  /* 「应用 / 取消」这两颗按钮在同一个面板里各有**两颗**（图标一层、背景一层），
   * 可见文字完全相同。可见文字一样是对的（对着自己那一块看不会有歧义），
   * 但可访问名必须区分 —— 读屏念出来只有「应用」，用户不知道应用的是哪一层；
   * 自动化也一样，`button:has-text("应用")` 会同时匹配到两颗，还会匹配到预览图
   * 按钮里的那颗「待应用」芯片（`:has-text` 是子串匹配）。所以补一个带层名的 aria-label。 */
  const layerName = (which) => (which === 'icon' ? t('game.icon_block', '磁贴图标') : t('game.bg_block', '磁贴背景'))
  const applyLabel = (which) => `${t('game.tile_apply', '应用')}（${layerName(which)}）`
  const cancelLabel = (which) => `${t('game.tile_cancel', '取消')}（${layerName(which)}）`

  const onSaveName = async () => {
    if (!trimmed) { setErr(t('game.name_empty', '名称不能为空')); return }
    if (!nameChanged) return
    setSavingName(true)
    setErr('')
    try {
      const r = await renameGame(tid, trimmed)
      addToast?.(t('game.renamed', '名称已改为「{{name}}」', { name: trimmed }))
      /* 名字**源**（游戏包里的 sce_sys/param.json）改成功了吗。
       * false **不等于**改名失败 —— 面板层与主机索引层已经生效，只是 SMP 重扫会把
       * 电视主屏刷回原名。所以它是常驻提示而不是错误。 */
      const next = []
      const pn = (r && r.persist_note) ? r.persist_note : ''
      if (r && r.persisted === false && pn) next.push({ tone: 'warn', note: pn })
      else if (r && r.persisted && pn) next.push({ tone: 'ok', note: pn })
      /* [CN-FORK] v0.5.2.165 主机磁贴数据库（app.db 平铺列 + appinfo.db）这一层。
       * 它的成败以前**只进主机日志**，前端一律报「名称已改为…」—— 于是
       * 「库里压根没写进去」和「写进去了、Shell 还没重绘」在界面上长得一模一样，
       * 用户看到的就是「改完名 PS5 上没生效」。两条分支都写清楚： */
      const hn = (r && r.host_note) ? r.host_note : ''
      if (r && r.host_ok === false) {
        next.push({
          tone: 'error',
          note: t('game.rename_host_fail',
            '主机磁贴数据库没有写进去，PS5 主屏不会改名：{{why}}',
            { why: hn || t('game.rename_host_fail_unknown', '原因见主机日志') }),
        })
      } else if (r && r.host_ok === true) {
        next.push({
          tone: 'info',
          note: t('game.rename_host_repaint',
            '已写入主机磁贴数据库。PS5 主屏的磁贴名字和自定义图标同源，Shell 只在重启主机或待机唤醒后才重绘 —— 现在回主屏看还是旧名属于正常，不是没生效。'),
        })
      }
      setNotes(next)
      onChanged?.()
    } catch (e) {
      setErr(e.message || t('game.rename_fail', '改名失败'))
    }
    if (aliveRef.current) setSavingName(false)
  }

  /* 两层图共用一套「选文件 → 缩 → 暂存 → 应用」的流程，只有目标尺寸、接口与文案不同。
   * 抽成一条 pickImage / onImageFile / applyStaged 而不是两份 handler：
   * 将来加第三层（比如占位图）只加一行配置。
   *
   * v0.5.2.163 扩到第三层「音乐」：mp3/ogg/wav 不需要缩放，直接 blob 暂存；
   * onImageFile 通过 kind 区分：icon/bg 走 fitCover，music 走「只校验 mime / 大小」。 */
  const pickImage = (which) => {
    if (which === 'icon') {
      if (iconFileRef.current) { iconFileRef.current.value = ''; iconFileRef.current.click() }
    } else if (which === 'bg') {
      if (bgFileRef.current) { bgFileRef.current.value = ''; bgFileRef.current.click() }
    } else {
      if (musicFileRef.current) { musicFileRef.current.value = ''; musicFileRef.current.click() }
    }
  }

  /* 选文件 → 缩到目标尺寸（音乐跳过）→ **只暂存**，不碰主机。
   * 暂存期间也占 busy（缩图是异步的，用户得有反馈），缩完立刻放开。 */
  const onImageFile = async (which, ev) => {
    const file = ev.target.files && ev.target.files[0]
    if (!file) return
    const setBusy = which === 'icon' ? setIconBusy : which === 'bg' ? setBgBusy : setMusicBusy
    setBusy(true)
    setErr('')
    try {
      let blob
      if (which === 'music') {
        /* 音乐只判 audio/* 与体积；不做格式转换 —— 浏览器不直接支持 mp3→ogg。
         * 后端只接 audio/mpeg/ogg/wav 几类，其它类型 415。 */
        const okType = /^audio\/(mpeg|mp3|ogg|wav|x-wav)$/.test(file.type || '')
        const okExt = /\.(mp3|ogg|wav)$/i.test(file.name || '')
        if (!okType && !okExt) {
          setErr(t('game.music_bad_type', '请选择 MP3 / OGG / WAV 音频文件'))
          setBusy(false); return
        }
        if (file.size > MUSIC_SIZE_LIMIT_MB * 1024 * 1024) {
          setErr(t('game.music_too_big', '音乐文件过大（{{n}} MB 上限）', { n: MUSIC_SIZE_LIMIT_MB }))
          setBusy(false); return
        }
        blob = file
      } else if (!/^image\//.test(file.type || '')) {
        setErr(t('game.not_image', '请选择图片文件'))
        setBusy(false); return
      } else {
        blob = which === 'icon'
          ? await fitCover(file, ICON_SIZE, ICON_SIZE)
          : await fitCover(file, BG_W, BG_H)
      }
      if (!aliveRef.current) return
      let url = ''
      try { url = URL.createObjectURL(blob) } catch { /* 老内核万一不支持 objectURL，退化成没有预览：仍可应用 */ }
      /* 换一张就把上一张顶掉 —— 旧 URL 由上面那条同步 effect 回收，这里不重复写一遍。 */
      setStaged((s) => ({ ...s, [which]: { blob, url } }))
    } catch {
      if (aliveRef.current) setErr(t('game.stage_fail', '读取这份文件失败，请换一份试试'))
    }
    if (aliveRef.current) setBusy(false)
  }

  /* 「应用」= 真正提交到主机。失败时**保留暂存图**，用户可以再点一次（网络抖一下不至于要重选）。 */
  const applyStaged = async (which) => {
    const cur = stagedRef.current[which]
    if (!cur) return
    const setBusy = which === 'icon' ? setIconBusy : which === 'bg' ? setBgBusy : setMusicBusy
    setBusy(true)
    setErr('')
    try {
      if (which === 'icon') {
        await setTileIcon(tid, cur.blob)
        if (!aliveRef.current) return
        setIconCustom(true); setIconFailed(false); setIconVer((v) => v + 1)
        addToast?.(t('game.icon_saved', '磁贴图标已应用'))
      } else if (which === 'bg') {
        await setTileBackground(tid, cur.blob)
        if (!aliveRef.current) return
        setBgCustom(true); setBgFailed(false); setBgVer((v) => v + 1)
        addToast?.(t('game.bg_saved', '磁贴背景已应用'))
      } else {
        /* 音乐层调专用 API（设 / 恢复默认） */
        await setGameMusic(tid, cur.blob, cur.name)
        if (!aliveRef.current) return
        setMusicCustom(true); setMusicFailed(false); setMusicVer((v) => v + 1)
        addToast?.(t('game.music_saved', '背景音乐已应用'))
      }
      setStaged((s) => ({ ...s, [which]: null }))
      onChanged?.()
    } catch (e) {
      if (aliveRef.current) {
        const fallback = which === 'icon' ? 'game.icon_fail' : which === 'bg' ? 'game.bg_fail' : 'game.music_fail'
        const defaultMsg = which === 'icon' ? '设置图标失败' : which === 'bg' ? '设置背景失败' : '设置音乐失败'
        setErr(e.message || t(fallback, defaultMsg))
      }
    }
    if (aliveRef.current) setBusy(false)
  }

  /* 「取消」只丢暂存图，不动主机上已经生效的那一张 ——
   * 所以取消之后预览会退回「当前生效的图」，而不是变空。 */
  const cancelStaged = (which) => {
    setErr('')
    setStaged((s) => ({ ...s, [which]: null }))
  }

  const onResetImage = async (which) => {
    const setBusy = which === 'icon' ? setIconBusy : which === 'bg' ? setBgBusy : setMusicBusy
    setBusy(true)
    setErr('')
    try {
      if (which === 'icon') {
        await resetTileIcon(tid)
        if (!aliveRef.current) return
        setIconCustom(false); setIconFailed(false); setIconVer((v) => v + 1)
        addToast?.(t('game.icon_reset', '已恢复默认图标'))
      } else if (which === 'bg') {
        await resetTileBackground(tid)
        if (!aliveRef.current) return
        setBgCustom(false); setBgFailed(false); setBgVer((v) => v + 1)
        addToast?.(t('game.bg_reset', '已恢复默认底色'))
      } else {
        await resetGameMusic(tid)
        if (!aliveRef.current) return
        setMusicCustom(false); setMusicFailed(false); setMusicVer((v) => v + 1)
        addToast?.(t('game.music_reset', '已恢复默认音乐'))
      }
      /* 恢复默认连未应用的暂存图一起丢掉：留着的话预览仍是那张暂存图，
       * 用户会以为「恢复默认没生效」。 */
      setStaged((s) => ({ ...s, [which]: null }))
      onChanged?.()
    } catch (e) {
      if (aliveRef.current) {
        const fallback = which === 'icon' ? 'game.icon_reset_fail' : which === 'bg' ? 'game.bg_reset_fail' : 'game.music_reset_fail'
        const defaultMsg = which === 'icon' ? '恢复默认图标失败' : which === 'bg' ? '恢复默认底色失败' : '恢复默认音乐失败'
        setErr(e.message || t(fallback, defaultMsg))
      }
    }
    if (aliveRef.current) setBusy(false)
  }

  /* 两层图的地址：**暂存图优先**（那是用户刚要应用的东西，预览必须显示它），
   * 其次是主机上已经生效的自定义图，最后是默认图。
   * v 参数是必需的：应用完图后 URL 不变的话，PS5 的 WebKit 会拿缓存里的旧图，
   * 用户会看到「明明应用成功了但预览还是旧的」。 */
  const iconSrc = staged.icon
    ? staged.icon.url
    : (iconCustom ? `${tileIconUrl(tid)}&v=${iconVer}` : `/game_icon?tid=${encodeURIComponent(tid)}`)
  const bgSrc = staged.bg
    ? staged.bg.url
    : (bgCustom ? `${tileBackgroundUrl(tid)}&v=${bgVer}` : '')

  /* 有暂存图时一定要显示（哪怕上一张 server 图加载失败过）——
   * 否则用户点了「上传图片」却什么都没看到，会以为没生效。 */
  const showBg = !!(bgSrc && (staged.bg || !bgFailed))
  const showIcon = !!(iconSrc && (staged.icon || !(iconFailed && !iconCustom)))

  return (
    <Modal
      show={show}
      title={t('game.rename_title', '名称与背景')}
      onClose={onClose}
      layout="panel"
    >
      <div className="panel-body leading-relaxed font-normal flex flex-col gap-5">
        <p className="text-zinc-500 panel-note font-mono tracking-wider">
          {tid}{game.name ? ` · ${game.name}` : ''}
        </p>

        {err && <Note tone="error" icon={AlertTriangle}>{err}</Note>}

        {/* ── 主区域：左 = 磁贴图标（占两行高，放大），右 = 上下两栏 ──
            用户口径（图）：左侧大格 = 磁贴图标 + 编辑控件，右侧上面 = 游戏名称，
            右侧下面 = 背景音乐；磁贴背景挪到整图下方整幅铺开（与「氛围图」语义一致）。

            版式：grid-cols-[320px_1fr] / 默认小屏 stack。
            左列 grid 跨两行（row-span-2），右列 上下两块各占一行。 */}
        <div className="grid grid-cols-1 xl:grid-cols-[320px_1fr] xl:grid-rows-2 gap-5">
          {/* ── 左列：磁贴图标（占两行，预览显著放大）── */}
          <Block
            className="min-w-0 xl:row-span-2"
            title={t('game.icon_block', '磁贴图标')}
            icon={Gamepad2}
            actions={<span className="panel-tiny text-zinc-500">{iconCustom ? t('game.icon_custom', '自定义图标') : t('game.icon_default', '默认图标')}</span>}
          >
            <div className="flex flex-col items-center gap-3 h-full">
              {/* 预览区域 = 一整列宽；放大到 w-full + aspect-square，
                  原 v.114 是 w-[8.25rem] 的小方块；这里点图片本身即可选图。 */}
              <button
                type="button"
                onClick={() => pickImage('icon')}
                disabled={iconBusy}
                aria-label={t('game.icon_pick', '更换磁贴图标')}
                title={t('game.tile_pick_hint', '点图片也可以选图')}
                className="group relative w-full max-w-[280px] mx-auto aspect-square rounded-2xl overflow-hidden bg-white/5 border border-white/10 hover:border-white/30 transition-colors flex items-center justify-center"
              >
                <Gamepad2 className="w-12 h-12 text-zinc-600 absolute" />
                {showIcon && (
                  <img
                    src={iconSrc}
                    alt=""
                    className="relative w-full h-full object-cover"
                    onError={() => setIconFailed(true)}
                  />
                )}
                <span className="absolute inset-x-0 bottom-0 py-2 panel-micro font-semibold text-white bg-black/65 opacity-0 group-hover:opacity-100 transition-opacity">
                  {t('game.tile_pick_hint', '点图片也可以选图')}
                </span>
                {staged.icon && (
                  <Chip tone="warn" className="absolute top-2 left-2">{t('game.tile_staged', '待应用')}</Chip>
                )}
              </button>
              {/* 文案与 `locales-overlay/*.json` 里的 `game.icon_preview` 保持一致
                  （overlay 才是运行时真相源，内联兜底只是它的镜像 —— 两边不一样
                  就会出现「dev 与出包看到的不是同一句」）。尺寸那一截写在下面的
                  `game.icon_limit` 里，不在这句重复。 */}
              <p className="panel-tiny text-zinc-500 text-center">{t('game.icon_preview', '图标预览（1:1）')}</p>
              {/* 这一句是「换这张图到底改了什么」的说明 —— v0.5.2.163 重排时一度
                  只剩尺寸说明，等于把「替换游戏封面」这层语义丢了；音乐块一直有
                  对应的 music_desc，两块对齐才不至于一块有说明一块没有。 */}
              <p className="panel-note text-zinc-400 text-center">
                {t('game.icon_desc', '替换游戏封面。按短边居中裁成 1:1，预览就是磁贴上看到的那一格。')}
              </p>

              {/* 按钮行：没选图时是「上传图片 / 恢复默认」，选了图之后换成「应用 / 取消」。
                  与左侧块对称 —— 这是用户口径里「图标修改中的图标进行放大」的延伸：
                  控件区与预览区都变宽，按钮跟着铺满。 */}
              <div className="flex flex-wrap items-center justify-center gap-2 w-full">
                {staged.icon ? (
                  <>
                    <button type="button" className="btn btn-primary flex-1" disabled={iconBusy} aria-label={applyLabel('icon')} onClick={() => applyStaged('icon')}>
                      <Check className="w-4 h-4" />
                      <span>{iconBusy ? t('game.uploading', '处理中…') : t('game.tile_apply', '应用')}</span>
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={iconBusy} aria-label={cancelLabel('icon')} onClick={() => cancelStaged('icon')}>
                      <X className="w-4 h-4" />
                      <span>{t('game.tile_cancel', '取消')}</span>
                    </button>
                  </>
                ) : (
                  <>
                    <button type="button" className="btn btn-outline flex-1" disabled={iconBusy} onClick={() => pickImage('icon')}>
                      <Upload className="w-4 h-4" />
                      <span>{iconBusy ? t('game.uploading', '处理中…') : t('game.bg_upload', '上传图片')}</span>
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={iconBusy || !iconCustom} onClick={() => onResetImage('icon')}>
                      <RotateCcw className="w-4 h-4" />
                      <span>{t('game.bg_reset_btn', '恢复默认')}</span>
                    </button>
                  </>
                )}
              </div>
              {staged.icon && (
                <p className="panel-tiny text-amber-400 text-center">
                  {t('game.tile_apply_hint', '已选好新图，点「应用」后才会写入主机。')}
                </p>
              )}
              <p className="panel-tiny text-zinc-500 text-center">
                {t('game.icon_limit', '支持 PNG / JPEG，上传前会自动缩到 1024×1024 的 PNG。')}
              </p>
            </div>
          </Block>

          {/* ── 右列上：游戏名称（占一行）── */}
          <Block
            className="min-w-0"
            title={t('game.rename_block', '游戏名称')}
            icon={Type}
            actions={
              <button
                type="button"
                className="btn btn-primary"
                disabled={savingName || !nameChanged}
                onClick={onSaveName}
              >
                <Check className="w-4 h-4" />
                <span>{savingName ? t('game.saving', '保存中…') : t('game.save_name', '保存名称')}</span>
              </button>
            }
          >
            <label className="block min-w-0">
              <span className="block panel-tiny font-semibold text-zinc-400 mb-1">{t('game.new_name', '新名称')}</span>
              <input
                className="field"
                type="text"
                value={name}
                maxLength={128}
                placeholder={t('game.name_ph', '输入要显示的名称')}
                onChange={(e) => setName(e.target.value)}
                onKeyDown={(e) => { if (e.key === 'Enter') onSaveName() }}
              />
            </label>
            {/* 「只改显示名」必须写在界面上 —— v0.5.2.111 的账号 ID 事故教训。 */}
            <p className="panel-tiny text-zinc-500 mt-2">
              {t('game.rename_hint', '只修改显示名称，不会改动标题 ID（{{tid}}），也不影响存档与启动。', { tid })}
            </p>
            {notes.map((n, i) => (
              <Note key={i}
                    tone={n.tone}
                    icon={n.tone === 'ok' ? Check : n.tone === 'info' ? Info : AlertTriangle}
                    className={i === 0 ? 'mt-2' : 'mt-1.5'}>
                {n.note}
              </Note>
            ))}
          </Block>

          {/* ── 右列下：背景音乐（占一行，新增）── */}
          <Block
            className="min-w-0"
            title={t('game.music_block', '背景音乐')}
            icon={Music}
            actions={<span className="panel-tiny text-zinc-500">{musicCustom ? t('game.music_custom', '自定义音乐') : t('game.music_default', '默认（无）')}</span>}
          >
            <div className="flex flex-col sm:flex-row gap-3 items-stretch">
              {/* 左侧预览：原图是 .mp3/.ogg/.wav 这类不能 <img>，所以用 <audio controls>。
               域外先在浏览器里建好 objectURL（staged.music.url），随机命中即可播。 */}
              <div className="sm:w-[180px] shrink-0">
                <div className="w-full h-full min-h-[112px] rounded-xl bg-white/5 border border-white/10 flex flex-col items-center justify-center gap-2 px-3 py-3">
                  <Music className="w-7 h-7 text-zinc-500" />
                  {staged.music ? (
                    <audio
                      src={staged.music.url}
                      controls
                      preload="metadata"
                      className="w-full max-w-full"
                      style={{ height: 32 }}
                    />
                  ) : musicCustom && !musicFailed ? (
                    <audio
                      src={`${gameMusicUrl(tid)}&v=${musicVer}`}
                      controls
                      preload="metadata"
                      className="w-full max-w-full"
                      style={{ height: 32 }}
                      onError={() => setMusicFailed(true)}
                    />
                  ) : (
                    <p className="panel-micro text-zinc-500 text-center">
                      {musicCustom ? t('game.music_load_fail', '加载失败，点右侧「恢复默认」重置') : t('game.music_empty', '尚未设置')}
                    </p>
                  )}
                  {staged.music && (
                    <Chip tone="warn">{t('game.tile_staged', '待应用')}</Chip>
                  )}
                </div>
              </div>
              {/* 右侧控件区 */}
              <div className="min-w-0 flex-1 flex flex-col gap-3">
                <p className="panel-note text-zinc-400">
                  {t('game.music_desc', '为这款游戏设置背景音乐（磁贴在主屏预览时会播放）。')}
                </p>
                <div className="flex flex-wrap items-center gap-2">
                  {staged.music ? (
                    <>
                      <button type="button" className="btn btn-primary flex-1" disabled={musicBusy} aria-label={applyLabel('music')} onClick={() => applyStaged('music')}>
                        <Check className="w-4 h-4" />
                        <span>{musicBusy ? t('game.uploading', '处理中…') : t('game.tile_apply', '应用')}</span>
                      </button>
                      <button type="button" className="btn btn-ghost" disabled={musicBusy} aria-label={cancelLabel('music')} onClick={() => cancelStaged('music')}>
                        <X className="w-4 h-4" />
                        <span>{t('game.tile_cancel', '取消')}</span>
                      </button>
                    </>
                  ) : (
                    <>
                      <button type="button" className="btn btn-outline flex-1" disabled={musicBusy} onClick={() => pickImage('music')}>
                        <Upload className="w-4 h-4" />
                        <span>{musicBusy ? t('game.uploading', '处理中…') : t('game.music_upload', '上传音乐')}</span>
                      </button>
                      <button type="button" className="btn btn-ghost" disabled={musicBusy || !musicCustom} onClick={() => onResetImage('music')}>
                        <RotateCcw className="w-4 h-4" />
                        <span>{t('game.bg_reset_btn', '恢复默认')}</span>
                      </button>
                    </>
                  )}
                </div>
                {staged.music && (
                  <p className="panel-tiny text-amber-400">
                    {t('game.tile_apply_hint', '已选好新文件，点「应用」后才会写入主机。')}
                  </p>
                )}
                <p className="panel-tiny text-zinc-500">
                  {t('game.music_limit', `支持 MP3 / OGG / WAV，体积上限 ${MUSIC_SIZE_LIMIT_MB} MB。`)}
                </p>
              </div>
            </div>
          </Block>
        </div>

        {/* ── ③ 磁贴背景：整图下方整幅（与原版一致，沿用 v.114 的左右分栏 + 上限）──
            保留版式沿革说明：v.114 起就是「图左 / 按钮 + 说明右」的对称结构，
            这次挪到下方不变 —— 上下两块各自有清晰的语义：上半部是「身份 + 内容」
            （图标 / 名称 / 音乐），下半部是「氛围图」（磁贴背景）。 */}
        <Block
          title={t('game.bg_block', '磁贴背景')}
          icon={ImagePlus}
          actions={<span className="panel-tiny text-zinc-500">{bgCustom ? t('game.bg_custom', '自定义背景') : t('game.bg_default', '默认底色')}</span>}
        >
          <div className="flex flex-col sm:flex-row gap-4">
            <div className="sm:flex-1 min-w-0">
              <button
                type="button"
                onClick={() => pickImage('bg')}
                disabled={bgBusy}
                aria-label={t('game.bg_pick', '更换磁贴背景')}
                title={t('game.tile_pick_hint', '点图片也可以选图')}
                className="group relative w-full aspect-video max-h-[52vh] rounded-xl overflow-hidden bg-white/5 border border-white/10 hover:border-white/30 transition-colors flex items-center justify-center"
              >
                <ImagePlus className="w-9 h-9 text-zinc-600 absolute" />
                {showBg && (
                  <img
                    src={bgSrc}
                    alt=""
                    className="relative w-full h-full object-contain"
                    onError={() => setBgFailed(true)}
                  />
                )}
                <span className="absolute inset-x-0 bottom-0 py-1.5 panel-micro font-semibold text-white bg-black/65 opacity-0 group-hover:opacity-100 transition-opacity">
                  {t('game.tile_pick_hint', '点图片也可以选图')}
                </span>
                {staged.bg && (
                  <Chip tone="warn" className="absolute top-2 left-2">{t('game.tile_staged', '待应用')}</Chip>
                )}
              </button>
              <p className="panel-micro text-zinc-500 mt-1.5 text-center">{t('game.bg_preview', '背景预览（16:9）')}</p>
            </div>
            <div className="sm:w-[340px] sm:shrink-0 min-w-0 flex flex-col gap-3">
              <div className="flex flex-wrap items-center gap-2">
                {staged.bg ? (
                  <>
                    <button type="button" className="btn btn-primary" disabled={bgBusy} aria-label={applyLabel('bg')} onClick={() => applyStaged('bg')}>
                      <Check className="w-4 h-4" />
                      <span>{bgBusy ? t('game.uploading', '处理中…') : t('game.tile_apply', '应用')}</span>
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={bgBusy} aria-label={cancelLabel('bg')} onClick={() => cancelStaged('bg')}>
                      <X className="w-4 h-4" />
                      <span>{t('game.tile_cancel', '取消')}</span>
                    </button>
                  </>
                ) : (
                  <>
                    <button type="button" className="btn btn-outline" disabled={bgBusy} onClick={() => pickImage('bg')}>
                      <Upload className="w-4 h-4" />
                      <span>{bgBusy ? t('game.uploading', '处理中…') : t('game.bg_upload', '上传图片')}</span>
                    </button>
                    <button type="button" className="btn btn-ghost" disabled={bgBusy || !bgCustom} onClick={() => onResetImage('bg')}>
                      <RotateCcw className="w-4 h-4" />
                      <span>{t('game.bg_reset_btn', '恢复默认')}</span>
                    </button>
                  </>
                )}
              </div>
              {staged.bg && (
                <p className="panel-tiny text-amber-400">
                  {t('game.tile_apply_hint', '已选好新图，点「应用」后才会写入主机。')}
                </p>
              )}
              <p className="panel-note text-zinc-400">
                {t('game.bg_desc', '整张卡片的底图。上传后由主机转成 1920×1080 的 DDS；在磁贴上图标压在它上面，所以它只在图标四周与文字区露出来。')}
              </p>
              <p className="panel-tiny text-zinc-500">
                {t('game.bg_limit', '支持 PNG / JPEG，上传前会自动缩到 1920×1080（16:9 居中裁切）。')}
              </p>
            </div>
          </div>
        </Block>

        {/* 三个隐藏的文件选择器。带 name 不只是语义正确：它们是 hidden 的，自动化测试
            挑不出「哪一个对应哪一层」，靠 DOM 顺序取下标会在以后调换板块时静默挑错。
            走 `input[name="tile_icon" / tile_bg / tile_music]` 就与板块标题一一对应了。 */}
        <input ref={iconFileRef} name="tile_icon" type="file" accept="image/*" className="hidden" onChange={(e) => onImageFile('icon', e)} />
        <input ref={bgFileRef} name="tile_bg" type="file" accept="image/*" className="hidden" onChange={(e) => onImageFile('bg', e)} />
        {/* [CN-FORK] v0.5.2.163 音乐 accept 用 audio/*；前端 mime 类型不全（部分手机标成 octet-stream），
            所以真正判定走 file.type 与扩展名的 OR，后端再做兜底。 */}
        <input ref={musicFileRef} name="tile_music" type="file" accept="audio/mpeg,audio/mp3,audio/ogg,audio/wav" className="hidden" onChange={(e) => onImageFile('music', e)} />
      </div>
    </Modal>
  )
}
