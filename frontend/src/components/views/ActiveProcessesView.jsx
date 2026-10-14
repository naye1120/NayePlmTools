/* [CN-FORK] v0.6.0 合入：导入取两侧并集 —— 上游补了 requestScope/mutationRequest/
 * useCallback/useRef/Loader2（文件本体就是上游那版刷新链，这些符号都在用），
 * fork 的共用 UI 组件（PageHead/Section/EmptyState/LoadingState/Switch）与 isPS5
 * 也仍在下面用 → 两类都留。 */
import { createRequestScope } from '../../utils/requestScope'
import { apiFetch, mutationFetch } from '../../utils/mutationRequest'
import React, { useState, useEffect, useMemo, useCallback, useRef } from 'react'
import { Cpu, RefreshCw, XCircle, Search, AlertTriangle, Activity, Loader2, Info, Trash2 } from 'lucide-react'
import { useTranslation, Trans } from 'react-i18next'
import { cn } from '../../utils/helpers'
import PageHead from '../ui/PageHead' /* [CN-FORK] v0.5.2.94 页头/板块/空态/加载归一 */
import Section from '../ui/Section'
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'
import Switch from '../ui/Switch'

const ActiveProcessesView = ({ addToast, showConfirm }) => {
  const { t } = useTranslation()
  const [processes, setProcesses] = useState([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(false)
  const [showAll, setShowAll] = useState(false)
  const [search, setSearch] = useState('')

  const requestRef = useRef(null)
  const killRef = useRef(null)
  const aliveRef = useRef(false)
  const refreshTimerRef = useRef(null)
  const fetchProcesses = useCallback(async () => {
    requestRef.current?.abort()
    const controller = createRequestScope()
    requestRef.current = controller
    try {
      const res = await apiFetch('/processes_list', { signal: controller.signal, cache: 'no-store' })
      if (!res.ok) throw new Error()
      const data = await res.json()
      if (controller.aborted) return
      if (!Array.isArray(data?.processes)) throw new Error()
      const entries = data.processes.filter(p => Number.isInteger(p.pid) && p.pid >= 0 &&
        typeof p.name === 'string' && Number.isFinite(p.memory) && p.memory >= 0)
      setProcesses(entries)
      setError(false)
    } catch {
      if (!controller.aborted) setError(true)
    } finally {
      if (!controller.aborted) setLoading(false)
    }
  }, [])
  const refresh = () => { setLoading(true); setError(false); void fetchProcesses() }
  useEffect(() => {
    aliveRef.current = true
    // StrictMode's setup/cleanup probe cancels this startup task before it can
    // issue a duplicate request; initial loading is already visible.
    const initialId = setTimeout(fetchProcesses, 0)
    const intervalId = setInterval(fetchProcesses, 15000)
    return () => {
      aliveRef.current = false
      clearTimeout(initialId)
      clearInterval(intervalId)
      clearTimeout(refreshTimerRef.current)
      requestRef.current?.abort()
      killRef.current?.abort()
    }
  }, [fetchProcesses])

  const filteredProcesses = useMemo(() => {
    let result = processes
    if (!showAll) {
      result = result.filter(p => p.is_daemon)
    }
    if (search.trim() !== '') {
      const q = search.toLowerCase()
      result = result.filter(p => p.name.toLowerCase().includes(q))
    }
    return result
  }, [processes, showAll, search])

  const handleKill = (proc) => {
    const isCritical = proc.name.startsWith('NayePlmTools') || proc.name === 'elfldr.elf';
    if (isCritical) {
      addToast(t("active_processes.cannot_kill", "Cannot kill {{name}}", { name: proc.name }), "error")
      return
    }

    showConfirm(
      t("active_processes.kill_modal_title", "Kill Process"),
      t("active_processes.kill_modal_message", "Are you sure you want to kill {{name}} (PID: {{pid}})?", { name: proc.name, pid: proc.pid }),
      async () => {
        if (!aliveRef.current) return
        const controller = createRequestScope()
        killRef.current = controller
        try {
          const res = await mutationFetch(`/process_kill?pid=${proc.pid}`, { signal: controller.signal })
          const data = await res.json()
          if (controller.aborted) return
          if (res.ok) {
            setProcesses(prev => prev.filter(p => p.pid !== proc.pid));
            addToast(t("active_processes.kill_success", "Successfully killed {{name}}", { name: proc.name }))
            clearTimeout(refreshTimerRef.current);
            refreshTimerRef.current = setTimeout(fetchProcesses, 1000);
          } else {
            addToast(data.error || t("active_processes.kill_failed", "Failed to kill {{name}}", { name: proc.name }), "error")
          }
        } catch {
          if (controller.aborted) return
          addToast(t("active_processes.kill_error", "Error killing {{name}}", { name: proc.name }), "error")
        }
      },
      /* [CN-FORK] v0.5.2.93 杀进程不可逆 → 危险档确认 */
      true
    )
  }

  return (
    <div className="space-y-6">
      {/* [CN-FORK] v0.5.2.94 页头改用共用 <PageHead>（粘顶 + 实色 rail 底）：「显示全部进程」
          这个开关原来跟着页头滚走，列表一长就得先滚回顶部才能改筛选。 */}
      <PageHead
        title={<>{t("active_processes.title_active", "Active")} <span className="text-ps-blue">{t("active_processes.title_processes", "Processes")}</span></>}
        actions={
          /* [CN-FORK] v0.5.2.94 这一处原来是全站第 4 种开关写法（h-6 w-11 + translate-x-6），
              现在与设置页共用 <Switch>，电视端同步抬大到 --switch-* 档。 */
          <div className="flex items-center">
            <span className="text-sm font-bold text-zinc-300 mr-3">
              {t("active_processes.show_all", "Show All System Processes")}
            </span>
            <Switch checked={showAll} onChange={setShowAll} label={t("active_processes.show_all", "Show All System Processes")} />
          </div>
        }
      />

      <Section
        id="process-list"
        icon={Cpu}
        title={t("active_processes.title_processes", "Processes")}
        count={loading && processes.length === 0 ? null : filteredProcesses.length}
        bodyClass="space-y-4"
        actions={
          <button
            onClick={() => fetchProcesses()}
            title={t("common.refresh", "刷新")}
            aria-label={t("common.refresh", "刷新")}
            className="icon-btn"
          >
            <RefreshCw className={cn(loading && "animate-spin")} />
          </button>
        }
      >
        {/* [CN-FORK] v0.5.2.94 搜索框与管理 Payload 页同一形状：.field 内嵌件底 + 就地图标 */}
        <div className="field flex items-center focus-within:border-ps-blue/50">
          <Search className="w-4 h-4 text-zinc-500 mr-2 shrink-0" />
          <input
            type="text"
            placeholder={t("active_processes.search_placeholder", "Search processes by name...")}
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            className="flex-1 min-w-0 bg-transparent border-none outline-none text-white font-medium placeholder:text-zinc-600 !p-0"
          />
        </div>

        {loading && processes.length === 0 ? (
          <LoadingState label={t("active_processes.fetching", "Fetching process list...")} />
        ) : error ? (
          /* [CN-FORK] 错误态仍走共用 <EmptyState>：本文件另一处空态（下面 no_processes 那处）
             用的就是它，且重试按钮按 v0.5.2.102 的约定走 btn-info 青档。 */
          <EmptyState
            icon={AlertTriangle}
            iconClassName="text-red-500 opacity-60"
            title={t("active_processes.error_loading", "Failed to load processes")}
            className="border border-red-500/20 rounded-xl bg-red-950/5"
            action={
              /* [CN-FORK] v0.5.2.102 重试从红档换成青档（btn-info＝刷新/重试类），红只留给 kill 这类不可逆 */
              <button onClick={refresh} className="btn btn-info">
                <RefreshCw className="w-4 h-4" />
                <span>{t("active_processes.retry", "Retry")}</span>
              </button>
            }
          />
        ) : filteredProcesses.length === 0 ? (
          <EmptyState icon={Cpu} title={t("active_processes.no_processes", "No processes found")} />
        ) : (
          <div className="space-y-3">
            {filteredProcesses.map((p) => {
              const critical = p.name.startsWith('NayePlmTools') || p.name === 'elfldr.elf';
              return (
                <div key={p.pid} className={cn(
                  "row-link flex flex-row items-center justify-between p-3 rounded-xl border border-white/10 bg-white/[0.02] hover:border-ps-blue/30 transition-colors"
                )}>
                  {/* [CN-FORK] v0.5.2.94 行去掉 .glass-card 的 hover 抬升：整行不是可点对象，
                      抬起来会误导「这行能点」；Kill 按钮也从 opacity-50 + 仅悬停显现改成常显，
                      触屏和遥控器没有 hover 态。 */}
                  <div className="flex items-center min-w-0 flex-1">
                    <div className={cn(
                      "p-3 rounded-xl flex-shrink-0",
                      p.is_daemon ? "bg-ps-blue/10 text-ps-blue" : "bg-white/5 text-zinc-400"
                    )}>
                      <Cpu className="w-5 h-5" />
                    </div>
                    <div className="min-w-0 flex-1 ml-3">
                      <h3 className="text-xl font-bold text-white truncate">{p.name}</h3>
                      <div className="flex items-center text-xs font-mono text-zinc-500 uppercase tracking-wider mt-1">
                        <span>PID: <span className="text-zinc-300">{p.pid}</span></span>
                        <span className="ml-4">MEM: <span className="text-zinc-300">{p.memory.toFixed(1)} MiB</span></span>
                      </div>
                    </div>
                  </div>
                  <button
                    onClick={() => handleKill(p)}
                    disabled={critical}
                    className="btn btn-danger ml-3 shrink-0"
                    title={critical ? t("active_processes.cannot_kill_tooltip", "Cannot kill critical process") : t("active_processes.kill_button", "Kill")}
                  >
                    <Trash2 className="w-4 h-4" />
                    <span>{t("active_processes.kill_button", "Kill")}</span>
                  </button>
                </div>
              )
            })}
          </div>
        )}

        <div className="flex items-start p-4 rounded-xl border border-white/5 bg-white/[0.01]">
          <Info className="w-5 h-5 shrink-0 text-ps-blue" />
          <div className="ml-3">
            <p className="font-bold text-white mb-1">{t("active_processes.note_title", "Note:")}</p>
            <p className="leading-relaxed">
              <Trans
                i18nKey="active_processes.note_message"
                defaults="Some payloads that inject threads into system processes (like <1>SceShellCore</1>) will persist inside those processes even after their main process is killed."
                components={{ 1: <code className="bg-white/10 px-1 py-0.5 rounded text-ps-blue font-mono text-sm" /> }}
              />
            </p>
          </div>
        </div>
      </Section>
    </div>
  )
}

export default ActiveProcessesView
