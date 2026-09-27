import React, { useState, useEffect, useCallback, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { Play, Power, Upload, RefreshCw } from 'lucide-react'

/* [CN-FORK] 设置页的 PKG Manager 管理卡片。侧栏「PKG安装」菜单已改为自动拉起并铺满
 * 右侧内容区直接跑它自带界面（:8844），本卡片保留低频管理功能：查看状态/版本、
 * 手动上传更新、启动或重启（上传新版后点一下即可切到新版本，其自身单实例互杀旧进程）、
 * 以及关闭（后端按进程名 SIGKILL，不跑就不占内存，也不会在 iframe 里误看到旧会话）。 */

const BTN_GHOST = 'inline-flex items-center gap-2 px-4 py-2 rounded-lg bg-white/5 border border-white/10 text-zinc-200 text-[14px] font-bold hover:bg-white/10 disabled:opacity-40 disabled:cursor-not-allowed transition'
const BTN_DANGER = 'inline-flex items-center gap-2 px-4 py-2 rounded-lg bg-red-500/10 border border-red-500/25 text-red-300 text-[14px] font-bold hover:bg-red-500/20 disabled:opacity-40 disabled:cursor-not-allowed transition'

export default function PkgManagerCard() {
  const { t } = useTranslation()
  const [status, setStatus] = useState(null)
  const [busy, setBusy] = useState('')
  const [msg, setMsg] = useState(null) // { ok: boolean, text: string }
  const fileRef = useRef(null)

  const load = useCallback(async () => {
    try {
      const res = await fetch(`/pkgmanager_status?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data && typeof data.running === 'boolean') setStatus(data)
    } catch { /* 服务未响应时保留上次状态 */ }
  }, [])

  useEffect(() => { load() }, [load])

  const launch = async () => {
    setBusy('launch')
    setMsg(null)
    try {
      const res = await fetch(`/pkgmanager_launch?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data?.ok) setMsg({ ok: true, text: t('pkg.launch_ok', '启动指令已发送，首次启动需要几秒') })
      else setMsg({ ok: false, text: data?.message || t('pkg.launch_fail', '启动失败') })
    } catch {
      setMsg({ ok: false, text: t('pkg.launch_fail', '启动失败') })
    }
    setBusy('')
    setTimeout(load, 1500)
  }

  /* 关闭：后端 pkgmanager_stop() 枚举进程并 SIGKILL pkg-manager.elf，只影响它自己。
   * 没在运行时按钮置灰，避开无意义调用。 */
  const stop = async () => {
    setBusy('stop')
    setMsg(null)
    try {
      const res = await fetch(`/pkgmanager_stop?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data?.ok) setMsg({ ok: true, text: t('pkg.stop_ok', '已发送关闭指令') })
      else setMsg({ ok: false, text: data?.message || t('pkg.stop_fail', '关闭失败') })
    } catch {
      setMsg({ ok: false, text: t('pkg.stop_fail', '关闭失败') })
    }
    setBusy('')
    setTimeout(load, 1200)
  }

  const upload = async (e) => {
    const file = e.target.files && e.target.files[0]
    if (!file) return
    setBusy('upload')
    setMsg(null)
    try {
      const res = await fetch(`/pkgmanager_upload?filename=${encodeURIComponent(file.name)}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: file
      })
      const data = await res.json().catch(() => null)
      if (data?.ok) setMsg({ ok: true, text: t('pkg.upload_ok', '已上传 v{{ver}}，重启 PKG Manager 后生效', { ver: data.version }) })
      else setMsg({ ok: false, text: data?.message || t('pkg.upload_fail', '上传失败') })
    } catch {
      setMsg({ ok: false, text: t('pkg.upload_fail', '上传失败') })
    }
    setBusy('')
    if (fileRef.current) fileRef.current.value = ''
    setTimeout(load, 500)
  }

  const running = !!(status && status.running)

  return (
    <section className="p-4 md:p-5 bg-white/[0.03] rounded-2xl border border-white/10 space-y-4">
      <div className="flex flex-wrap items-center gap-3">
        <span className={`w-2.5 h-2.5 rounded-full shrink-0 ${running ? 'bg-green-400' : 'bg-zinc-600'}`} />
        <span className="text-[15px] font-bold text-white">
          {running ? t('pkg.running', '运行中') : t('pkg.stopped', '未运行')}
        </span>
        {status && (
          <span className="text-[12px] text-zinc-500 tabular-nums">
            {t('pkg.versions', '当前 v{{installed}} · 内置基线 v{{embedded}}', {
              installed: status.installed_version,
              embedded: status.embedded_version
            })}
          </span>
        )}
      </div>

      <div className="flex flex-wrap items-center gap-3">
        <button onClick={launch} disabled={busy === 'launch'} className={BTN_GHOST}>
          <Play className="w-4 h-4" />
          {busy === 'launch' ? t('pkg.launching', '启动中…') : t('pkg.launch_restart', '启动 / 重启')}
        </button>
        <button onClick={stop} disabled={!running || busy === 'stop'} className={BTN_DANGER}
          title={running ? t('pkg.stop_hint', '停止 PKG Manager 守护进程（随时可重新启动）') : t('pkg.stopped', '未运行')}>
          {busy === 'stop' ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Power className="w-4 h-4" />}
          {busy === 'stop' ? t('pkg.stopping', '关闭中…') : t('pkg.stop', '关闭')}
        </button>
        <button onClick={() => fileRef.current && fileRef.current.click()} disabled={busy === 'upload'} className={BTN_GHOST}>
          {busy === 'upload' ? <RefreshCw className="w-4 h-4 animate-spin" /> : <Upload className="w-4 h-4" />}
          {busy === 'upload' ? t('pkg.uploading', '上传中…') : t('pkg.upload', '上传更新')}
        </button>
        <input ref={fileRef} type="file" accept=".elf,.bin" onChange={upload} className="hidden" />
        <button onClick={load} title={t('common.refresh', '刷新')} className="p-2 rounded-lg text-zinc-400 hover:text-white hover:bg-white/5 transition">
          <RefreshCw className="w-4 h-4" />
        </button>
      </div>

      <p className="text-[12px] text-zinc-500 leading-relaxed">
        {t('pkg.upload_hint', '更新方式为手动上传：请在电脑浏览器打开本页，选择新版本的 pkg-manager ELF 文件上传。上传会做 ELF 与版本签名校验并原子覆盖，重启 PKG Manager 后生效；内置基线仅作离线保底，不会覆盖已上传的文件。')}
      </p>

      {msg && (
        <p className={`text-[14px] font-bold ${msg.ok ? 'text-green-400' : 'text-red-400'}`}>
          {msg.text}
        </p>
      )}
    </section>
  )
}
