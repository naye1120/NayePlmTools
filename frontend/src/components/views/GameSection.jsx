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
 * c=彩色底色（内联 style 用，避开 Tailwind JIT 漏生成任意色类 + 浅色主题洗白字）。 */
const FMT_LABEL = {
  pkg: { k: 'game.fmt_pkg', d: 'PKG', c: '#059669' },
  ffpkg: { k: 'game.fmt_ffpkg', d: 'ffPKG', c: '#0891b2' },
  exfat: { k: 'game.fmt_exfat', d: 'exFAT', c: '#d97706' },
  ffpfs: { k: 'game.fmt_ffpfs', d: 'ffpfs', c: '#7c3aed' },
  ffpfsc: { k: 'game.fmt_ffpfsc', d: 'ffpfsc', c: '#db2777' },
  folder: { k: 'game.fmt_folder', d: '文件夹', c: '#475569' },
  lz4: { k: 'game.fmt_lz4', d: 'LZ4', c: '#4f46e5' },
}
/* 平台徒标颜色：PS5 蓝、PS4 紫。 */
const PLAT_COLOR = { PS5: '#2563eb', PS4: '#7c3aed' }
/* 统一尺寸的徒标样式（固定宽高 + 居中）；不用 text-white 类，颜色由内联 style 给定。 */
const BADGE =
  'inline-flex items-center justify-center w-[3.25rem] h-[18px] rounded text-[10px] font-bold leading-none shadow'

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
    <div className="bg-black/20 border border-white/10 rounded-xl p-1.5 flex flex-col gap-1.5">
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
         * 用内联 style 上彩色底 + 白字：既绕开 Tailwind JIT 可能没生成任意色类，也避开浅色主题
         * 把 .text-white 洗成近黑的问题（这里根本不用 text-white 类）。 */}
        <div className="absolute top-1.5 left-1.5 flex flex-col items-start gap-1">
          {platform && (
            <span
              className={BADGE}
              style={{ backgroundColor: platColor, color: '#ffffff' }}
              title={platform === 'PS4' ? t('game.plat_ps4', 'PS4 游戏') : t('game.plat_ps5', 'PS5 游戏')}
            >
              {platform}
            </span>
          )}
          <span
            className={BADGE}
            style={{ backgroundColor: fmtInfo.c, color: '#ffffff' }}
            title={`${t('game.fmt', '格式')}: ${fmtLabel}`}
          >
            {fmtLabel}
          </span>
        </div>
        <span
          className={`absolute top-1.5 right-1.5 text-[10px] font-bold px-1.5 py-0.5 rounded ${
            installed ? 'bg-ps-blue/85 text-white' : 'bg-amber-500/90 text-black'
          }`}
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
        <div className="flex items-baseline justify-between gap-2">
          <span className="text-[11px] text-zinc-500 font-mono tracking-wider truncate">{g.tid}</span>
          {sizeLabel && (
            <span
              className="shrink-0 text-[11px] font-semibold text-zinc-400 tabular-nums"
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
        <div className="grid grid-cols-2 sm:grid-cols-3 lg:grid-cols-4 gap-3">
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
