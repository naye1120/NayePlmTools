import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Plus, X, RefreshCw, ExternalLink, Check } from 'lucide-react'
import ThermalSection from './ThermalSection' /* [CN-FORK] 温控管理板块 */

/* [CN-FORK] 「常用入口」页：手动维护的快捷访问清单（IP:port 类服务导航）。
 * 数据存 payload 侧 /data/nayeplmtools/quicklinks.json（非浏览器 localStorage），
 * 手机/电脑/PS5 打开本 UI 看到的是同一份。列表接口顺带对每条 URL 的 host:port
 * 做 TCP 探活：绿点=可连通、灰点=不可达，点卡片新标签打开。
 * 本机已知服务自动探测表暂未实现，后续在 /quicklinks 里合并生成即可，前端无感。 */

const BTN_PRIMARY = 'inline-flex items-center gap-2 px-4 py-2 rounded-lg bg-ps-blue text-white text-[14px] font-bold hover:opacity-90 disabled:opacity-40 disabled:cursor-not-allowed transition'
const BTN_GHOST = 'inline-flex items-center gap-2 px-3 py-1.5 rounded-lg bg-white/5 border border-white/10 text-zinc-200 text-[13px] font-bold hover:bg-white/10 disabled:opacity-40 disabled:cursor-not-allowed transition'
const INPUT_CLS = 'w-full px-3 py-2 rounded-lg bg-black/30 border border-white/10 text-[14px] text-white placeholder-zinc-500 focus:outline-none focus:border-ps-blue'

export default function CommonEntriesView() {
  const { t } = useTranslation()
  const [entries, setEntries] = useState(null) // null=未加载
  const [busy, setBusy] = useState(false)
  const [msg, setMsg] = useState(null) // { ok, text }
  const [adding, setAdding] = useState(false)
  const [form, setForm] = useState({ name: '', url: '' })
  const [confirmIdx, setConfirmIdx] = useState(-1)
  const [startingIdx, setStartingIdx] = useState(-1) // [CN-FORK] 程序类入口正在启动的卡片下标

  const load = useCallback(async () => {
    try {
      const res = await fetch(`/quicklinks?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data && Array.isArray(data.entries)) setEntries(data.entries)
    } catch { /* 服务未响应时保留上次清单 */ }
  }, [])

  useEffect(() => { load() }, [load])

  const save = async () => {
    let url = form.url.trim()
    // 少打字容错：没写协议前缀自动补 http://（https 需显式书写）
    if (url && !/^https?:\/\//i.test(url)) url = `http://${url}`
    if (!form.name.trim() || !url) return
    setBusy(true)
    setMsg(null)
    try {
      const res = await fetch(
        `/quicklinks_add?name=${encodeURIComponent(form.name.trim())}&url=${encodeURIComponent(url)}&_=${Date.now()}`,
        { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data?.ok) {
        setForm({ name: '', url: '' })
        setAdding(false)
        setMsg({ ok: true, text: t('entries.saved', '已添加') })
      } else {
        setMsg({ ok: false, text: data?.message || t('entries.save_fail', '添加失败') })
      }
    } catch {
      setMsg({ ok: false, text: t('entries.save_fail', '添加失败') })
    }
    setBusy(false)
    load()
  }

  const remove = async (index) => {
    setConfirmIdx(-1)
    setBusy(true)
    setMsg(null)
    try {
      const res = await fetch(`/quicklinks_remove?index=${index}&_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      setMsg(data?.ok
        ? { ok: true, text: t('entries.deleted', '已删除') }
        : { ok: false, text: data?.message || t('entries.delete_fail', '删除失败') })
    } catch {
      setMsg({ ok: false, text: t('entries.delete_fail', '删除失败') })
    }
    setBusy(false)
    load()
  }

  /* [CN-FORK] 程序类入口（program=pkgmanager，如 PKG Manager :8844）点击时以「端口存活」为准：
   * 端口存活（服务已就绪）→ 直接打开；不存活 → 试着调 /pkgmanager_launch 拉起（elfldr），再轮询
   * 端口直至存活后打开。空白标签页必须在点击手势里同步 window.open（异步后再开会被弹窗拦截），
   * 先写「正在启动」占位文档避免白屏。 */
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

  const openProgram = async (entry, index) => {
    if (startingIdx !== -1) return
    const tip = t('entries.starting_prog', '正在启动 {{name}}…', { name: entry.name })
    const w = window.open('', '_blank')
    if (w) {
      try {
        w.document.write(`<!doctype html><title>${tip}</title><body style="margin:0;height:100vh;display:flex;align-items:center;justify-content:center;background:#101014;color:#a1a1aa;font:600 15px/1.6 system-ui,sans-serif">${tip}</body>`)
        w.document.close()
      } catch { /* 占位文档写失败不影响启动流程 */ }
    }
    setStartingIdx(index)
    setMsg(null)
    try {
      /* [CN-FORK] running 即主机对 127.0.0.1:8844 的 TCP 探活结果；端口活=服务已就绪可直接打开 */
      const portAlive = () =>
        fetch('/pkgmanager_status', { cache: 'no-store' })
          .then((r) => r.json()).then((s) => !!(s && s.running)).catch(() => false)
      let alive = await portAlive()
      if (!alive) {
        /* 端口不存活：试着拉起。launch 应答失败也不短路，给端口一小段缓冲再定论 */
        const lr = await fetch('/pkgmanager_launch', { cache: 'no-store' })
          .then((r) => r.json()).catch(() => null)
        const maxWait = lr && !lr.ok ? 4 : 40 // 明确失败 2s 缓冲；否则 20s 等待启动
        for (let k = 0; k < maxWait && !alive; k++) {
          await sleep(500)
          alive = await portAlive()
        }
        if (!alive) throw new Error((lr && !lr.ok && lr.message) || t('entries.start_fail', '启动失败'))
      }
      if (w) {
        if (!w.closed) w.location.href = entry.url
      } else {
        const w2 = window.open(entry.url, '_blank')
        if (!w2) window.location.replace(entry.url)
      }
    } catch (err) {
      if (w) { try { w.close() } catch { /* 忽略 */ } }
      setMsg({ ok: false, text: (err && err.message) || t('entries.start_fail', '启动失败') })
    }
    setStartingIdx(-1)
    load()
  }

  const list = entries || []

  return (
    <div className="max-w-5xl mx-auto space-y-8 pb-12">
      {/* [CN-FORK] 布局：板块一 常用入口（沿用原 quicklinks 全部功能）；板块二 温控管理。
          游戏管理板块已抽出为独立页（侧栏「游戏管理」），不再挂在本页。 */}
      <section className="bg-white/5 border border-white/10 rounded-2xl p-5 md:p-6 space-y-6">
      <div className="flex flex-wrap items-end justify-between gap-4">
        <div className="space-y-4">
          {/* [CN-FORK] 大标题文案独立于侧栏：侧栏保持「常用配置」，本页标题用「常用入口」 */}
          <h2 className="text-2xl font-bold text-white tracking-tight">
            {t('entries.title', '常用入口')}
          </h2>
          <p className="text-[14px] text-zinc-400 max-w-2xl">
            {t('entries.desc', '常用插件与局域网服务的快捷访问清单（如 http://192.168.1.214:8844）。条目保存在主机上，手机、电脑、PS5 打开本页看到的都是同一份；绿点表示当前可连通。')}
          </p>
        </div>
        <div className="flex items-center gap-3">
          {!adding && (
            <button onClick={() => { setAdding(true); setMsg(null) }} className={BTN_PRIMARY}>
              <Plus className="w-4 h-4" />
              {t('entries.add', '添加入口')}
            </button>
          )}
          <button onClick={load} title={t('common.refresh', '刷新')} className="p-2 rounded-lg text-zinc-400 hover:text-white hover:bg-white/5 transition">
            <RefreshCw className="w-4 h-4" />
          </button>
        </div>
      </div>

      {adding && (
        <section className="bg-white/5 border border-ps-blue/40 rounded-2xl p-5">
          <div className="grid grid-cols-1 md:grid-cols-[200px_1fr_auto] gap-3 items-end">
            <div className="space-y-1.5">
              <label className="text-[12px] text-zinc-400">{t('entries.name', '名称')}</label>
              <input
                className={INPUT_CLS}
                value={form.name}
                maxLength={127}
                placeholder={t('entries.name_ph', '如：PKG Manager')}
                onChange={(e) => setForm({ ...form, name: e.target.value })}
              />
            </div>
            <div className="space-y-1.5">
              <label className="text-[12px] text-zinc-400">{t('entries.url', '地址')}</label>
              <input
                className={INPUT_CLS}
                value={form.url}
                maxLength={511}
                placeholder="http://192.168.1.100:8080"
                onChange={(e) => setForm({ ...form, url: e.target.value })}
                onKeyDown={(e) => { if (e.key === 'Enter') save() }}
              />
            </div>
            <div className="flex items-center gap-2 pb-0.5">
              <button onClick={save} disabled={busy || !form.name.trim() || !form.url.trim()} className={BTN_PRIMARY}>
                <Check className="w-4 h-4" />
                {t('entries.save', '保存')}
              </button>
              <button onClick={() => { setAdding(false); setMsg(null) }} className={BTN_GHOST}>
                {t('entries.cancel', '取消')}
              </button>
            </div>
          </div>
          <p className="text-[12px] text-zinc-500 mt-3">
            {t('entries.url_hint', '协议前缀可省略，默认按 http:// 处理；https 请完整书写。')}
          </p>
        </section>
      )}

      {entries === null ? (
        <div className="flex justify-center py-16">
          <RefreshCw className="w-5 h-5 text-zinc-500 animate-spin" />
        </div>
      ) : list.length === 0 && !adding ? (
        <p className="text-[14px] text-zinc-500 py-10 text-center border border-dashed border-white/10 rounded-2xl">
          {t('entries.empty', '还没有入口，点右上角「添加入口」开始')}
        </p>
      ) : (
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
          {list.map((e, i) => (
            <div key={`${e.url}-${i}`} className="relative group bg-white/5 border border-white/10 rounded-2xl hover:border-ps-blue/50 transition">
              <a
                href={e.url}
                target="_blank"
                rel="noreferrer"
                className="block p-4 space-y-2"
                onClick={(ev) => {
                  /* [CN-FORK] 程序类入口接管点击（起停由本服务代理）；其余条目保持默认新标签打开 */
                  if (e.program === 'pkgmanager') {
                    ev.preventDefault()
                    openProgram(e, i)
                  }
                }}
              >
                <div className="flex items-center gap-2.5 pr-8">
                  {/* pr-8 是给右上角的删除按钮留位：它 absolute top-2 right-2，不预留就会和
                      本行末尾的外链图标叠在一起（实机反馈：链接与关闭按钮重合）。
                      name 上的 min-w-0 是 truncate 在 flex 子里真正生效的前提。 */}
                  <span className={`w-2.5 h-2.5 rounded-full shrink-0 ${e.alive ? 'bg-green-400' : 'bg-zinc-600'}`} title={e.alive ? t('entries.alive', '可连通') : t('entries.dead', '当前不可达')} />
                  <span className="text-[15px] font-bold text-white truncate min-w-0">{e.name}</span>
                  {e.src === 'auto' && (
                    <span className="shrink-0 px-1.5 py-0.5 rounded bg-ps-blue/15 text-ps-blue text-[11px] font-bold leading-none">
                      {t('entries.auto_tag', '自动')}
                    </span>
                  )}
                  <ExternalLink className="w-3.5 h-3.5 text-zinc-500 shrink-0 ml-auto group-hover:text-ps-blue transition" />
                </div>
                <p className="text-[12px] text-zinc-500 break-all leading-relaxed">{e.url}</p>
              </a>
              {startingIdx === i ? (
                <div className="absolute inset-0 bg-black/80 rounded-2xl flex flex-col items-center justify-center gap-2.5 px-4">
                  <RefreshCw className="w-5 h-5 text-ps-blue animate-spin" />
                  <p className="text-[13px] text-white text-center">
                    {t('entries.starting_prog', '正在启动 {{name}}…', { name: e.name })}
                  </p>
                </div>
              ) : confirmIdx === i ? (
                <div className="absolute inset-0 bg-black/80 rounded-2xl flex flex-col items-center justify-center gap-3 px-4">
                  <p className="text-[13px] text-white text-center">
                    {t('entries.confirm_delete', '删除「{{name}}」？', { name: e.name })}
                  </p>
                  <div className="flex items-center gap-2">
                    <button onClick={() => remove(i)} className="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-lg bg-red-500/80 text-white text-[13px] font-bold hover:bg-red-500 transition">
                      <X className="w-3.5 h-3.5" />
                      {t('entries.confirm_yes', '删除')}
                    </button>
                    <button onClick={() => setConfirmIdx(-1)} className={BTN_GHOST}>
                      {t('entries.confirm_no', '取消')}
                    </button>
                  </div>
                </div>
              ) : (
                <button
                  onClick={() => setConfirmIdx(i)}
                  title={t('entries.delete', '删除')}
                  className="absolute top-2 right-2 p-1.5 rounded-lg text-zinc-600 hover:text-red-400 hover:bg-white/10 transition"
                >
                  <X className="w-4 h-4" />
                </button>
              )}
            </div>
          ))}
        </div>
      )}

      {msg && (
        <p className={`text-[14px] font-bold ${msg.ok ? 'text-green-400' : 'text-red-400'}`}>
          {msg.text}
        </p>
      )}
      </section>

      {/* [CN-FORK] 板块二 温控管理：读 /thermal_stats 展示 CPU/GPU/SSD 温度 + 风扇 */}
      <ThermalSection />
    </div>
  )
}
