import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Gamepad2, RefreshCw, Info, ExternalLink } from 'lucide-react'
import Section from '../ui/Section' /* [CN-FORK] v0.5.2.94 板块形状统一 */
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'
import { cn } from '../../utils/helpers'
import { formatSize } from '../game/format'
/* [CN-FORK] v0.5.2.128 磁贴下的四颗动作按钮（存档/多媒体/改名/待定）按用户要求全部下线，
 * 只留一颗跳 ShadowMount 管理后台的按钮，所以 game/GameActionIcons.jsx 的四个图标本文件不再引用
 * （文件本身保留：图标设计说明还有参考价值，别删）。 */
import GameDetailModal from '../game/GameDetailModal'
import SaveManagerModal from '../game/SaveManagerModal'
import MediaManagerModal from '../game/MediaManagerModal'
import RenameModal from '../game/RenameModal'
import ComingSoonModal from '../game/ComingSoonModal'

/* [CN-FORK] 游戏管理板块：读 /game_list 展示「已安装（app.db）+ 已存在（扫描 dump）」游戏。
 * 进页面拉一次 + 手动刷新，不轮询——app.db 全库读 + 目录扫描是有成本的 IO，后端已用
 * mutex + 60s 快照压住频率，这里再叠轮询只会放大风险（参考 /system_stats 死机教训）。
 * 图标走独立 /game_icon?tid= 端点读 icon0.png；读不到（onError）回退占位图。
 *
 * [CN-FORK] v0.5.2.112 磁贴交互落地（用户口径：「磁贴点开要显示游戏详情，包括删除功能」+
 * 「四个按钮图标全部重做」+ 四颗按钮各自的功能）：
 *   · 磁贴**整块可点** → 游戏详情。实现上用一张铺满卡片的**透明按钮**做热区，而不是给卡片加
 *     onClick —— 卡片里还有一颗真按钮，把 onClick 挂在外层 div 上就成了「按钮套按钮」，
 *     读屏会念错、遥控器焦点也会错乱。
 *   · 图拆成**两层**：封面（icon，列表项带 icon 时优先）压在最上面，整卡底图（bg）在它下面。
 *     见下方 GameCard 里 iconSrc / bgSrc 两处注释。
 * [CN-FORK] v0.5.2.128 磁贴下只剩一颗「去 ShadowMount 管理」按钮（跳 10101 后台）。四颗动作按钮
 * 下线的原因见 CHANGELOG 的 .128。
 *
 * 为什么四个弹窗都挂在 GameSection 这一层而不是各自挂在卡片里：卡片是列表项，一屏可能有几十个，
 * 每个卡片挂五个弹窗实例 = 几百个空组件。收在板块层只留一份，靠 modal.kind 切换。
 * （存档/多媒体/名称与背景三个弹窗现在只能从**游戏详情**跳 —— GameDetailModal 的 onOpenSaves /
 * onOpenMedia / onOpenRename，见那边的三行 GridRow。它们跟挂载/卸载无关，ShadowMount 也不提供，
 * 所以撤磁贴按钮的同时在详情里补了入口，不能一起弄丢。pending 那颗占位弹窗则彻底没了入口。）
 */

/* [CN-FORK] 由 TITLE_ID 前缀判断主机平台（后端 gm_tid_is_game 白名单即这几类前缀）。 */
function gamePlatform(tid) {
  const p = String(tid || '').slice(0, 4).toUpperCase()
  if (p === 'PPSA' || p === 'EMUA' || p === 'PAUS') return 'PS5'
  if (p === 'CUSA' || p === 'PPJB') return 'PS4'
  return ''
}
/* 格式标签：与 ShadowMountPlus 一致的真实格式集。后端 /game_list 的 fmt 取值：
 * pkg（内置安装）/ ffPKG / exFAT / ffpfs / ffpfsc（镜像挂载）/ folder（裸目录）/ lz4（ampr 实时解压目录）；缺字段时按 src 回退。
 * [CN-FORK] v0.5.2.103 徽标收敛：取色从「饱和实心底」改为「深色芯片上的同色字」，
 * 所以这里的 c 现在是**文字/描边色**（提亮一档，压在近黑芯片上要读得清），不再是底色。
 * 原值（#059669/#0891b2/#d97706/#7c3aed/#db2777/#4f46e5）作实心底用，做文字偏暗。 */
const FMT_LABEL = {
  pkg: { k: 'game.fmt_pkg', d: 'PKG', c: '#3fb950' },
  ffpkg: { k: 'game.fmt_ffpkg', d: 'ffPKG', c: '#39c5cf' },
  exfat: { k: 'game.fmt_exfat', d: 'exFAT', c: '#e3b341' },
  ffpfs: { k: 'game.fmt_ffpfs', d: 'ffpfs', c: '#a371f7' },
  ffpfsc: { k: 'game.fmt_ffpfsc', d: 'ffpfsc', c: '#f778ba' },
  folder: { k: 'game.fmt_folder', d: '文件夹', c: '#8b949e' },
  lz4: { k: 'game.fmt_lz4', d: 'LZ4', c: '#7c8cff' },
}
/* 平台徒标颜色：PS5 蓝、PS4 紫（同样是文字色，不是底色）。 */
const PLAT_COLOR = { PS5: '#58a6ff', PS4: '#a371f7' }
/* 统一尺寸的徒标样式（固定宽高 + 居中）。
 * [CN-FORK] v0.5.2.103 收敛：原来是「饱和实心底 + 白字」，一屏最多同时出现 8 种饱和色块
 * （平台 2 色 + 格式 7 色 + 来源 2 色），是游戏页最吵的一处。现在统一成**深色芯片**：
 * 底一律压暗（盖在任意封面上都可读，两种主题都不用翻），色相只由字与那根发丝线传达。
 * 这是 GitHub label 的成熟做法 —— 语义一个不少，视觉噪声降一个数量级。 */
const BADGE =
  'inline-flex items-center justify-center w-[3.25rem] h-[18px] rounded-md text-[10px] font-semibold leading-none tracking-wide border'
const CHIP_BG = 'rgba(8, 10, 14, 0.72)'
const chipStyle = (c) => ({ color: c, borderColor: `${c}59`, backgroundColor: CHIP_BG })

/* ShadowMount 后端固定监听 10101（Web UI 直接在该端口根路径，ShadowMount Library）。
 * 端口不是我们的面板 8911，写成一个常量：将来它改端口只改这一处。 */
const SMP_PORT = 10101

/* 单个游戏磁贴（宫格）：大图标铺满 1:1 + 名称/TID，来源徽标叠在图标角上，
 * 图标下排只有一颗跳 ShadowMount 管理后台的按钮。整块可点开详情（见文件头注释）。 */
function GameCard({ g, iconFailed, onIconError, onOpen }) {
  const { t } = useTranslation()
  const installed = g.src === 'installed'
  const platform = gamePlatform(g.tid)
  const fmtInfo = FMT_LABEL[g.fmt] || FMT_LABEL[installed ? 'pkg' : 'folder']
  const fmtLabel = t(fmtInfo.k, fmtInfo.d)
  const sizeLabel = formatSize(g.size)
  const platColor = PLAT_COLOR[platform] || '#475569'
  /* [CN-FORK] v0.5.2.112 磁贴上的图**拆成两层**（用户：「还要增加一个修改背景的」）：
   *   · icon —— 封面那一格方图。有自定义（列表项带 icon 版本号）就用 /game_tile_icon，
   *     否则回默认 icon0.png。没有版本号时**不去白跑一次 404**。
   *   · bg   —— 整张卡片的底图。有自定义（列表项带 bg 版本号）才渲染这一层。
   * 两个都带 v=<版本号> 破缓存：换完图列表会刷新，版本号一变 URL 就变，
   * 否则 PS5 的老内核会一直拿缓存里的旧图。 */
  const iconSrc = g.icon
    ? `/game_tile_icon?tid=${encodeURIComponent(g.tid)}&v=${encodeURIComponent(g.icon)}`
    : `/game_icon?tid=${encodeURIComponent(g.tid)}`
  const bgSrc = g.bg
    ? `/game_tile_bg?tid=${encodeURIComponent(g.tid)}&v=${encodeURIComponent(g.bg)}`
    : ''

  /* [CN-FORK] v0.5.2.128 唯一的磁贴动作：跳 ShadowMount 后端（ShadowMount Library 直接挂在
   * 10101 端口根路径）。host 用当前页面的 host：在主机上从 127.0.0.1:8911 打开面板时它自动是
   * 127.0.0.1，从电脑用 192.168.x.x 打开时就是局域网地址，两边都不需要配置。
   * 先开 about:blank 再把 URL 塞过去：PS5 老内核里 window.open(url) 只要不在点击手势的同步栈里
   * 就会被拦（参考 .84 那次的修法），同步开空窗最稳；空窗都没开出来就退化成直接开 url。
   *
   * 这里**不能**带 'noopener,noreferrer' —— 那样 window.open 会直接返回 null，于是落到下面的
   * 兜底分支**再开一个**标签，用户一次点出两个 tab（第一颗还停在 about:blank）。回归实测出的
   * 就是这个症状。代价是这个新 tab 能反向拿到我们的 window —— 但它只是 ShadowMount 自家后端
   * （同机 127.0.0.1 / 局域网），跟 .84 那条 CommonEntriesView 的做法一致，可接受。 */
  const openSmp = () => {
    const raw = window.location.hostname || '127.0.0.1'
    const host = raw.includes(':') ? `[${raw}]` : raw
    const url = `http://${host}:${SMP_PORT}/`
    const win = window.open('about:blank', '_blank')
    if (win) {
      try {
        win.location.replace(url)
        win.focus()
      } catch {
        try { win.close() } catch { /* 关不掉也没关系，下面这个兜底才是真正打开它的那个 */ }
        window.open(url, '_blank')
      }
    } else {
      window.open(url, '_blank')
    }
  }

  return (
    <div className="relative bg-black/20 border border-white/10 rounded-xl p-1.5 flex flex-col space-y-1.5">
      {/* 背景层（整卡底图）。放在详情热区**之前**：两者都是 inset-0，DOM 靠前的先画，
          于是热区仍然稳稳压在它上面 —— 否则点背景那一片就点不开详情了。
          没有自定义背景时整层不存在（不是「给个默认渐变」）：两种状态必须一眼分得出来。
          压暗层的数值与 RenameModal 的整卡预览完全一致，那边要 WYSIWYG。
          onError 直接把整层从显示里摘掉：图 404 时留着那层压暗渐变会变成一块纯黑卡片，
          比什么都不显示更糟。React 不会覆盖这个 style（本元素没有 style prop，前后都是 undefined）。 */}
      {bgSrc && (
        <div className="absolute inset-0 rounded-xl overflow-hidden pointer-events-none" aria-hidden="true">
          <img
            src={bgSrc}
            alt=""
            loading="lazy"
            className="w-full h-full object-cover"
            onError={(e) => { const p = e.currentTarget.parentElement; if (p) p.style.display = 'none' }}
          />
          <div
            className="absolute inset-0"
            style={{ background: 'linear-gradient(180deg, rgba(6,8,12,.32) 0%, rgba(6,8,12,.58) 55%, rgba(6,8,12,.86) 100%)' }}
          />
        </div>
      )}

      {/* 详情热区：铺满整张卡片的透明按钮。
       * 用真 <button> 而不是 div+onClick，是为了让它天然进入 tab 序列与空间导航 ——
       * 遥控器上「选中磁贴」这一步必须存在，否则用户没法进详情。
       * 焦点环画在整张卡片外沿（.is-focus 的 outline-offset），正是想要的效果。 */}
      <button
        type="button"
        onClick={() => onOpen(g)}
        aria-label={t('game.open_detail', '查看 {{name}} 的详情', { name: g.name || g.tid })}
        className="absolute inset-0 z-0 rounded-xl"
      />

      {/* 内容层：pointer-events-none 让点击穿透到下面的详情热区，
          只有四颗动作按钮显式恢复可点（pointer-events-auto）。 */}
      <div className="relative w-full aspect-square rounded-lg overflow-hidden bg-white/5 border border-white/10 flex items-center justify-center pointer-events-none">
        {iconFailed ? (
          <Gamepad2 className="w-12 h-12 text-zinc-600" />
        ) : (
          <img
            src={iconSrc}
            alt=""
            loading="lazy"
            className="w-full h-full object-cover"
            onError={onIconError}
          />
        )}
        {/* [CN-FORK] 左上角竖排徒标：上=平台(PS4/PS5)、下=格式。两枚统一尺寸（固定宽高、居中），
         * 用内联 style 上色：既绕开 Tailwind JIT 可能没生成任意色类，也避开浅色主题把
         * .text-white 洗成近黑的问题（这里根本不用 text-white 类）。
         * v0.5.2.103 起底一律是压暗芯片，色相只由字与线给（见上方 BADGE 注释）。 */}
        <div className="absolute top-1.5 left-1.5 flex flex-col items-start space-y-1">
          {platform && (
            <span
              className={BADGE}
              style={chipStyle(platColor)}
              title={platform === 'PS4' ? t('game.plat_ps4', 'PS4 游戏') : t('game.plat_ps5', 'PS5 游戏')}
            >
              {platform}
            </span>
          )}
          <span
            className={BADGE}
            style={chipStyle(fmtInfo.c)}
            title={`${t('game.fmt', '格式')}: ${fmtLabel}`}
          >
            {fmtLabel}
          </span>
        </div>
        <span
          className="absolute top-1.5 right-1.5 text-[10px] font-semibold px-1.5 py-0.5 rounded-md border"
          style={chipStyle(installed ? '#3fb950' : '#e3b341')}
          title={installed ? t('game.src_installed', '已安装') : t('game.src_dump', '转储/外挂')}
        >
          {installed ? t('game.badge_installed', '已安装') : t('game.badge_dump', '转储')}
        </span>
        {/* [CN-FORK] v0.5.2.128 磁贴下的四颗动作按钮（存档/多媒体/改名/待定）按用户要求全部下线，
         * 换成一颗「去 ShadowMount 管理」：点它跳 ShadowMount 后端（10101）自己管挂载/卸载。
         * 原因写在 CHANGELOG 的 .128 里 —— 卸载按钮会把 ShadowMount 的文件夹游戏从注册表里删掉
         * 却又挂不回，所以干脆把挂载类的入口都交给 ShadowMount，自己不再碰注册表。
         * 仍是 z-10 + pointer-events-auto：压在详情热区之上，点按钮不会顺带打开详情。
         * 单按钮不写 grid 了：一个子元素自己撑满 inset-x-1.5，省一层 DOM。 */}
        <button
          type="button"
          onClick={openSmp}
          title={t('game.smp_open', '去 ShadowMount 管理')}
          aria-label={t('game.smp_open', '去 ShadowMount 管理')}
          className="absolute inset-x-1.5 bottom-1.5 flex items-center justify-center gap-1.5 py-1.5 rounded-md z-10 pointer-events-auto
            bg-black/55 border border-white/15 backdrop-blur-sm text-white
            transition-colors hover:bg-black/80 hover:border-white/35"
        >
          <ExternalLink className="w-4 h-4 shrink-0" />
          <span className="text-[12px] font-semibold truncate">
            {t('game.smp_open', '去 ShadowMount 管理')}
          </span>
        </button>
      </div>

      {/* 名称 + TID（行末右侧标游戏体积）+ 详情提示 */}
      {/* [CN-FORK] v0.5.2.119 名称+体积行加半透明背景条，突出显示 */}
      <div className="space-y-0.5 min-w-0 pointer-events-none bg-white/[0.04] rounded-md px-2 py-1 -mx-0.5">
        <p className="text-[14px] font-bold text-white truncate" title={g.name}>{g.name}</p>
        <div className="flex items-baseline justify-between">
          <span className="text-[11px] text-zinc-500 font-mono tracking-wider truncate">{g.tid}</span>
          {sizeLabel && (
            <span
              className="shrink-0 ml-2 text-[11px] font-semibold text-zinc-400 tabular-nums"
              title={`${t('game.size', '体积')}: ${sizeLabel}`}
            >
              {sizeLabel}
            </span>
          )}
        </div>
      </div>
    </div>
  )
}

export default function GameSection({ addToast, showConfirm }) {
  const { t } = useTranslation()
  const [games, setGames] = useState(null) // null=未加载；[]=已加载但无游戏
  const [count, setCount] = useState(0)
  const [truncated, setTruncated] = useState(false)
  const [loading, setLoading] = useState(false)
  const [err, setErr] = useState('')
  const [failedIcons, setFailedIcons] = useState({})
  /* 五个弹窗共用一个 state：{kind, game}。kind ∈ detail|saves|media|rename|pending。
   * 用对象而不是五个布尔，是因为「从详情里点『存档 3 个』跳到存档弹窗」需要在同一个游戏上
   * 换 kind —— 五个布尔得同时改两个，很容易出现两个弹窗叠在一起。 */
  const [modal, setModal] = useState(null)

  const load = useCallback(async () => {
    setLoading(true)
    setErr('')
    try {
      const res = await fetch(`/game_list?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data && Array.isArray(data.games)) {
        setGames(data.games)
        setCount(typeof data.count === 'number' ? data.count : data.games.length)
        setTruncated(!!data.truncated)
        setFailedIcons({})
      } else {
        setErr(t('game.read_fail', '读取失败'))
      }
    } catch {
      setErr(t('game.read_fail', '读取失败'))
    }
    setLoading(false)
  }, [t])

  /* 进页面拉一次 + 手动刷新，不轮询（原因见文件头）。
   * react-hooks/set-state-in-effect 对「effect 里同步调用一个内含 setState 的函数」一律报错，
   * 而它不区分 setState 到底在 await 之前还是之后（实测：把 load 包成 async 才会被放行）。
   * 拉数据本来就要 setLoading(true)，这里显式豁免并写明理由，而不是把代码改成它认的写法。 */
  useEffect(() => {
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 进页面/刷新时拉一次列表
    load()
  }, [load])

  const list = games || []

  /* [CN-FORK] v0.5.2.128 磁贴下的四颗动作按钮下线后，「从磁贴直开弹窗」这条入口就没了：
   * saves / media / rename 改由游戏详情跳过去（GameDetailModal 的 onOpenSaves / onOpenMedia /
   * onOpenRename，见 GameDetailModal.jsx 里那三行 GridRow）—— 这三个功能和挂载/卸载无关，
   * ShadowMount 也不提供，不该因为撤了磁贴按钮就跟着消失。
   * pending 那颗本来就是个「还没定做什么」的占位弹窗，现在连入口都没有了（实例还挂着，但
   * modal.kind 永远不会变成 'pending'，等于惰性死代码 —— 真删的话要连弹窗组件一起摘，不值当）。
   * 所以这块 openAction 分发整个删掉了，四个弹窗实例保留（见文件头注释）。 */

  const closeModal = useCallback(() => setModal(null), [])

  return (
    <>
      <Section
        id="game-list"
        icon={Gamepad2}
        title={t('game.list_title', '游戏列表')}
        count={games ? t('game.total', '共 {{n}} 款', { n: count }) : undefined}
        bodyClass="space-y-4"
        actions={
          <button onClick={load} disabled={loading} title={t('common.refresh', '刷新')} aria-label={t('common.refresh', '刷新')} className="icon-btn">
            <RefreshCw className={cn(loading && 'animate-spin')} />
          </button>
        }
      >
        {/* [CN-FORK] v0.5.2.94 页级大标题与说明上移到 <PageHead>（GameManageView），本块降为页内板块：
            原来这里既是页头又是板块，同一屏出现两个 24px 标题，且页头不粘顶。 */}
        {truncated && (
          <p className="text-[12px] text-amber-400">{t('game.truncated', '列表过大，已截断显示')}</p>
        )}
        {err && <p className="text-[13px] text-red-400">{err}</p>}

        {games === null && !err ? (
          <LoadingState label={t('game.loading', '正在读取游戏列表…')} />
        ) : list.length === 0 ? (
          <EmptyState icon={Gamepad2} title={t('game.empty', '未发现已安装或已存在的游戏')} />
        ) : (
          <>
            {/* 磁贴可点开详情这件事没有任何视觉暗示的话，用户不会去点。
                一行说明比在每个磁贴上盖一个「详情」角标省地方得多。 */}
            <p className="text-[11px] text-zinc-500 flex items-center gap-1.5">
              <Info className="w-3.5 h-3.5 shrink-0" />
              <span>{t('game.tile_hint', '点磁贴查看游戏详情；磁贴底部那颗按钮跳转到 ShadowMount 后台（10101）继续管理。')}</span>
            </p>
            {/* [CN-FORK] v0.5.2.100 用户指令「磁贴再小三分之一，变成自适应，屏幕宽就多放几个、
               窄就少放几个，不要固定四个」：原来是 grid-cols-2 / sm:3 / lg:4，宽屏永远卡在 4 列，
               一块磁贴 ≈355px。现在列宽下限交给 --tile-w（PC 262.5 / 电视 375，v0.5.2.114 又 ×1.25），
               列数由容器自己算。 */}
            <div className="tile-grid">
              {list.map((g) => (
                <GameCard
                  key={g.tid}
                  g={g}
                  iconFailed={!!failedIcons[g.tid]}
                  onIconError={() => setFailedIcons((m) => ({ ...m, [g.tid]: true }))}
                  onOpen={(x) => setModal({ kind: 'detail', game: x })}
                />
              ))}
            </div>
          </>
        )}
      </Section>

      {/* 五个弹窗收在这一层（见文件头注释：不放进卡片里）。
          全部传 show={modal?.kind === 'x'}，同一时刻只有一个可见。 */}
      <GameDetailModal
        show={modal?.kind === 'detail'}
        game={modal?.game}
        onClose={closeModal}
        addToast={addToast}
        showConfirm={showConfirm}
        onChanged={load}
        onOpenSaves={(g) => setModal({ kind: 'saves', game: g })}
        onOpenMedia={(g) => setModal({ kind: 'media', game: g })}
        onOpenRename={(g) => setModal({ kind: 'rename', game: g })}
      />
      <SaveManagerModal
        show={modal?.kind === 'saves'}
        game={modal?.game}
        onClose={closeModal}
        addToast={addToast}
        showConfirm={showConfirm}
      />
      <MediaManagerModal
        show={modal?.kind === 'media'}
        game={modal?.game}
        onClose={closeModal}
        addToast={addToast}
        showConfirm={showConfirm}
      />
      <RenameModal
        show={modal?.kind === 'rename'}
        game={modal?.game}
        onClose={closeModal}
        addToast={addToast}
        onChanged={load}
      />
      <ComingSoonModal
        show={modal?.kind === 'pending'}
        game={modal?.game}
        onClose={closeModal}
      />
    </>
  )
}
