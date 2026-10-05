import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Gamepad2, RefreshCw, Download, Camera, Pencil, Tag } from 'lucide-react'
import Section from '../ui/Section' /* [CN-FORK] v0.5.2.94 板块形状统一 */
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'
import { cn } from '../../utils/helpers'

/* [CN-FORK] 游戏管理板块：读 /game_list 展示「已安装（app.db）+ 已存在（扫描 dump）」游戏。
 * 进页面拉一次 + 手动刷新，不轮询——app.db 全库读 + 目录扫描是有成本的 IO，后端已用
 * mutex + 60s 快照压住频率，这里再叠轮询只会放大风险（参考 /system_stats 死机教训）。
 * 图标走独立 /game_icon?tid= 端点读 icon0.png；读不到（onError）回退占位图。
 * 导出存档 / 导出截图 / 改名 / 多标签 均为后续功能，先以禁用按钮占位（不接后端）。 */

/* [CN-FORK] 由 TITLE_ID 前缀判断主机平台（后端 gm_tid_is_game 白名单即这几类前缀）。 */
function gamePlatform(tid) {
  const p = String(tid || '').slice(0, 4).toUpperCase()
  if (p === 'PPSA' || p === 'EMUA' || p === 'PAUS') return 'PS5'
  if (p === 'CUSA' || p === 'PPJB') return 'PS4'
  return ''
}
/* [CN-FORK] 游戏体积：后端 /game_list 的 size 为字节（镜像=文件 st_size、裸目录=du 累加）；
 * 0/缺失则不显示。按 1024 递进，≥100 或 B/KB 取整、其余保留小数。 */
function formatSize(bytes) {
  const n = Number(bytes) || 0
  if (n <= 0) return ''
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let v = n
  let i = 0
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024
    i += 1
  }
  const digits = v >= 100 || i <= 1 ? 0 : v >= 10 ? 1 : 2
  return `${v.toFixed(digits)} ${units[i]}`
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

/* 单个游戏磁贴（宫格）：大图标铺满 1:1 + 名称/TID，来源徽标叠在图标角上，
 * 操作按钮排在图标下排。导出存档/截图、改名、标签为后续功能，本版禁用占位。 */
function GameCard({ g, iconFailed, onIconError }) {
  const { t } = useTranslation()
  const installed = g.src === 'installed'
  const platform = gamePlatform(g.tid)
  const fmtInfo = FMT_LABEL[g.fmt] || FMT_LABEL[installed ? 'pkg' : 'folder']
  const fmtLabel = t(fmtInfo.k, fmtInfo.d)
  const sizeLabel = formatSize(g.size)
  const platColor = PLAT_COLOR[platform] || '#475569'
  const actions = [
    { Icon: Download, key: 'game.act_export_save' },
    { Icon: Camera, key: 'game.act_export_shot' },
    { Icon: Pencil, key: 'game.act_rename' },
    { Icon: Tag, key: 'game.act_tags' },
  ]
  return (
    <div className="bg-black/20 border border-white/10 rounded-xl p-1.5 flex flex-col space-y-1.5">
      {/* 大图标：宽铺满、1:1 正方形，徽标绝对定位叠右上角 */}
      <div className="relative w-full aspect-square rounded-lg overflow-hidden bg-white/5 border border-white/10 flex items-center justify-center">
        {iconFailed ? (
          <Gamepad2 className="w-12 h-12 text-zinc-600" />
        ) : (
          <img
            src={`/game_icon?tid=${encodeURIComponent(g.tid)}`}
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
        {/* [CN-FORK] 占位按钮：压在封面上的常规半透明深色小图标按钮（细线白图标 + 轻底衷），克制不突兀；
         * 用 aria-disabled 而非 disabled（避免浏览器对禁用控件默认降透）；白字在浅色主题下的可见性由 light-theme.css 豁免保证。 */}
        <div className="absolute inset-x-1.5 bottom-1.5 grid grid-cols-4 gap-1.5">
          {actions.map(({ Icon, key }) => (
            <button
              key={key}
              type="button"
              aria-disabled="true"
              tabIndex={-1}
              title={t('game.soon', '后续开放')}
              className="flex items-center justify-center py-1.5 rounded-md text-white bg-black/55 border border-white/15 backdrop-blur-sm cursor-not-allowed"
            >
              <Icon className="w-4 h-4" />
            </button>
          ))}
        </div>
      </div>
      {/* 名称 + TID（行末右侧标游戏体积） */}
      <div className="space-y-0.5 min-w-0">
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

export default function GameSection() {
  const { t } = useTranslation()
  const [games, setGames] = useState(null) // null=未加载；[]=已加载但无游戏
  const [count, setCount] = useState(0)
  const [truncated, setTruncated] = useState(false)
  const [loading, setLoading] = useState(false)
  const [err, setErr] = useState('')
  const [failedIcons, setFailedIcons] = useState({})

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

  useEffect(() => { load() }, [load])

  const list = games || []

  return (
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
        /* [CN-FORK] v0.5.2.100 用户指令「磁贴再小三分之一，变成自适应，屏幕宽就多放几个、
           窄就少放几个，不要固定四个」：原来是 grid-cols-2 / sm:3 / lg:4，宽屏永远卡在 4 列，
           一块磁贴 ≈355px。现在列宽下限交给 --tile-w（PC 168 / 电视 240），列数由容器自己算。 */
        <div className="tile-grid">
          {list.map((g) => (
            <GameCard
              key={g.tid}
              g={g}
              iconFailed={!!failedIcons[g.tid]}
              onIconError={() => setFailedIcons((m) => ({ ...m, [g.tid]: true }))}
            />
          ))}
        </div>
      )}
    </Section>
  )
}
