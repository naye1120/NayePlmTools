import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { RefreshCw, Thermometer } from 'lucide-react'

/* [CN-FORK] 温控管理板块：读 /thermal_stats 展示 CPU / GPU / SSD 温度与风扇占空比。
 * 进页面拉一次 + 手动刷新，不自动轮询——传感器 API 不可重入，后端已用
 * safe_mode 短路 + mutex 串行 + 5s 快照兜底；这里再叠一层轮询没有意义。
 * 「按温度自动调速」是写操作（参考 ps5-fan-control 的 /dev/icc_fan 阈值），
 * 属高风险项，待后端 /fan_control 落地后再在此补控制 UI，本版先只读。 */

const BTN_GHOST =
  'inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-white/5 border border-white/10 text-zinc-200 text-[13px] font-bold hover:bg-white/10 disabled:opacity-40 disabled:cursor-not-allowed transition'

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
            {t('thermal.desc', 'CPU / GPU / SSD 实时温度与风扇转速（只读）。按温度自动调速待接入。')}
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
    </section>
  )
}
