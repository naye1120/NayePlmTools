import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Gamepad2, RefreshCw, Download, Camera, Pencil, Tag } from 'lucide-react'

/* [CN-FORK] 游戏管理板块：读 /game_list 展示「已安装（app.db）+ 已存在（扫描 dump）」游戏。
 * 进页面拉一次 + 手动刷新，不轮询——app.db 全库读 + 目录扫描是有成本的 IO，后端已用
 * mutex + 60s 快照压住频率，这里再叠轮询只会放大风险（参考 /system_stats 死机教训）。
 * 图标走独立 /game_icon?tid= 端点读 icon0.png；读不到（onError）回退占位图。
 * 导出存档 / 导出截图 / 改名 / 多标签 均为后续功能，先以禁用按钮占位（不接后端）。 */

const BTN_GHOST =
  'inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-white/5 border border-white/10 text-zinc-200 text-[13px] font-bold hover:bg-white/10 disabled:opacity-40 disabled:cursor-not-allowed transition'

/* 单个游戏卡片：图标 + 名称 + TID + 来源徽标；底部一排「后续开放」的禁用操作按钮。 */
function GameCard({ g, iconFailed, onIconError }) {
  const { t } = useTranslation()
  const installed = g.src === 'installed'
  return (
    <div className="bg-black/20 border border-white/10 rounded-xl p-3 flex gap-3 items-start">
      <div className="w-14 h-14 shrink-0 rounded-lg overflow-hidden bg-white/5 border border-white/10 flex items-center justify-center">
        {iconFailed ? (
          <Gamepad2 className="w-6 h-6 text-zinc-600" />
        ) : (
          <img
            src={`/game_icon?tid=${encodeURIComponent(g.tid)}`}
            alt=""
            loading="lazy"
            className="w-full h-full object-cover"
            onError={onIconError}
          />
        )}
      </div>
      <div className="min-w-0 flex-1 space-y-1">
        <div className="flex items-center gap-2">
          <span className="text-[14px] font-bold text-white truncate min-w-0" title={g.name}>{g.name}</span>
          <span
            className={`shrink-0 text-[10px] font-bold px-1.5 py-0.5 rounded ${
              installed ? 'bg-ps-blue/20 text-ps-blue' : 'bg-amber-400/15 text-amber-300'
            }`}
            title={installed ? t('game.src_installed', '已安装') : t('game.src_dump', '转储/外挂')}
          >
            {installed ? t('game.badge_installed', '已安装') : t('game.badge_dump', '转储')}
          </span>
        </div>
        <span className="text-[11px] text-zinc-500 font-mono tracking-wider">{g.tid}</span>
        {/* 后续功能占位：导出存档 / 导出截图 / 改名 / 标签，本版全部禁用 */}
        <div className="flex items-center gap-1 pt-1">
          {[
            { Icon: Download, key: 'game.act_export_save', cn: '导出存档' },
            { Icon: Camera, key: 'game.act_export_shot', cn: '导出截图' },
            { Icon: Pencil, key: 'game.act_rename', cn: '改名' },
            { Icon: Tag, key: 'game.act_tags', cn: '标签' },
          ].map(({ Icon, key }) => (
            <button
              key={key}
              disabled
              title={t('game.soon', '后续开放')}
              className="p-1.5 rounded-md text-zinc-600 bg-white/5 border border-white/10 cursor-not-allowed"
            >
              <Icon className="w-3.5 h-3.5" />
            </button>
          ))}
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
    <section className="bg-white/5 border border-white/10 rounded-2xl p-5 md:p-6 space-y-4">
      <div className="flex flex-wrap items-end justify-between gap-4">
        <div className="space-y-1.5">
          <h3 className="text-2xl font-bold text-white tracking-tight">
            <Gamepad2 className="inline w-6 h-6 mr-2 -mt-1 text-ps-blue" />
            {t('game.title', '游戏管理')}
          </h3>
          <p className="text-[14px] text-zinc-400 max-w-2xl">
            {t('game.desc', '读取已安装（app.db）与已存在（扫描挂载路径）的游戏；导出存档/截图、改名、多标签后续开放。')}
          </p>
        </div>
        <button onClick={load} disabled={loading} className={BTN_GHOST}>
          <RefreshCw className={`w-4 h-4 ${loading ? 'animate-spin' : ''}`} />
          {t('common.refresh', '刷新')}
        </button>
      </div>

      {err && <p className="text-[13px] text-red-400">{err}</p>}

      {games === null && !err ? (
        <div className="flex justify-center py-12">
          <RefreshCw className="w-5 h-5 text-zinc-500 animate-spin" />
        </div>
      ) : list.length === 0 ? (
        <p className="text-[14px] text-zinc-500 py-10 text-center border border-dashed border-white/10 rounded-2xl">
          {t('game.empty', '未发现已安装或已存在的游戏')}
        </p>
      ) : (
        <>
          <div className="flex items-center justify-between">
            <span className="text-[13px] text-zinc-400">
              {t('game.total', '共 {{n}} 款', { n: count })}
            </span>
            {truncated && (
              <span className="text-[12px] text-amber-400">{t('game.truncated', '列表过大，已截断显示')}</span>
            )}
          </div>
          <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
            {list.map((g) => (
              <GameCard
                key={g.tid}
                g={g}
                iconFailed={!!failedIcons[g.tid]}
                onIconError={() => setFailedIcons((m) => ({ ...m, [g.tid]: true }))}
              />
            ))}
          </div>
        </>
      )}
    </section>
  )
}
