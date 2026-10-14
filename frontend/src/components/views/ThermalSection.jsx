import React, { useState, useEffect, useCallback, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { RefreshCw, Thermometer, Fan, Power } from 'lucide-react'
import { cn } from '../../utils/helpers' /* [CN-FORK] v0.5.2.95 档位/开关钮归到 .btn 体系后需要拼类名 */
import Section from '../ui/Section' /* [CN-FORK] v0.5.2.94 板块形状统一 */
import Segmented from '../ui/Segmented' /* [CN-FORK] v0.5.2.97 互斥档位统一 */
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'

/* [CN-FORK] 温控管理板块：读 /thermal_stats 展示 CPU / GPU / SSD 温度与风扇占空比，
 * 并提供风扇温控（按温度设定调速目标，见 FanControlCard）。
 * 温度读取：进页面拉一次 + 每 5s 只读自动刷新（可用开关关闭）。轮询周期对齐后端
 * thermal.c 的 5s 快照缓存——5s 内重复请求直接命中缓存、不产生任何内核调用，与「传感器
 * API 不可重入、限流≤５s」的安全规范一致（后端另有 safe_mode 短路 + mutex 兑底）。
 * 注意：真正写 /dev/icc_fan 的调速已由 fan_control.c 常驻守护落地；FanControlCard 在
 * /fan_control 返回 supported:false 或请求失败时优雅显示「未接入」，不谎报成功。 */

/* [CN-FORK] v0.5.2.85 按钮收口到 index.css 骨架层。
 * [CN-FORK] v0.5.2.102 按功能分色（样张 A + I2）：这里的两档分别是「主操作（蓝实心）」与
 * 「刷新/重读（青实心）」，原来的 BTN_GHOST 名字连同它的透明底一起作废。 */
const BTN_INFO = 'btn btn-info'
const BTN_PRIMARY = 'btn btn-primary'

function MetricCard({ label, value, unit, warn }) {
  return (
    <div className="bg-black/20 border border-white/10 rounded-xl p-4 flex flex-col space-y-1">
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

  /* [CN-FORK] v0.6.0.176 挂载即读一次配置是刻意为之（loadCfg 内部先 await 再 setState，
   * 不是同步 setState）；上游新 eslint 规则 react-hooks/set-state-in-effect 对此偏保守。 */
  // eslint-disable-next-line react-hooks/set-state-in-effect
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
      ? { text: t('thermal.fan_st_running_temp', '运行中-当前配置温度{{temp}}℃', { temp: cfg?.target ?? target }), cls: 'text-green-400' }
      : { text: t('thermal.fan_st_inactive', '未生效'), cls: 'text-red-400' }

  return (
    <div className="bg-black/20 border border-white/10 rounded-xl p-4 space-y-3">
      <div className="flex items-center justify-between">
        <span className="text-[14px] font-bold text-white inline-flex items-center">
          <Fan className="w-4 h-4 text-ps-blue mr-2" />
          {t('thermal.fan_ctrl_title', '风扇温控')}
          <span className={`ml-2 text-[12px] font-bold ${status.cls}`}>（{status.text}）</span>
        </span>
        <button
          type="button"
          disabled={!supported || busy}
          onClick={toggleEnabled}
          /* [CN-FORK] v0.5.2.102 「已启用」是图二那颗：从蓝选中档（.sel-chip 是「当前选中」，
           * 不是状态）换成绿实心 btn-ok；未启用时仍是紫实心 btn-outline（开启这一动作属次要操作档）。 */
          className={cn('btn ml-3', enabled ? 'btn-ok' : 'btn-outline')}
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
          <div className="flex items-center">
            <span className="text-[12px] text-zinc-400 shrink-0 mr-3">{t('thermal.fan_target', '目标温度')}</span>
            <input
              type="range"
              min={MIN}
              max={MAX}
              value={target}
              disabled={!enabled}
              onChange={(e) => setTarget(Number(e.target.value))}
              className={`flex-1 accent-ps-blue disabled:opacity-40 ${enabled ? '' : 'cursor-not-allowed'}`}
            />
            <span className="text-[14px] font-bold text-white w-12 text-right tabular-nums ml-3">{target}°C</span>
          </div>
          <div className="flex items-center flex-wrap">
            <span className="text-[12px] text-zinc-400 mr-2">{t('thermal.fan_presets', '档位')}</span>
            {/* [CN-FORK] v0.5.2.97 三枚档位归到共用 <Segmented>；未启用时整条轨道一起禁用，
                原来只有三枚按钮各自 disabled，读屏听不出「这一组现在不可选」。 */}
            <fieldset disabled={!enabled} className="flex disabled:opacity-60">
              <Segmented
                label={t('thermal.fan_presets', '档位')}
                value={target}
                onChange={(v) => setTarget(v)}
                options={presets.map((p) => ({ value: p.v, label: p.label }))}
              />
            </fieldset>
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

/* [CN-FORK] 自动刷新周期：对齐后端 thermal.c 的 5s 快照缓存（更快也无意义，返回的都是缓存）。 */
const AUTO_REFRESH_MS = 5000

export default function ThermalSection() {
  const { t } = useTranslation()
  const [d, setD] = useState(null)
  const [loading, setLoading] = useState(false)
  const [err, setErr] = useState('')
  const [auto, setAuto] = useState(true)
  const inflightRef = useRef(false)

  // silent=true 时为自动刷新：不驱动手动按钮的 loading 图标，避免每 5s 狂转
  const load = useCallback(async (silent) => {
    if (inflightRef.current) return // 上一次未回来，跳过本次，不叠加请求
    inflightRef.current = true
    if (!silent) setLoading(true)
    setErr('')
    try {
      const res = await fetch(`/thermal_stats?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data) setD(data)
      else setErr(t('thermal.read_fail', '读取失败'))
    } catch {
      setErr(t('thermal.read_fail', '读取失败'))
    }
    if (!silent) setLoading(false)
    inflightRef.current = false
  }, [t])

  /* [CN-FORK] v0.6.0.176 挂载即读一次是刻意为之（load 内部先 await 再 setState，
   * 不是同步 setState）；上游新 eslint 规则 react-hooks/set-state-in-effect 对此偏保守。 */
  // eslint-disable-next-line react-hooks/set-state-in-effect
  useEffect(() => { load() }, [load])

  // 只读自动刷新：页面隐藏时暂停，回到前台立即补一次
  useEffect(() => {
    if (!auto) return
    const onTick = () => {
      if (document.visibilityState !== 'visible') return
      load(true)
    }
    const onVisible = () => { if (document.visibilityState === 'visible') load(true) }
    const id = setInterval(onTick, AUTO_REFRESH_MS)
    document.addEventListener('visibilitychange', onVisible)
    return () => { clearInterval(id); document.removeEventListener('visibilitychange', onVisible) }
  }, [auto, load])

  const temp = (v) => (typeof v === 'number' && v >= 0 ? String(v) : '—')
  const disabled = d && d.enabled === false
  const fan = d && typeof d.fan?.duty === 'number' ? `${d.fan.duty}%` : '—'

  return (
    <Section
      id="thermal"
      icon={Thermometer}
      title={t('thermal.title', '温控管理')}
      bodyClass="space-y-4"
      actions={
        <>
          <label className="inline-flex items-center mr-3 text-[13px] text-zinc-400 cursor-pointer select-none">
            <input
              type="checkbox"
              className="accent-ps-blue w-4 h-4 mr-1.5"
              checked={auto}
              onChange={(e) => setAuto(e.target.checked)}
            />
            {t('thermal.auto_refresh', '自动刷新')}
            <span className="text-zinc-600 tabular-nums ml-1">·{AUTO_REFRESH_MS / 1000}s</span>
          </label>
          <button onClick={() => load(false)} disabled={loading} className={BTN_INFO}>
            <RefreshCw className={`w-4 h-4 ${loading ? 'animate-spin' : ''}`} />
            {t('common.refresh', '刷新')}
          </button>
        </>
      }
    >
      {/* [CN-FORK] v0.5.2.94 本块从「自己写一个大页头」降为页内板块：主标题已经交给页面级的
          粘顶 <PageHead>，这里只留一行说明，避免同一屏出现两个 24px 级标题。 */}
      <p className="text-[14px] text-zinc-400 max-w-2xl">
        {t('thermal.desc', 'CPU / GPU / SSD 实时温度与风扇转速，并可设定按温度调速的目标阈值。')}
      </p>

      {disabled && (
        <p className="text-[13px] text-amber-400">
          {t('thermal.safe_mode', '主机处于安全模式（safe_mode），温控探测已停用。')}
        </p>
      )}
      {err && <p className="text-[13px] text-red-400">{err}</p>}

      {!d && loading ? (
        <LoadingState label={t('thermal.loading', '正在读取温度…')} />
      ) : d && !disabled ? (
        <div className="grid grid-cols-2 sm:grid-cols-4 gap-3">
          <MetricCard label="CPU" value={temp(d.cpu?.temp)} unit="°C" warn={typeof d.cpu?.temp === 'number' && d.cpu.temp >= 80} />
          <MetricCard label="GPU" value={temp(d.gpu?.temp)} unit="°C" warn={typeof d.gpu?.temp === 'number' && d.gpu.temp >= 80} />
          <MetricCard label="SSD" value={temp(d.ssd?.temp)} unit="°C" warn={typeof d.ssd?.temp === 'number' && d.ssd.temp >= 65} />
          <MetricCard label={t('thermal.fan', '风扇')} value={fan} unit="" warn={false} />
        </div>
      ) : d ? null : (
        <EmptyState dashed={false} title={t('thermal.pending', '温控数据待接入')} />
      )}

      {!disabled && <FanControlCard />}
    </Section>
  )
}
