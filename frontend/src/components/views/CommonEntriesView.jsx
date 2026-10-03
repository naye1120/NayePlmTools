import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Plus, X, RefreshCw, ExternalLink, Check } from 'lucide-react'
import ThermalSection from './ThermalSection' /* [CN-FORK] 温控管理板块 */
import GameSection from './GameSection' /* [CN-FORK] 游戏管理板块 */

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

  const list = entries || []

  return (
    <div className="max-w-5xl mx-auto space-y-8 pb-12">
      {/* [CN-FORK] 右侧三段式布局：板块一 常用入口（沿用原 quicklinks 全部功能） */}
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
              <a href={e.url} target="_blank" rel="noreferrer" className="block p-4 space-y-2">
                <div className="flex items-center gap-2.5 pr-8">
                  {/* pr-8 是给右上角的删除按钮留位：它 absolute top-2 right-2，不预留就会和
                      本行末尾的外链图标叠在一起（实机反馈：链接与关闭按钮重合）。
                      name 上的 min-w-0 是 truncate 在 flex 子里真正生效的前提。 */}
                  <span className={`w-2.5 h-2.5 rounded-full shrink-0 ${e.alive ? 'bg-green-400' : 'bg-zinc-600'}`} title={e.alive ? t('entries.alive', '可连通') : t('entries.dead', '当前不可达')} />
                  <span className="text-[15px] font-bold text-white truncate min-w-0">{e.name}</span>
                  <ExternalLink className="w-3.5 h-3.5 text-zinc-500 shrink-0 ml-auto group-hover:text-ps-blue transition" />
                </div>
                <p className="text-[12px] text-zinc-500 break-all leading-relaxed">{e.url}</p>
              </a>
              {confirmIdx === i ? (
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

      {/* [CN-FORK] 板块三 游戏管理：读 /game_list 展示已安装 + 已存在的游戏（图标/名称/TID/来源） */}
      <GameSection />
    </div>
  )
}
