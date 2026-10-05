import React, { useState, useEffect, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { Type, ImagePlus, RotateCcw, Upload, AlertTriangle, Check, Gamepad2, X } from 'lucide-react'
import Modal from '../ui/Modal'
import { Block, Chip, Note } from './parts'
import {
  renameGame, setTileIcon, resetTileIcon, tileIconUrl,
  setTileBackground, resetTileBackground, tileBackgroundUrl,
} from '../../utils/gameApi'

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
  /* 两层各自的「待应用」图：{ blob, url } | null。选了文件先落在这里，
   * 预览走本地 objectURL（此时还没碰过主机），点「应用」才 uploadTile。
   * 两层的键名与 pickImage / tile 接口同一套命名，免得再加第三层时要现起名字。 */
  const [staged, setStaged] = useState({ icon: null, bg: null })
  const [err, setErr] = useState('')
  const iconFileRef = useRef(null)
  const bgFileRef = useRef(null)
  const aliveRef = useRef(true)
  /* 暂存图的 objectURL 必须有人回收：它们不归浏览器缓存管，组件卸载后还会把整张
   * 1920×1080 的位图钉在内存里（电视端内存尤其紧）。
   * 回收**只有这一处**（见下面的同步 effect）：谁把 staged 里的某一层换掉或清掉，
   * 这里就顺手 revoke 掉那一层的旧 URL，于是「换一张」「应用」「取消」「恢复默认」
   * 「换游戏」「关弹窗」六条路径全都不用各自记得回收 —— 少记一处就是漏一处。 */
  const stagedRef = useRef({ icon: null, bg: null })

  useEffect(() => {
    aliveRef.current = true
    return () => {
      aliveRef.current = false
      const cur = stagedRef.current
      Object.keys(cur).forEach((k) => dropStagedUrl(cur[k]))
      stagedRef.current = { icon: null, bg: null }
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

  /* 打开 / 换游戏时把输入框、两层图的状态、错误全部归零。
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
    setErr('')
    setIconVer(0); setBgVer(0)
    setIconBusy(false); setBgBusy(false)
    setIconFailed(false); setBgFailed(false)
    setSavingName(false)
    /* 换游戏/关闭时把上一款游戏的暂存图丢掉（否则它会跟着预览显示到下一款上，
     * 用户会以为「这款游戏已经有自定义图了」）。 */
    setStaged({ icon: null, bg: null })
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
      await renameGame(tid, trimmed)
      addToast?.(t('game.renamed', '名称已改为「{{name}}」', { name: trimmed }))
      onChanged?.()
    } catch (e) {
      setErr(e.message || t('game.rename_fail', '改名失败'))
    }
    if (aliveRef.current) setSavingName(false)
  }

  /* 两层图共用一套「选文件 → 缩 → 暂存 → 应用」的流程，只有目标尺寸、接口与文案不同。
   * 抽成一条 pickImage / onImageFile / applyStaged 而不是两份 handler：
   * 将来加第三层（比如占位图）只加一行配置。 */
  const pickImage = (which) => {
    const ref = which === 'icon' ? iconFileRef : bgFileRef
    if (ref.current) { ref.current.value = ''; ref.current.click() }
  }

  /* 选文件 → 缩到目标尺寸 → **只暂存**，不碰主机。
   * 暂存期间也占 busy（缩图是异步的，用户得有反馈），缩完立刻放开。 */
  const onImageFile = async (which, ev) => {
    const file = ev.target.files && ev.target.files[0]
    if (!file) return
    if (!/^image\//.test(file.type || '')) { setErr(t('game.not_image', '请选择图片文件')); return }
    const setBusy = which === 'icon' ? setIconBusy : setBgBusy
    setBusy(true)
    setErr('')
    try {
      const blob = which === 'icon' ? await fitCover(file, ICON_SIZE, ICON_SIZE) : await fitCover(file, BG_W, BG_H)
      if (!aliveRef.current) return
      let url = ''
      try { url = URL.createObjectURL(blob) } catch { /* 老内核万一不支持 objectURL，退化成没有预览：仍可应用 */ }
      /* 换一张就把上一张顶掉 —— 旧 URL 由上面那条同步 effect 回收，这里不重复写一遍。 */
      setStaged((s) => ({ ...s, [which]: { blob, url } }))
    } catch {
      if (aliveRef.current) setErr(t('game.stage_fail', '读取这张图片失败，请换一张试试'))
    }
    if (aliveRef.current) setBusy(false)
  }

  /* 「应用」= 真正提交到主机。失败时**保留暂存图**，用户可以再点一次（网络抖一下不至于要重选）。 */
  const applyStaged = async (which) => {
    const cur = stagedRef.current[which]
    if (!cur) return
    const setBusy = which === 'icon' ? setIconBusy : setBgBusy
    setBusy(true)
    setErr('')
    try {
      if (which === 'icon') {
        await setTileIcon(tid, cur.blob)
        if (!aliveRef.current) return
        setIconCustom(true); setIconFailed(false); setIconVer((v) => v + 1)
        addToast?.(t('game.icon_saved', '磁贴图标已应用'))
      } else {
        await setTileBackground(tid, cur.blob)
        if (!aliveRef.current) return
        setBgCustom(true); setBgFailed(false); setBgVer((v) => v + 1)
        addToast?.(t('game.bg_saved', '磁贴背景已应用'))
      }
      setStaged((s) => ({ ...s, [which]: null }))
      onChanged?.()
    } catch (e) {
      if (aliveRef.current) {
        setErr(e.message || t(which === 'icon' ? 'game.icon_fail' : 'game.bg_fail', which === 'icon' ? '设置图标失败' : '设置背景失败'))
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
    const setBusy = which === 'icon' ? setIconBusy : setBgBusy
    setBusy(true)
    setErr('')
    try {
      if (which === 'icon') {
        await resetTileIcon(tid)
        if (!aliveRef.current) return
        setIconCustom(false); setIconFailed(false); setIconVer((v) => v + 1)
        addToast?.(t('game.icon_reset', '已恢复默认图标'))
      } else {
        await resetTileBackground(tid)
        if (!aliveRef.current) return
        setBgCustom(false); setBgFailed(false); setBgVer((v) => v + 1)
        addToast?.(t('game.bg_reset', '已恢复默认底色'))
      }
      /* 恢复默认连未应用的暂存图一起丢掉：留着的话预览仍是那张暂存图，
       * 用户会以为「恢复默认没生效」。 */
      setStaged((s) => ({ ...s, [which]: null }))
      onChanged?.()
    } catch (e) {
      if (aliveRef.current) {
        setErr(e.message || t(which === 'icon' ? 'game.icon_reset_fail' : 'game.bg_reset_fail', which === 'icon' ? '恢复默认图标失败' : '恢复默认底色失败'))
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

        {/* ── ①② 名称 + 图标**同一行**，背景另起一行的整幅（用户口径：
            「将磁贴图标与游戏名称放在一行，磁贴背景放在下面」）。
            名称与图标都只有一点点内容（一个输入框 / 一张方图），各占半屏才不至于一行只有半截。
            背景是 16:9 的整幅图，挤在半屏里只能缩到很小，所以给它整行。 */}
        <div className="grid grid-cols-1 xl:grid-cols-2 gap-5 items-start">
          {/* ── ① 名称 ── */}
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
            {/* 把「只改显示名」写在界面上，不是只写在注释里 —— v0.5.2.111 的账号 ID 事故就是
                因为「名字」和「身份」的关系没被说清楚。 */}
            <p className="panel-tiny text-zinc-500 mt-2">
              {t('game.rename_hint', '只修改显示名称，不会改动标题 ID（{{tid}}），也不影响存档与启动。', { tid })}
            </p>
          </Block>

          {/* ── ② 图标 ── */}
          <Block
            className="min-w-0"
            title={t('game.icon_block', '磁贴图标')}
            icon={Gamepad2}
            actions={<span className="panel-tiny text-zinc-500">{iconCustom ? t('game.icon_custom', '自定义图标') : t('game.icon_default', '默认图标')}</span>}
          >
            <div className="flex flex-col sm:flex-row gap-4">
              <div className="w-[8.25rem] shrink-0">
                {/* 预览图本身就是选图入口（用户口径：「这里点击也可以上传图片」）。
                    做成 <button> 而不是给 div 挂 onClick：遥控器与键盘要能走到它、
                    要能读出它是干什么的，这两件事只有真按钮天然具备。 */}
                <button
                  type="button"
                  onClick={() => pickImage('icon')}
                  disabled={iconBusy}
                  aria-label={t('game.icon_pick', '更换磁贴图标')}
                  title={t('game.tile_pick_hint', '点图片也可以选图')}
                  className="group relative w-full aspect-square rounded-xl overflow-hidden bg-white/5 border border-white/10 hover:border-white/30 transition-colors flex items-center justify-center"
                >
                  <Gamepad2 className="w-9 h-9 text-zinc-600 absolute" />
                  {showIcon && (
                    <img
                      src={iconSrc}
                      alt=""
                      className="relative w-full h-full object-cover"
                      onError={() => setIconFailed(true)}
                    />
                  )}
                  {/* 悬停浮出「换图」提示。电视端没有 hover，所以它只是补充，
                      真正的入口说明写在下方的图注与按钮上。 */}
                  <span className="absolute inset-x-0 bottom-0 py-1.5 panel-micro font-semibold text-white bg-black/65 opacity-0 group-hover:opacity-100 transition-opacity">
                    {t('game.tile_pick_hint', '点图片也可以选图')}
                  </span>
                  {staged.icon && (
                    <Chip tone="warn" className="absolute top-1 left-1">{t('game.tile_staged', '待应用')}</Chip>
                  )}
                </button>
                <p className="panel-micro text-zinc-500 mt-1.5 text-center">{t('game.icon_preview', '图标预览（1:1）')}</p>
              </div>
              <div className="min-w-0 flex-1">
                <p className="panel-note text-zinc-400">
                  {t('game.icon_desc', '替换游戏封面。按短边居中裁成 1:1，预览就是磁贴上看到的那一格。')}
                </p>
                {/* 按钮行随暂存态换一套：没选图时是「上传图片 / 恢复默认」，
                    选了图之后变成「应用 / 取消」—— 「上传」与「生效」是两步，界面就该是两步。 */}
                <div className="flex flex-wrap items-center gap-2 mt-3">
                  {staged.icon ? (
                    <>
                      <button type="button" className="btn btn-primary" disabled={iconBusy} aria-label={applyLabel('icon')} onClick={() => applyStaged('icon')}>
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
                      <button type="button" className="btn btn-outline" disabled={iconBusy} onClick={() => pickImage('icon')}>
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
                  <p className="panel-tiny text-amber-400 mt-2">
                    {t('game.tile_apply_hint', '已选好新图，点「应用」后才会写入主机。')}
                  </p>
                )}
                <p className="panel-tiny text-zinc-500 mt-2">
                  {t('game.icon_limit', '支持 PNG / JPEG，上传前会自动缩到 1024×1024 的 PNG。')}
                </p>
              </div>
            </div>
          </Block>
        </div>

        {/* ── ③ 磁贴背景：16:9 预览 + 按钮/说明同行 ──
            版式沿革：
              · v.112 第一版：图片 + 按钮 + 说明**左右分栏**（图片占一列、右侧上下堆按钮和说明）
              · v.112 第三轮：改成「预览独占一整行 → 按钮 → 说明」的**竖排**（用户当时说
                「把文字说明和按钮换位置，预览图片铺满一行」）—— 竖排的好处是预览能开到最大；
                坏处是整块变高，按钮得滚才能看到
              · v.114 现在这一版：又回到**左右分栏**（用户：「磁贴背景的按钮和说明放到图片右边」）
                —— 左侧图片独占、右侧上下放按钮和说明。**与图标块布局完全对称**（图标块一直是
                这个版式），两个块并排时视觉上不再一个横一个竖。
            高度上限（max-h）：竖排时给了 46vh（正文宽 ~1500 时图 ≈742px 宽），回到左右分栏后
            图在 65% 宽的栏里，容器宽约 985px；`aspect-video` 撑出 555px 高度仍然过大，
            所以继续带上限，只是放宽到 52vh（≈468px）—— 相对竖排那版「相对放大 25%」
            （46 → 57.5 的绝对值 52vh 是折中，因为**分栏后图已经比竖排时矮**，绝对上限抬到
            52vh 让**面积**大约回到竖排那版 ×1.25）。
            上限一生效容器不再是严格 16:9，所以图走 **object-contain**：宁可两侧留空也不能裁 ——
            这是让用户判断「这张图当底图好不好看」的东西，裁掉一半等于给错的信息。 */}
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

        {/* 两个隐藏的文件选择器。带 name 不只是语义正确：它们是 hidden 的，自动化测试
            挑不出「哪一个对应哪一层」，靠 DOM 顺序取下标会在以后调换板块时静默挑错。
            走 `input[name="tile_icon"]` 就与板块标题一一对应了。 */}
        <input ref={iconFileRef} name="tile_icon" type="file" accept="image/*" className="hidden" onChange={(e) => onImageFile('icon', e)} />
        <input ref={bgFileRef} name="tile_bg" type="file" accept="image/*" className="hidden" onChange={(e) => onImageFile('bg', e)} />
      </div>
    </Modal>
  )
}
