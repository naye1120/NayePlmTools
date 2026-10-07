import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Gamepad2, RefreshCw, Info, Tag } from 'lucide-react'
import Section from '../ui/Section' /* [CN-FORK] v0.5.2.94 板块形状统一 */
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'
import { cn } from '../../utils/helpers'
import { formatSize } from '../game/format'
import { syncTitleNames } from '../../utils/gameApi' /* [CN-FORK] v0.5.2.137 名字回填 */
import { SaveManagerIcon, MediaManagerIcon, RenameTileIcon, PendingSlotIcon } from '../game/GameActionIcons'
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
 *   · 磁贴**整块可点** → 游戏详情（`.129` 起详情只读，挂载 / 卸载 / 删除源全在 ShadowMount）。实现上用一张铺满卡片的
 *     **透明按钮**做热区，而不是给卡片加 onClick —— 卡片里还有四颗真按钮，把 onClick 挂在外层
 *     div 上就成了「按钮套按钮」，读屏会念错、遥控器焦点也会错乱。
 *   · 四颗按钮从「禁用占位」变成真入口，图标换成专用的四颗（见 game/GameActionIcons.jsx）。
 *   · 图拆成**两层**：封面（icon，列表项带 icon 时优先）压在最上面，整卡底图（bg）在它下面。
 *     见下方 GameCard 里 iconSrc / bgSrc 两处注释。
 *
 * 为什么四个弹窗都挂在 GameSection 这一层而不是各自挂在卡片里：卡片是列表项，一屏可能有几十个，
 * 每个卡片挂五个弹窗实例 = 几百个空组件。收在板块层只留一份，靠 modal.kind 切换。
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

/* 磁贴上的四颗按钮。顺序即语义顺序（存档 / 多媒体 / 改名 / 待定），
 * 图标是这一轮专门画的（见 game/GameActionIcons.jsx 顶部：为什么不再用 lucide 那四颗占位）。
 * 数组放在这里而不是图标文件里：那个文件只许导出组件，否则 react-refresh 的 HMR 会退化成整页刷新。 */
const TILE_ACTIONS = [
  { key: 'saves', Icon: SaveManagerIcon },
  { key: 'media', Icon: MediaManagerIcon },
  { key: 'rename', Icon: RenameTileIcon },
  { key: 'pending', Icon: PendingSlotIcon },
]

/* 单个游戏磁贴（宫格）：大图标铺满 1:1 + 名称/TID，来源徽标叠在图标角上，
 * 四颗操作按钮排在图标下排。整块可点开详情（见文件头注释）。 */
function GameCard({ g, iconFailed, onIconError, onOpen, onAction }) {
  const { t } = useTranslation()
  const installed = g.src === 'installed'
  const platform = gamePlatform(g.tid)
  const fmtInfo = FMT_LABEL[g.fmt] || FMT_LABEL[installed ? 'pkg' : 'folder']
  const fmtLabel = t(fmtInfo.k, fmtInfo.d)
  /* v0.5.2.136：folder/dump 的真实体积由后端**后台预热**后带回来（size>0 才有），
   * 冷启动那几秒先不显示，几秒后自动补齐。
   * app.db 的 size 列**不是游戏体积**（真机 132 GB 的博德之门 3 在这里只有 1.22 GB），
   * 只给「没有物理源可扫」的内置 PKG 兜底 —— 见 game_mgr.c 的 gm_warm_* 注释。 */
  const sizeLabel = formatSize(g.size || (g.fmt === 'pkg' ? g.appdb_size : 0))
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

  const ACT_LABEL = {
    saves: t('game.act_saves', '存档管理'),
    media: t('game.act_media', '多媒体管理'),
    rename: t('game.act_rename', '名称与背景'),
    pending: t('game.act_pending', '待定功能'),
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
        {/* [CN-FORK] v0.5.2.112 四颗动作按钮从禁用占位变成真入口：
         * 图标换成专用四颗（记忆卡 / 叠卡+播放 / 笔+基线 / 方框三点），配色仍走原来的
         * 「半透明深底 + 细线白图标」，只把 cursor 和悬停反馈补上。
         * 它们**压在详情热区之上**（z-10 + pointer-events-auto），点按钮不会顺带打开详情。 */}
        <div className="absolute inset-x-1.5 bottom-1.5 grid grid-cols-4 gap-1.5 z-10 pointer-events-auto">
          {/* 用 `const Icon = item.Icon` 而不是 `({ key, Icon }) =>`：本仓库的 eslint 配置没装
              eslint-plugin-react，只在 JSX 里用到的**解构参数**会被 no-unused-vars 误判为未使用
              （const 变量有 varsIgnorePattern '^[A-Z_]' 兜底）。 */}
          {TILE_ACTIONS.map((item) => {
            const Icon = item.Icon
            return (
              <button
                key={item.key}
                type="button"
                onClick={() => onAction(item.key, g)}
                title={ACT_LABEL[item.key]}
                aria-label={ACT_LABEL[item.key]}
                className={cn(
                  'flex items-center justify-center py-1.5 rounded-md text-white',
                  'bg-black/55 border border-white/15 backdrop-blur-sm',
                  'transition-colors hover:bg-black/80 hover:border-white/35',
                )}
              >
                <Icon className="w-4 h-4" />
              </button>
            )
          })}
        </div>
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
  /* v0.5.2.136 后台体积预热：后端列表只读体积缓存（不扫盘）→ folder/dump 游戏第一次
   * 拿不到真实体积，先不显示标签，同时起线程去扫；扫完作废快照并回 warming=false。
   * 这里在 warming 期间隔几秒自动再拉一次，让体积自己补齐（见下面的 effect）。 */
  const [warming, setWarming] = useState(false)
  const [warmTry, setWarmTry] = useState(0)
  /* [CN-FORK] v0.5.2.137 名字回填按钮的跑动态（见 syncNames）。 */
  const [syncing, setSyncing] = useState(false)
  /* 五个弹窗共用一个 state：{kind, game}。kind ∈ detail|saves|media|rename|pending。
   * 用对象而不是五个布尔，是因为「从详情里点『存档 3 个』跳到存档弹窗」需要在同一个游戏上
   * 换 kind —— 五个布尔得同时改两个，很容易出现两个弹窗叠在一起。 */
  const [modal, setModal] = useState(null)

  /* force=true 时带 force=1：让后端跳过 60s 快照直接重建（.133）。
   * 进页面那次不带 —— 吃缓存，秒开；用户手点「刷新」才付一次全库读的成本。
   * 语义对齐按钮的含义：点了刷新就是要真从主机重新读，而不是等快照自己过期。 */
  const load = useCallback(async (force) => {
    setLoading(true)
    setErr('')
    try {
      const q = force ? `?force=1&_=${Date.now()}` : `?_=${Date.now()}`
      const res = await fetch(`/game_list${q}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data && Array.isArray(data.games)) {
        setGames(data.games)
        setCount(typeof data.count === 'number' ? data.count : data.games.length)
        setTruncated(!!data.truncated)
        setFailedIcons({})
        const w = !!data.warming
        setWarming(w)
        if (!w) setWarmTry(0)   /* 预热结束：重试计数归零，下次进页面从头数 */
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

  /* 预热期间自动重拉（v0.5.2.136）。**不是轮询** —— 文件头那条「不轮询」的原则仍然成立：
   * 只在 warming=true 时临时重试，预热一结束就停，且有次数上限（15 × 4s ≈ 1 分钟），
   * 后端万一卡住也不会无休止地打请求。
   * 重试走普通 load()（吃 60s 快照）：预热线程扫完会**主动作废快照**，所以不必带
   * force=1 —— 那会每次都全量重建（app.db 全库读 + 目录扫描），得不偿失。 */
  useEffect(() => {
    if (!warming || warmTry >= 15) return undefined
    const id = setTimeout(() => {
      setWarmTry((n) => n + 1)
      load()
    }, 4000)
    return () => clearTimeout(id)
  }, [warming, warmTry, load])

  const list = games || []

  /* 动作分发：四颗按钮各自开自己的弹窗。用一张表而不是 switch，
   * 以后加第五颗（第四个位置定下来时）只改这里一行。 */
  const openAction = useCallback((key, g) => {
    if (key === 'saves') setModal({ kind: 'saves', game: g })
    else if (key === 'media') setModal({ kind: 'media', game: g })
    else if (key === 'rename') setModal({ kind: 'rename', game: g })
    else setModal({ kind: 'pending', game: g })
  }, [])

  const closeModal = useCallback(() => setModal(null), [])

  /* [CN-FORK] v0.5.2.137 名字回填：把面板里**所有**改过的名字重放到主机各层。
   *
   * 为什么要有这颗按钮，而不是让用户逐款重改一次名：改名只在**改名那一刻**写主机，
   * 于是主机侧有两种情况会落后 —— ① 改名发生在 .132（写游戏源）之前，当时只写了
   * 面板 + app.db，之后 app.db 又被 SMP 重扫按源重建，中文名全被冲回英文原名；
   * ② 镜像类（.ffpfsc）的源锁在镜像里，SMP 每次重新注册都可能让名字回退。
   * 两种情况用户都只看到「面板对、SMP 与电视主屏错」，而逐款手点改名谁也不愿意。
   * 详见 utils/gameApi.js 里 syncTitleNames 的契约注释。
   *
   * 跑完强制重拉列表（force=1）：后端改的是主机 app.db，而列表吃 60s 快照 ——
   * 不重拉的话用户点完看不到任何变化，会以为按钮没反应（.133 那条教训）。
   * 失败款数 > 0 时按 error 弹，让用户知道有哪几款没落上（note 里有明细）。 */
  const syncNames = useCallback(async () => {
    setSyncing(true)
    try {
      const r = await syncTitleNames()
      const failed = r?.failed ?? 0
      addToast(
        t('game.sync_done', '已回填 {{n}} 款：主机索引 {{ok}}、名字源 {{src}}、失败 {{fail}}', {
          n: r?.scanned ?? 0, ok: r?.applied ?? 0, src: r?.persisted ?? 0, fail: failed,
        }),
        failed > 0 ? 'error' : 'success',
      )
      load(true)
    } catch (e) {
      addToast(`${t('game.sync_fail', '同步失败')}${e && e.message ? `：${e.message}` : ''}`, 'error')
    }
    setSyncing(false)
  }, [addToast, load, t])

  return (
    <>
      <Section
        id="game-list"
        icon={Gamepad2}
        title={t('game.list_title', '游戏列表')}
        count={games ? t('game.total', '共 {{n}} 款', { n: count }) : undefined}
        bodyClass="space-y-4"
        actions={
          <div className="flex items-center gap-2">
            {/* [CN-FORK] v0.5.2.137 名字回填（见 syncNames 的注释）。放在刷新左边：
                它是「补历史 / 补被冲掉的名字」，与刷新那种「重新读一遍」是两件事。 */}
            <button
              onClick={syncNames}
              disabled={syncing || loading}
              title={t('game.sync_names_hint', '把面板里改过的名字重写到主机：先写主机索引（app.db / appinfo.db，SMP 与电视主屏读的就是它），裸目录 / LZ4 类再写一次游戏源 param.json。改名发生在旧版本、或 SMP 重扫把名字冲回去之后，点这里补一次即可（已经一致的款不会重复写）。')}
              aria-label={t('game.sync_names', '同步名字到主机')}
              className="icon-btn"
            >
              <Tag className={cn(syncing && 'animate-pulse')} />
            </button>
            <button onClick={() => load(true)} disabled={loading} title={t('common.refresh', '刷新')} aria-label={t('common.refresh', '刷新')} className="icon-btn">
              <RefreshCw className={cn(loading && 'animate-spin')} />
            </button>
          </div>
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
              <span>{t('game.tile_hint', '点磁贴查看游戏详情（详情里只能看信息，挂载 / 卸载 / 删除源请到 ShadowMount 后台）；磁贴上的四颗按钮分别是存档管理、多媒体管理、名称与背景、待定功能。')}</span>
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
                  onAction={openAction}
                />
              ))}
            </div>
          </>
        )}
      </Section>

      {/* 五个弹窗收在这一层（见文件头注释：不放进卡片里）。
          全部传 show={modal?.kind === 'x'}，同一时刻只有一个可见。 */}
      {/* .129：详情弹窗改成纯只读，不再发起任何操作，所以 addToast / showConfirm /
          onChanged 三个回调一起收掉；它自己的「去 ShadowMount 管理」不经过我们。 */}
      <GameDetailModal
        show={modal?.kind === 'detail'}
        game={modal?.game}
        onClose={closeModal}
        onOpenSaves={(g) => setModal({ kind: 'saves', game: g })}
        onOpenMedia={(g) => setModal({ kind: 'media', game: g })}
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
