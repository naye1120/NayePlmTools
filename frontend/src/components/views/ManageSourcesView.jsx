import React, { useState, useEffect } from 'react'
import {
  Plus, Trash2, ChevronUp, ChevronDown,
  Lock, Globe, Loader2, AlertTriangle
} from 'lucide-react'
import { QRCodeSVG } from 'qrcode.react'
import { useTranslation } from 'react-i18next'
import { cn, isPS5 } from '../../utils/helpers'
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'

const ManageSourcesView = ({ onBack, ip, addToast, showConfirm }) => {
  const { t } = useTranslation()
  const [sources, setSources] = useState([])
  const [loading, setLoading] = useState(true)
  const [newUrl, setNewUrl] = useState('')
  const [adding, setAdding] = useState(false)
  const [addError, setAddError] = useState('')
  const [showAddForm, setShowAddForm] = useState(false)

  useEffect(() => {
    fetch('/sources_list')
      .then(r => r.json())
      .then(d => {
        if (d?.sources) setSources(d.sources)
      })
      .catch(() => { })
      .finally(() => setLoading(false))
  }, [])

  const saveSources = async (updated) => {
    try {
      const res = await fetch('/sources_set', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ sources: updated })
      })
      if (res.ok) {
        addToast(t("manage_sources.saved", "Sources saved"))
      } else {
        addToast(t("manage_sources.save_failed", "Failed to save sources"), 'error')
      }
    } catch {
      addToast(t("manage_sources.save_failed", "Failed to save sources"), 'error')
    }
  }

  const move = (idx, dir) => {
    if (idx + dir < 1 || idx + dir >= sources.length) return
    const updated = [...sources]
      ;[updated[idx], updated[idx + dir]] = [updated[idx + dir], updated[idx]]
    setSources(updated)
    saveSources(updated)
  }

  const remove = (idx) => {
    if (idx === 0) return
    const src = sources[idx]
    showConfirm(
      t("manage_sources.remove_title", "Remove Source"),
      t("manage_sources.remove_message", "Remove \"{{name}}\" from your sources?", { name: src.name }),
      () => {
        const updated = sources.filter((_, i) => i !== idx)
        setSources(updated)
        saveSources(updated)
      }
    )
  }

  const handleAdd = async (e) => {
    e.preventDefault()
    setAddError('')
    if (!newUrl.trim()) return
    setAdding(true)
    try {
      const res = await fetch(`/sources_add?url=${encodeURIComponent(newUrl.trim())}`)
      const data = await res.json()
      if (data.ok) {
        // Reload sources list
        const listRes = await fetch('/sources_list')
        const listData = await listRes.json()
        if (listData?.sources) setSources(listData.sources)
        setNewUrl('')
        setShowAddForm(false)
        addToast(t("manage_sources.added", "\"{{name}}\" added", { name: data.name }))
      } else {
        setAddError(data.message || t("manage_sources.add_failed", "Failed to add source"))
      }
    } catch {
      setAddError(t("manage_sources.add_request_failed", "Request failed. Check the URL and try again."))
    }
    setAdding(false)
  }

  /* ---- PS5 Mode: QR + URL only ---- */
  if (isPS5) {
    return (
      <div className="max-w-3xl mx-auto pb-12">
        <PageHead
          bleed={false}
          title={<>{t("manage_sources.ps5_title_1", "Manage")} <span className="text-ps-blue">{t("manage_sources.ps5_title_2", "Sources")}</span></>}
          back={onBack}
          backLabel={t("manage_sources.back_btn", "Back")}
        />
        <Section className="mt-6" headless bodyClass="flex flex-col items-center space-y-6">
          <div className="bg-white p-6 rounded-3xl">
            <QRCodeSVG value={`http://${ip}:8911`} size={180} level="M" />
          </div>
          {/* [CN-FORK] 同上：IP 地址用主题变量色，浅色主题直接跟随翻转 */}
          <code className="text-ps-text font-mono text-base font-black opacity-90 italic tracking-tight uppercase">
            {ip}:8911
          </code>
          <p className="text-zinc-400 text-center text-sm leading-relaxed max-w-md">
            {t("manage_sources.ps5_description", "Open this address on your phone or PC to manage payload sources.")}
          </p>
        </Section>
      </div>
    )
  }

  /* ---- Desktop Mode: full CRUD ---- */
  return (
    <div className="w-full max-w-3xl mx-auto pb-12 min-w-0">
      <PageHead
        bleed={false}
        title={<>{t("manage_sources.web_title_1", "Payload")} <span className="text-ps-blue">{t("manage_sources.web_title_2", "Sources")}</span></>}
        back={onBack}
        backLabel={t("manage_sources.back_btn", "Back")}
      />

      <div className="mt-6 space-y-4">
        <Section
          id="sources-list"
          icon={Globe}
          title={t("manage_sources.list_title", "Source list")}
          count={loading ? null : sources.length}
          bodyClass="space-y-3"
        >
          {loading ? (
            <LoadingState label={t("manage_sources.loading", "Loading sources...")} />
          ) : sources.length === 0 ? (
            <EmptyState
              icon={Globe}
              title={t("manage_sources.empty", "No sources yet")}
              desc={t("manage_sources.add_new_desc", "Paste the URL to a JSON file.")}
              action={
                <button onClick={() => { setShowAddForm(true); setAddError('') }} className="btn btn-primary">
                  <Plus className="w-4 h-4" />
                  <span>{t("manage_sources.add_source_btn", "Add Source")}</span>
                </button>
              }
            />
          ) : (
            sources.map((src, idx) => (
              /* [CN-FORK] v0.5.2.94 一套行 markup 取代原来的「手机一套 + 桌面一套」重复实现：
                  窄屏纵向（信息块在上、操作在下），≥640px 横向（信息块占满余位、操作贴右）。
                  两份拷贝此前已经不同步过（改一处忘一处），角色也统一成 .row-link 一档命中区。 */
              <div
                key={src.id}
                className={cn(
                  'row-link flex flex-col sm:flex-row sm:items-center p-4 rounded-xl border transition-colors w-full min-w-0 max-w-full overflow-hidden',
                  src.removable
                    ? 'bg-white/5 border-white/10 hover:border-ps-blue/30'
                    : 'bg-white/[0.015] border-white/5',
                  /* [CN-FORK] v0.5.1.65 默认云端仓库被关掉时整卡灰显（后端 enabled=false），不是从列表里抹掉 */
                  src.enabled === false && 'opacity-60'
                )}
              >
                <div className="flex items-center min-w-0 flex-1">
                  {/* Priority index */}
                  <div className={cn(
                    'w-9 h-9 rounded-xl flex items-center justify-center text-sm font-black shrink-0',
                    idx === 0 ? 'sel-chip' : 'bg-white/5 text-zinc-500'
                  )}>
                    {idx + 1}
                  </div>

                  {/* Icon */}
                  <div className="p-2 rounded-xl shrink-0 ml-2.5">
                    {src.removable
                      ? <Globe className="w-5 h-5 text-zinc-400 group-hover:text-ps-blue transition-colors" />
                      : <Lock className="w-5 h-5 text-ps-blue" />
                    }
                  </div>

                  <div className="min-w-0 ml-2.5 flex-1">
                    <p className="font-bold text-white text-base leading-tight truncate">{src.name}</p>
                    <p className="text-xs text-zinc-500 truncate mt-0.5 font-mono">{src.url}</p>
                  </div>
                </div>

                <div className="flex items-center shrink-0 sm:ml-3 mt-3 sm:mt-0">
                  {src.removable && (
                    <>
                      <button
                        onClick={() => move(idx, -1)}
                        disabled={idx <= 1}
                        title={t("manage_sources.move_up_btn", "Move up")}
                        aria-label={t("manage_sources.move_up_btn", "Move up")}
                        className="icon-btn"
                      >
                        <ChevronUp />
                      </button>
                      <button
                        onClick={() => move(idx, 1)}
                        disabled={idx === sources.length - 1}
                        title={t("manage_sources.move_down_btn", "Move down")}
                        aria-label={t("manage_sources.move_down_btn", "Move down")}
                        className="icon-btn"
                      >
                        <ChevronDown />
                      </button>
                      <button
                        onClick={() => remove(idx)}
                        title={t("manage_sources.remove_btn", "Remove source")}
                        aria-label={t("manage_sources.remove_btn", "Remove source")}
                        className="icon-btn hover:bg-red-500/10 hover:text-red-500"
                      >
                        <Trash2 />
                      </button>
                    </>
                  )}
                  {!src.removable && (
                    <span className="px-3 py-1 rounded-full text-[10px] font-bold uppercase tracking-widest text-ps-blue border border-ps-blue/20 bg-ps-blue/5">
                      {t("manage_sources.default_badge", "Default")}
                    </span>
                  )}
                  {src.enabled === false && (
                    <span className="ml-2 px-3 py-1 rounded-full text-[10px] font-bold uppercase tracking-widest text-zinc-500 border border-white/10 bg-white/5">
                      {t("manage_sources.disabled_badge", "Disabled")}
                    </span>
                  )}
                </div>
              </div>
            ))
          )}
        </Section>

        {/* Add Source */}
        {!showAddForm ? (
          sources.length > 0 && (
            <button
              onClick={() => { setShowAddForm(true); setAddError('') }}
              className="w-full min-h-[var(--row-h)] flex items-center justify-center border-2 border-dashed border-white/10 rounded-xl text-zinc-500 hover:text-ps-blue hover:border-ps-blue/30 transition-colors font-bold"
            >
              <Plus className="w-5 h-5 mr-2" />
              <span>{t("manage_sources.add_source_btn", "Add Source")}</span>
            </button>
          )
        ) : (
          <Section headless>
            <form onSubmit={handleAdd} className="space-y-4">
              <p className="font-bold text-white text-lg">{t("manage_sources.add_new_title", "Add a New Source")}</p>
              <p className="text-sm text-zinc-500">
                {t("manage_sources.add_new_desc", "Paste the URL to a JSON file.")}
              </p>
              <div className="stack-x">
                <input
                  type="url"
                  value={newUrl}
                  onChange={e => setNewUrl(e.target.value)}
                  placeholder="https://example.com/payloads.json"
                  className="field flex-1"
                  autoFocus
                  disabled={adding}
                />
                <div className="flex flex-1 sm:flex-none">
                  <button
                    type="submit"
                    disabled={adding || !newUrl.trim()}
                    className="btn btn-primary flex-1 sm:flex-none"
                  >
                    {adding ? <Loader2 className="w-4 h-4 animate-spin" /> : <Plus className="w-4 h-4" />}
                    <span>{adding ? t("manage_sources.validating", "Validating...") : t("manage_sources.add_btn", "Add")}</span>
                  </button>
                  <button
                    type="button"
                    onClick={() => { setShowAddForm(false); setNewUrl(''); setAddError('') }}
                    disabled={adding}
                    className="btn btn-outline flex-1 sm:flex-none ml-2"
                  >
                    {t("manage_sources.cancel_btn", "Cancel")}
                  </button>
                </div>
              </div>
              {addError && (
                <p className="flex items-center text-red-400 text-sm">
                  <AlertTriangle className="w-4 h-4 shrink-0 mr-2" />
                  <span>{addError}</span>
                </p>
              )}
            </form>
          </Section>
        )}
      </div>
    </div>
  )
}

export default ManageSourcesView
