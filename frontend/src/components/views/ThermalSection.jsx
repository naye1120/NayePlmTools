import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { RefreshCw, Thermometer, Fan, Power } from 'lucide-react'

/* [CN-FORK] 温控管理板块：读 /thermal_stats 展示 CPU / GPU / SSD 温度与风扇占空比，
 * 并提供风扇温控（按温度设定调速目标，见 FanControlCard）。
 * 温度/风扇读取进页面拉一次 + 手动刷新，不轮询——传感器 API 不可重入，后端已用
 * safe_mode 短路 + mutex + 5s 快照兜底。
 * 注意：真正写 /dev/icc_fan 的调速后端尚未落地（高风险硬件写，待单独确认）；
 * FanControlCard 在 /fan_control 返回 supported:false 或请求失败时优雅显示「未接入」，不谎报成功。 */

const BTN_GHOST =
  'inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-white/5 border border-white/10 text-zinc-200 text-[13px] font-bold hover:bg-white/10 disabled:opacity-40 disabled:cursor-not-allowed transition'
const BTN_PRIMARY =
  'inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-ps-blue text-white text-[13px] font-bold hover:opacity-90 disabled:opacity-40 disabled:cursor-not-allowed transition'

function MetricCard({ label, value, unit, warn }) {
  return (
    <div className="bg-black/20 border border-white/10 rounded-xl p-4 flex flex-col gap-1">
      <span className="text-[12px] text-zinc-400">{label}</span>
      <span className={`text-2xl font-bold tracking-tight ${warn ? 'text-red-400' : 'text-white'}`}>
        {value}
        {value !== '—' && unit ? <span className="text-[13px] font-normal text-zinc-400 ml-1">{unit}</span> : null}
      </span>
    </div>
  )
}

/* 风扇温控卡：读 /fan_control 拿当前配置（是否支持/启用/目标温度阈值），
 * 提供开关 + 目标温度滑块 + 快捷档位 + 应用。参考 ps5-fan-control：写的是「按温度调速的目标阈值」。
 * 真正的 /dev/icc_fan 硬件写后端尚未落地（高风险，待单独确认）：/fan_control 返回
 * supported:false 或非 2xx/JSON 时，这里降级为「未接入」提示，不谎报成功。 */
function FanControlCard() {
  const { t } = useTranslation()
  const MIN = 60
  const MAX = 85
  const [cfg, setCfg] = useState(null) // {supported, enabled, applied, target}
  const [enabled, setEnabled] = useState(false)
  const [applied, setApplied] = useState(false)
  const [target, setTarget] = useState(72)
  const [busy, setBusy] = useState(false)
  const [msg, setMsg] = useState(null) // {ok, text}

  const loadCfg = useCallback(async () => {
    try {
      const res = await fetch(`/fan_control?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data && typeof data === 'object') {
        setCfg(data)
        setEnabled(!!data.enabled)
        setApplied(!!data.applied)
        if (typeof data.target === 'number') setTarget(data.target)
      } else {
        setCfg({ supported: false })
      }
    } catch {
      setCfg({ supported: false })
    }
  }, [])

  useEffect(() => { loadCfg() }, [loadCfg])

  const apply = async () => {
    setBusy(true)
    setMsg(null)
    try {
      const res = await fetch(`/fan_control?apply=1&target=${target}&_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data?.ok) setMsg({ ok: true, text: data.message || t('thermal.fan_applied', '已应用') })
      else setMsg({ ok: false, text: (data && data.message) || t('thermal.fan_apply_fail', '应用失败（后端未接入）') })
    } catch {
      setMsg({ ok: false, text: t('thermal.fan_apply_fail', '应用失败（后端未接入）') })
    }
    setBusy(false)
    loadCfg()
  }

  // 开关：只持久化启用位（后端会清 applied），不写硬件——开启后默认失效
  const toggleEnabled = async () => {
    const next = !enabled
    setBusy(true)
    setMsg(null)
    try {
      await fetch(`/fan_control?set_enabled=${next ? 1 : 0}&_=${Date.now()}`, { cache: 'no-store' })
    } catch {
      /* 忽略：loadCfg 会反映真实状态 */
    }
    setBusy(false)
    loadCfg()
  }

  const presets = [
    { label: t('thermal.fan_preset_quiet', '静音'), v: 78 },
    { label: t('thermal.fan_preset_std', '标准'), v: 72 },
    { label: t('thermal.fan_preset_perf', '性能'), v: 66 },
  ]
  const supported = cfg && cfg.supported !== false
  const effective = enabled && applied
  const status = !supported
    ? { text: t('thermal.fan_st_none', '未接入'), cls: 'text-zinc-400' }
    : effective
      ? { text: t('thermal.fan_st_running', '运行中'), cls: 'text-green-400' }
      : { text: t('thermal.fan_st_inactive', '未生效'), cls: 'text-red-400' }

  return (
    <div className="bg-black/20 border border-white/10 rounded-xl p-4 space-y-3">
      <div className="flex items-center justify-between gap-3">
        <span className="text-[14px] font-bold text-white inline-flex items-center gap-2">
          <Fan className="w-4 h-4 text-ps-blue" />
          {t('thermal.fan_ctrl_title', '风扇温控')}
          <span className={`text-[12px] font-bold ${status.cls}`}>（{status.text}）</span>
        </span>
        <button
          type="button"
          disabled={!supported || busy}
          onClick={toggleEnabled}
          className={`inline-flex items-center gap-2 px-3 py-1.5 rounded-lg text-[13px] font-bold border transition disabled:opacity-40 disabled:cursor-not-allowed ${
            enabled ? 'bg-ps-blue/20 border-ps-blue/50 text-ps-blue' : 'bg-white/5 border-white/10 text-zinc-400'
          }`}
        >
          <Power className="w-4 h-4" />
          {enabled ? t('thermal.fan_on', '已启用') : t('thermal.fan_off', '未启用')}
        </button>
      </div>

      {!supported ? (
        <p className="text-[13px] text-amber-400">
          {t('thermal.fan_not_supported', '当前主机/后端未接入风扇控制（/dev/icc_fan 调速待单独确认后落地）。')}
        </p>
      ) : (
        <div className="space-y-3">
          <div className="flex items-center gap-3">
            <span className="text-[12px] text-zinc-400 shrink-0">{t('thermal.fan_target', '目标温度')}</span>
            <input
              type="range"
              min={MIN}
              max={MAX}
              value={target}
              disabled={!enabled}
              onChange={(e) => setTarget(Number(e.target.value))}
              className={`flex-1 accent-ps-blue disabled:opacity-40 ${enabled ? '' : 'cursor-not-allowed'}`}
            />
            <span className="text-[14px] font-bold text-white w-12 text-right tabular-nums">{target}°C</span>
          </div>
          <div className="flex items-center gap-2 flex-wrap">
            <span className="text-[12px] text-zinc-400">{t('thermal.fan_presets', '档位')}</span>
            {presets.map((p) => (
              <button
                key={p.v}
                type="button"
                disabled={!enabled}
                onClick={() => setTarget(p.v)}
                className={`px-2.5 py-1 rounded-md text-[12px] font-bold border transition disabled:opacity-40 disabled:cursor-not-allowed ${
                  target === p.v ? 'bg-ps-blue/20 border-ps-blue/50 text-ps-blue' : 'bg-white/5 border-white/10 text-zinc-300 hover:bg-white/10'
                }`}
              >
                {p.label}
              </button>
            ))}
            <button type="button" onClick={apply} disabled={busy || !enabled} className={`${BTN_PRIMARY} ml-auto`}>
              <RefreshCw className={`w-4 h-4 ${busy ? 'animate-spin' : ''}`} />
              {t('thermal.fan_apply', '应用')}
            </button>
          </div>
          <p className="text-[12px] text-zinc-500">
            {!enabled
              ? t('thermal.fan_hint_off', '未启用：打开右上角开关后方可调节目标温度。')
              : applied
                ? t('thermal.fan_hint', '目标温度越低风扇越积极（更凉、更吵）；越高越安静（更热）。')
                : t('thermal.fan_hint_notapplied', '已启用但尚未生效：设定目标温度后点「应用」。')}
          </p>
          {msg && <p className={`text-[13px] font-bold ${msg.ok ? 'text-green-400' : 'text-red-400'}`}>{msg.text}</p>}
        </div>
      )}
    </div>
  )
}

export default function ThermalSection() {
  const { t } = useTranslation()
  const [d, setD] = useState(null)
  const [loading, setLoading] = useState(false)
  const [err, setErr] = useState('')

  const load = useCallback(async () => {
    setLoading(true)
    setErr('')
    try {
      const res = await fetch(`/thermal_stats?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data) setD(data)
      else setErr(t('thermal.read_fail', '读取失败'))
    } catch {
      setErr(t('thermal.read_fail', '读取失败'))
    }
    setLoading(false)
  }, [t])

  useEffect(() => { load() }, [load])

  const temp = (v) => (typeof v === 'number' && v >= 0 ? String(v) : '—')
  const disabled = d && d.enabled === false
  const fan = d && typeof d.fan?.duty === 'number' ? `${d.fan.duty}%` : '—'

  return (
    <section className="bg-white/5 border border-white/10 rounded-2xl p-5 md:p-6 space-y-4">
      <div className="flex flex-wrap items-end justify-between gap-4">
        <div className="space-y-1.5">
          <h3 className="text-2xl font-bold text-white tracking-tight">
            <Thermometer className="inline w-6 h-6 mr-2 -mt-1 text-ps-blue" />
            {t('thermal.title', '温控管理')}
          </h3>
          <p className="text-[14px] text-zinc-400 max-w-2xl">
            {t('thermal.desc', 'CPU / GPU / SSD 实时温度与风扇转速，并可设定按温度调速的目标阈值。')}
          </p>
        </div>
        <button onClick={load} disabled={loading} className={BTN_GHOST}>
          <RefreshCw className={`w-4 h-4 ${loading ? 'animate-spin' : ''}`} />
          {t('common.refresh', '刷新')}
        </button>
      </div>

      {disabled && (
        <p className="text-[13px] text-amber-400">
          {t('thermal.safe_mode', '主机处于安全模式（safe_mode），温控探测已停用。')}
        </p>
      )}
      {err && <p className="text-[13px] text-red-400">{err}</p>}

      {d && !disabled ? (
        <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
          <MetricCard label="CPU" value={temp(d.cpu?.temp)} unit="°C" warn={typeof d.cpu?.temp === 'number' && d.cpu.temp >= 80} />
          <MetricCard label="GPU" value={temp(d.gpu?.temp)} unit="°C" warn={typeof d.gpu?.temp === 'number' && d.gpu.temp >= 80} />
          <MetricCard label="SSD" value={temp(d.ssd?.temp)} unit="°C" warn={typeof d.ssd?.temp === 'number' && d.ssd.temp >= 65} />
          <MetricCard label={t('thermal.fan', '风扇')} value={fan} unit="" warn={false} />
        </div>
      ) : d || loading || err ? null : (
        <div className="border border-dashed border-white/10 rounded-xl py-8 text-center text-[14px] text-zinc-500">
          {t('thermal.pending', '温控数据待接入')}
        </div>
      )}

      {!disabled && <FanControlCard />}
    </section>
  )
}
