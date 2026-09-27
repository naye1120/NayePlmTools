import React, { useState, useEffect } from 'react'
import { useTranslation } from 'react-i18next'
import {
  Monitor, Cpu, Thermometer, HardDrive, MemoryStick,
  Fan, Clock, Hash, Zap, Wifi, Image as ImageIcon
} from 'lucide-react'

/* [CN-FORK] 主页仪表盘 — 硬件信息、FPS 与当前游戏均来自后端 /system_stats
 * （受 READ_HARDWARE_INFO 开关控制）；未开启或取不到数据时一律显示占位破折号 “—”。*/

// 温度颜色: <60 绿, 60–79 黄, 80–84 橙, ≥85 红
const tempColor = (t) => t < 60 ? 'text-green-400' : t < 80 ? 'text-yellow-400' : t < 85 ? 'text-orange-400' : 'text-red-400'
const tempBg = (t) => t < 60 ? 'bg-green-400' : t < 80 ? 'bg-yellow-400' : t < 85 ? 'bg-orange-400' : 'bg-red-400'

// FPS 颜色: ≥59 绿, 29–58 黄, 21–28 橙, <21 红
const fpsColor = (f) => f >= 59 ? 'text-green-400' : f >= 29 ? 'text-yellow-400' : f >= 21 ? 'text-orange-400' : 'text-red-400'

// [CN-FORK] 仪表盘四段色（0%→100% 依次 绿→黄→橙→红），色环与百分比共用
const GAUGE_SEG_COLORS = ['#22c55e', '#eab308', '#f97316', '#ef4444']
const gaugeColor = (value, max = 100) => {
  const pct = max > 0 ? Math.min(1, Math.max(0, value / max)) : 0
  const idx = Math.min(GAUGE_SEG_COLORS.length - 1, Math.floor(pct * GAUGE_SEG_COLORS.length))
  return GAUGE_SEG_COLORS[idx]
}

// [CN-FORK] 内存/存储：正常保持本色，占用 >90% 橙、>95% 红（Donut 用 hex，进度条/图例用 tailwind class）
const memStroke = (pct, base) => pct > 95 ? '#ef4444' : pct > 90 ? '#f97316' : base
const memBg = (pct, base) => pct > 95 ? 'bg-red-400' : pct > 90 ? 'bg-orange-400' : base

const fmtGB = (mb) => (mb == null || Number.isNaN(mb)) ? '—' : `${(mb / 1024).toFixed(1)} GB`
// [CN-FORK] 安全百分比：无数据/除零 → null（调用方据此显示 “—”）
const safePct = (used, total) => (total && total > 0 && used != null) ? Math.min(100, Math.round((used / total) * 100)) : null
const hasNum = (v) => typeof v === 'number' && !Number.isNaN(v)
// [CN-FORK] 可用量：任一为空则 null（避免 null-null=0 误显示）
const freeGB = (total, used) => (total != null && used != null) ? (total - used) : null
// [CN-FORK] 温度：后端读不到会返回 -1，视为无数据→ null（显示 —），避免把失败值当成 0°C
const tempVal = (v) => (typeof v === 'number' && v >= 0) ? v : null

// [CN-FORK] 横向彩色进度条；上方右侧三分之一处标注 已用/总计（无数据显示 —）
const ProgressBar = ({ used, total, color = 'bg-ps-blue' }) => {
  const pct = safePct(used, total)
  const label = (pct == null) ? '—'
    : `${Math.round(used / 1024)}G/${Math.round(total / 1024)}G`
  return (
    <div className="space-y-1">
      <div className="grid grid-cols-3 items-end">
        <div className="col-span-2" />
        <div className="col-span-1 text-right">
          <span className="text-[12px] font-bold text-zinc-300 tabular-nums">{label}</span>
        </div>
      </div>
      <div className="h-1.5 bg-white/5 rounded-full overflow-hidden">
        <div className={`h-full ${color} rounded-full transition-all`} style={{ width: `${pct ?? 0}%` }} />
      </div>
    </div>
  )
}

// [CN-FORK] 饼图（环形进度）— 紧凑尺寸
const Donut = ({ used, total, color = 'var(--color-ps-blue, #3b82f6)', size = 72, stroke = 9, centerTop, centerSub }) => {
  const pct = total > 0 ? Math.min(1, used / total) : 0
  const r = (size - stroke) / 2
  const circ = 2 * Math.PI * r
  return (
    <div className="relative shrink-0" style={{ width: size, height: size }}>
      <svg className="w-full h-full -rotate-90" viewBox={`0 0 ${size} ${size}`}>
        <circle cx={size / 2} cy={size / 2} r={r} fill="none" stroke="rgba(255,255,255,0.06)" strokeWidth={stroke} />
        <circle cx={size / 2} cy={size / 2} r={r} fill="none" stroke={color} strokeWidth={stroke}
          strokeDasharray={`${circ * pct} ${circ}`} strokeLinecap="round" />
      </svg>
      <div className="absolute inset-0 flex flex-col items-center justify-center">
        <span className="text-[15.5px] font-black text-white leading-none">{centerTop}</span>
        {centerSub && <span className="text-[11px] text-zinc-500 mt-0.5 leading-none">{centerSub}</span>}
      </div>
    </div>
  )
}

// [CN-FORK] 彩色分区转速表（仿 ECharts gauge）：红→橙→黄→绿 分段弧 + 内侧刻度 + 三角指针
const Gauge = ({ value, max = 100 }) => {
  const pct = max > 0 ? Math.min(1, Math.max(0, value / max)) : 0
  const cx = 60, cy = 58, R = 44
  const START = 225, SWEEP = 270 // 0%→左下, 100%→右下, 顺时针经顶部
  const rad = (deg) => (deg * Math.PI) / 180
  const pt = (r, deg) => ({ x: +(cx + r * Math.cos(rad(deg))).toFixed(2), y: +(cy - r * Math.sin(rad(deg))).toFixed(2) })
  // 彩色分区弧（纯色，段间留 ~2.8° 缺口）
  const segColors = GAUGE_SEG_COLORS
  const segSpan = SWEEP / segColors.length
  const segs = segColors.map((c, i) => {
    const a1 = START - i * segSpan - 1.4
    const a2 = START - (i + 1) * segSpan + 1.4
    const p1 = pt(R, a1), p2 = pt(R, a2)
    return <path key={i} d={`M ${p1.x} ${p1.y} A ${R} ${R} 0 0 1 ${p2.x} ${p2.y}`} fill="none" stroke={c} strokeWidth="11" strokeLinecap="round" />
  })
  // 内侧刻度（每 10% 一条）
  const ticks = []
  for (let i = 0; i <= 10; i++) {
    const a = START - (SWEEP * i) / 10
    const o = pt(R - 7, a), inn = pt(R - 13, a)
    ticks.push(<line key={i} x1={o.x} y1={o.y} x2={inn.x} y2={inn.y} stroke="rgba(255,255,255,0.45)" strokeWidth="1.4" strokeLinecap="round" />)
  }
  // 指针
  const angle = START - pct * SWEEP
  const tip = pt(R - 12, angle)
  const bL = pt(6, angle + 90), bR = pt(6, angle - 90)
  return (
    <div className="relative w-[124px] h-[104px] shrink-0">
      <svg viewBox="0 0 120 104" className="w-full h-full overflow-visible">
        {segs}
        {ticks}
        <polygon points={`${tip.x},${tip.y} ${bL.x},${bL.y} ${bR.x},${bR.y}`} fill="#e5e7eb" />
        <circle cx={cx} cy={cy} r="5" fill="#e5e7eb" />
        <circle cx={cx} cy={cy} r="2" fill="#0b0b0d" />
      </svg>
    </div>
  )
}

// 信息行组件（主机基本信息紧凑版）
const InfoRow = ({ icon: Icon, label, value }) => (
  <div className="flex items-center gap-2.5">
    <div className="p-1.5 bg-white/5 rounded-lg shrink-0">
      <Icon className="w-3.5 h-3.5 text-zinc-400" />
    </div>
    <div className="min-w-0 flex-1">
      <p className="text-[12px] text-zinc-500 uppercase tracking-wider leading-tight">{label}</p>
      <p className="text-[15.5px] font-bold text-white truncate leading-tight">{value}</p>
    </div>
  </div>
)

// 温度卡片（紧凑）；value 为 null 时显示 “—”
const TempCard = ({ label, value, unit = '°C' }) => {
  const has = hasNum(value)
  return (
    <div className="glass-card p-3 rounded-2xl border border-white/10 flex flex-col justify-between h-full gap-2">
      <div className="flex items-center justify-between">
        <span className="text-[14px] text-zinc-400 font-medium uppercase tracking-wider">{label}</span>
        <Thermometer className={`w-4 h-4 ${has ? tempColor(value) : 'text-zinc-600'}`} />
      </div>
      <div className="flex items-end gap-1">
        <span className={`text-2xl font-black tracking-tighter ${has ? tempColor(value) : 'text-zinc-600'}`}>{has ? value : '—'}</span>
        <span className="text-[14px] text-zinc-500 mb-0.5">{unit}</span>
      </div>
      <div className="h-1.5 bg-white/5 rounded-full overflow-hidden">
        <div className={`h-full ${has ? tempBg(value) : 'bg-white/10'} rounded-full transition-all`} style={{ width: `${has ? Math.min(100, value) : 0}%` }} />
      </div>
    </div>
  )
}

// [CN-FORK] 左侧 FPS 面板：FPS + 当前游戏图标 + 名称（全部在此卡片内部）
const FpsHero = ({ fps, game }) => {
  const { t } = useTranslation()
  // [CN-FORK] 图标加载失败（404/开关关闭）回退占位图；记住失败的 src，换游戏后自动重试
  const [iconErrFor, setIconErrFor] = useState('')
  const showIcon = game.icon && iconErrFor !== game.icon
  return (
    <div className="glass-card p-4 rounded-2xl border border-white/10 flex flex-col gap-3 h-full">
      <div className="flex flex-1 items-center justify-between">
        <div className="flex items-center gap-2 self-start">
          <Zap className="w-4 h-4 text-ps-blue" />
          <span className="text-[14px] text-zinc-400 font-medium uppercase tracking-[0.15em]">{t("dashboard.fps", "FPS")}</span>
        </div>
        <div className="flex items-end gap-2">
          <span className={`text-[56px] leading-[0.8] font-black tracking-tighter tabular-nums ${fps == null ? 'text-zinc-600' : fpsColor(fps)}`}>{fps == null ? '—' : fps}</span>
          <span className="text-[16.5px] font-bold text-zinc-500 mb-1.5">fps</span>
        </div>
      </div>
      {/* 当前游戏：图标 + 名称 */}
      <div className="flex items-center gap-3 min-w-0 pt-3 border-t border-white/5">
        {showIcon
          ? <img src={game.icon} alt="game icon" onError={() => setIconErrFor(game.icon)} className="w-11 h-11 rounded-lg object-cover shrink-0" />
          : <div className="w-11 h-11 rounded-lg bg-white/5 border border-white/10 flex items-center justify-center shrink-0"><ImageIcon className="w-5 h-5 text-zinc-600" /></div>}
        <div className="min-w-0">
          <p className="text-[12px] text-zinc-500 uppercase tracking-wider leading-tight">{t("dashboard.current_game", "当前游戏")}</p>
          <p className="text-[15.5px] font-bold text-white truncate leading-tight">{game.name ?? '—'}</p>
        </div>
      </div>
    </div>
  )
}

// 风扇 / 内存 / 存储 通用「左饼右表」卡片外壳（紧凑）
const SplitCard = ({ title, icon: Icon, iconColor, children }) => (
  <div className="glass-card p-4 rounded-2xl border border-white/10 flex flex-col gap-3 h-full">
    <div className="flex items-center gap-2">
      <Icon className={`w-4 h-4 ${iconColor}`} />
      <span className="text-[15.5px] font-bold text-white">{title}</span>
    </div>
    {children}
  </div>
)

// 图例行（紧凑）
const Legend = ({ color, label, value }) => (
  <div className="flex items-center justify-between text-[14px]">
    <div className="flex items-center gap-1.5 min-w-0">
      <span className={`w-2 h-2 rounded-full ${color} shrink-0`} />
      <span className="text-zinc-400 truncate">{label}</span>
    </div>
    <span className="font-bold text-white shrink-0">{value}</span>
  </div>
)

const DashboardView = ({ config }) => {
  const { t } = useTranslation()
  const enabled = config?.READ_HARDWARE_INFO !== false
  const [stats, setStats] = useState(null)

  // [CN-FORK] 开启时轮询 /system_stats（2s）；关闭时清空。
  // 关键：用 setInterval 而非“请求完成后再排下一次”的链式 setTimeout —— 后者只要有一次 fetch 卡住不返回，
  // 整条轮询链就永久死掉（表现为放着不更新、手动刷新才复活）。
  // 配套防护：① AbortController 1.8s 强制中断挂起请求；② busy 超 5s 视为失效允许重发；
  // ③ 时间戳 + no-store 防 PS5 浏览器缓存 GET；④ 回到前台/窗口聚焦时立即补一次。
  useEffect(() => {
    if (!enabled) { setStats(null); return }
    let alive = true
    let busy = false
    let busySince = 0
    const load = async () => {
      if (busy && Date.now() - busySince < 5000) return /* 上一轮未归，跳过本轮防堆积；超 5s 强制重发 */
      busy = true
      busySince = Date.now()
      const ctrl = (typeof AbortController !== 'undefined') ? new AbortController() : null
      const killer = ctrl ? setTimeout(() => ctrl.abort(), 1800) : null
      try {
        const res = await fetch(`/system_stats?_=${Date.now()}`, { cache: 'no-store', signal: ctrl ? ctrl.signal : undefined })
        const data = await res.json().catch(() => null)
        if (alive) setStats(!data || data.enabled === false ? null : data)
      } catch { /* 网络异常/超时中断：忽略，下个 tick 重试 */ }
      if (killer) clearTimeout(killer)
      busy = false
    }
    load()
    const iv = setInterval(load, 2000)
    const onActive = () => { if (!document.hidden) load() }
    document.addEventListener('visibilitychange', onActive)
    window.addEventListener('focus', onActive)
    return () => {
      alive = false
      clearInterval(iv)
      document.removeEventListener('visibilitychange', onActive)
      window.removeEventListener('focus', onActive)
    }
  }, [enabled])

  // 硬件信息映射（未开启/无数据 → null → 前端显示 “—”）
  const s = enabled ? stats : null

  // FPS / 当前游戏：同样来自 /system_stats（受同一开关门控），无数据显示 “—”
  const fpsVal = (typeof s?.fps === 'number' && s.fps > 0.5) ? Math.round(s.fps) : null
  const game = { name: s?.game?.name || null, tid: s?.game?.tid || null, icon: s?.game?.tid ? `/game_icon?tid=${s.game.tid}` : '' }
  const temps = { cpu: tempVal(s?.cpu?.temp), gpu: tempVal(s?.gpu?.temp), ssd: tempVal(s?.ssd?.temp) }
  const fan = { duty: s?.fan?.duty ?? null, max: s?.fan?.max ?? 100 }
  const mem = { ramUsed: s?.ram?.used ?? null, ramTotal: s?.ram?.total ?? null, vramUsed: s?.vram?.used ?? null, vramTotal: s?.vram?.total ?? null }
  const internal = s?.storage?.find(x => x.label === 'Internal storage')
  const nvme = s?.storage?.find(x => x.label === 'NVME SSD')
  const storage = { ssdUsed: internal?.used ?? null, ssdTotal: internal?.total ?? null, m2Used: nvme?.used ?? null, m2Total: nvme?.total ?? null }
  // [CN-FORK] 型号/序列号/系统版本均由 /system_stats 下发（v0.5.1.46 起）；序列号 userland 暂无可靠数据源，探不到即空→显示 —
  const sysInfo = { model: s?.model || null, serial: s?.serial || null, firmware: s?.firmware || null, uptime: s?.uptime || null }

  const ramPct = safePct(mem.ramUsed, mem.ramTotal)
  const vramPct = safePct(mem.vramUsed, mem.vramTotal)
  const ssdPct = safePct(storage.ssdUsed, storage.ssdTotal)
  const m2Pct = safePct(storage.m2Used, storage.m2Total)
  const fanPct = safePct(fan.duty, fan.max)

  return (
    <div className="w-full space-y-4 animate-fade-in">
      {/* 标题 + 主机基本信息 合并为一条紧凑头部 */}
      <div className="glass-card p-4 rounded-2xl border border-white/10 flex flex-col gap-3">
        <div className="flex items-baseline justify-between">
          <h2 className="text-xl font-black text-white uppercase tracking-tight">
            {t("dashboard.title", "系统信息")}
          </h2>
          <p className="text-[14px] text-zinc-500">{t("dashboard.subtitle", "PS5 主机实时状态概览")}</p>
        </div>
        <div className="grid grid-cols-2 xl:grid-cols-4 gap-3">
          <InfoRow icon={Monitor} label={t("dashboard.model", "主机型号")} value={sysInfo.model ?? '—'} />
          <InfoRow icon={Hash} label={t("dashboard.serial", "序列号")} value={sysInfo.serial ?? '—'} />
          <InfoRow icon={Wifi} label={t("dashboard.firmware", "系统版本")} value={sysInfo.firmware ? `v${sysInfo.firmware}` : '—'} />
          <InfoRow icon={Clock} label={t("dashboard.uptime", "开机时间")} value={sysInfo.uptime ?? '—'} />
        </div>
      </div>

      {/* 实时监控：FPS + 风扇 + CPU/GPU/SSD 温度，单行 */}
      <section className="space-y-2.5">
        <h3 className="label-caps !text-ps-blue !opacity-100 flex items-center gap-2 !text-[15.5px] tracking-[0.15em]">
          <Thermometer className="w-4 h-4" />
          <span>{t("dashboard.monitor", "实时监控")}</span>
        </h3>
        {/* 单行：FPS(含当前游戏) + 风扇 + CPU/GPU/SSD 温度 */}
        <div className="grid grid-cols-2 xl:grid-cols-12 gap-3">
          <div className="col-span-2 xl:col-span-3">
            <FpsHero fps={fpsVal} game={game} />
          </div>
          <div className="col-span-2 xl:col-span-3">
            <SplitCard title={t("dashboard.fan", "风扇转速")} icon={Fan} iconColor="text-cyan-400">
              <div className="flex items-center gap-3">
                <Gauge value={fan.duty ?? 0} max={fan.max} />
                <div className="flex-1 min-w-0 flex flex-col items-center">
                  <span className="text-3xl font-black tabular-nums leading-none" style={{ color: fanPct != null ? gaugeColor(fan.duty ?? 0, fan.max) : undefined }}>{fanPct != null ? `${fanPct}%` : '—'}</span>
                </div>
              </div>
            </SplitCard>
          </div>
          <div className="xl:col-span-2"><TempCard label="CPU" value={temps.cpu} /></div>
          <div className="xl:col-span-2"><TempCard label="GPU" value={temps.gpu} /></div>
          <div className="xl:col-span-2"><TempCard label={t("dashboard.ssd_temp", "SSD")} value={temps.ssd} /></div>
        </div>
      </section>

      {/* 内存 + 存储 合并为单行 4 卡 */}
      <section className="space-y-2.5">
        <h3 className="label-caps !text-ps-blue !opacity-100 flex items-center gap-2 !text-[15.5px] tracking-[0.15em]">
          <MemoryStick className="w-4 h-4" />
          <span>{t("dashboard.memory_storage", "内存与存储")}</span>
        </h3>
        <div className="grid grid-cols-1 sm:grid-cols-2 xl:grid-cols-4 gap-3">
          {/* 运行内存 */}
          <SplitCard title={t("dashboard.ram", "运行内存")} icon={Cpu} iconColor="text-ps-blue">
            <div className="flex items-center gap-3">
              <Donut used={mem.ramUsed} total={mem.ramTotal} color={memStroke(ramPct, "var(--color-ps-blue, #3b82f6)")}
                centerTop={ramPct != null ? `${ramPct}%` : '—'} centerSub="已用" />
              <div className="flex-1 space-y-1.5 min-w-0">
                <Legend color={memBg(ramPct, "bg-ps-blue")} label={t("dashboard.used", "已用")} value={fmtGB(mem.ramUsed)} />
                <Legend color="bg-white/10" label={t("dashboard.free", "可用")} value={fmtGB(freeGB(mem.ramTotal, mem.ramUsed))} />
              </div>
            </div>
            <ProgressBar used={mem.ramUsed} total={mem.ramTotal} color={memBg(ramPct, "bg-ps-blue")} />
          </SplitCard>
          {/* 显存 */}
          <SplitCard title={t("dashboard.vram", "显存")} icon={MemoryStick} iconColor="text-purple-400">
            <div className="flex items-center gap-3">
              <Donut used={mem.vramUsed} total={mem.vramTotal} color={memStroke(vramPct, "#a855f7")}
                centerTop={vramPct != null ? `${vramPct}%` : '—'} centerSub="已用" />
              <div className="flex-1 space-y-1.5 min-w-0">
                <Legend color={memBg(vramPct, "bg-purple-500")} label={t("dashboard.used", "已用")} value={fmtGB(mem.vramUsed)} />
                <Legend color="bg-white/10" label={t("dashboard.free", "可用")} value={fmtGB(freeGB(mem.vramTotal, mem.vramUsed))} />
              </div>
            </div>
            <ProgressBar used={mem.vramUsed} total={mem.vramTotal} color={memBg(vramPct, "bg-purple-500")} />
          </SplitCard>
          {/* 内置 SSD */}
          <SplitCard title={t("dashboard.ssd", "内置 NVMe SSD")} icon={HardDrive} iconColor="text-emerald-400">
            <div className="flex items-center gap-3">
              <Donut used={storage.ssdUsed} total={storage.ssdTotal} color={memStroke(ssdPct, "#10b981")}
                centerTop={ssdPct != null ? `${ssdPct}%` : '—'} centerSub="已用" />
              <div className="flex-1 space-y-1.5 min-w-0">
                <Legend color={memBg(ssdPct, "bg-emerald-500")} label={t("dashboard.used", "已用")} value={fmtGB(storage.ssdUsed)} />
                <Legend color="bg-white/10" label={t("dashboard.free", "可用")} value={fmtGB(freeGB(storage.ssdTotal, storage.ssdUsed))} />
              </div>
            </div>
            <ProgressBar used={storage.ssdUsed} total={storage.ssdTotal} color={memBg(ssdPct, "bg-emerald-500")} />
          </SplitCard>
          {/* M.2 扩展 */}
          <SplitCard title={t("dashboard.m2", "M.2 扩展存储")} icon={HardDrive} iconColor="text-amber-400">
            <div className="flex items-center gap-3">
              <Donut used={storage.m2Used} total={storage.m2Total} color={memStroke(m2Pct, "#f59e0b")}
                centerTop={m2Pct != null ? `${m2Pct}%` : '—'} centerSub="已用" />
              <div className="flex-1 space-y-1.5 min-w-0">
                <Legend color={memBg(m2Pct, "bg-amber-500")} label={t("dashboard.used", "已用")} value={fmtGB(storage.m2Used)} />
                <Legend color="bg-white/10" label={t("dashboard.free", "可用")} value={fmtGB(freeGB(storage.m2Total, storage.m2Used))} />
              </div>
            </div>
            <ProgressBar used={storage.m2Used} total={storage.m2Total} color={memBg(m2Pct, "bg-amber-500")} />
          </SplitCard>
        </div>
      </section>
    </div>
  )
}

export default DashboardView
