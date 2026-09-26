import React, { useState, useEffect, useMemo } from 'react'
import { CloudDownload, Upload, Package, Database, RefreshCw, Trash2, Loader2, AlertTriangle, HardDrive, Usb, ChevronDown, Globe, Search } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { QRCodeSVG } from 'qrcode.react'
import { cn, isPS5, isIOS, parsePayloadName } from '../../utils/helpers'
import PayloadName from '../ui/PayloadName'

const PayloadItem = ({ p, multiSources, isPS5, onInstall, srcId, srcUrl }) => {
  const { t } = useTranslation()
  return (
  <div
    className={cn(
      /* [CN-FORK] 风格统一：列表行紧凑内边距，与全局 text-sm 尺度对齐 */
      "flex flex-col md:flex-row justify-between gap-3 md:gap-6 p-4 transition-all",
      multiSources
        ? "hover:bg-white/[0.03]"
        : "bg-white/[0.02] border border-white/10 rounded-xl hover:border-ps-blue/40 hover:bg-white/[0.04]",
      isPS5 ? "flex-row items-center" : "items-start md:items-center"
    )}
  >
    <div className="space-y-1 min-w-0">
      <PayloadName path={p.filename} version={p.version} className="text-sm md:text-base text-white font-semibold" stacked lastUpdate={p.last_update} />
      {p.description && (
        <p className="text-xs md:text-sm text-zinc-400 font-medium leading-relaxed">{p.description}</p>
      )}
    </div>
    <button
      onClick={() => onInstall(p, srcId === 'legacy-repo' ? null : srcId, srcUrl)}
      className={cn(
        "flex items-center justify-center gap-2 px-4 py-2 font-semibold text-sm transition-all shrink-0",
        p.isUpdate
          ? "bg-emerald-600 hover:bg-emerald-500 text-white rounded-lg shadow-[0_4px_14px_rgba(5,150,105,0.3)]"
          : "btn-primary px-6",
        isPS5 ? "w-auto px-8" : "w-full md:w-auto"
      )}
    >
      <CloudDownload className="w-4 h-4" />
      <span>{p.isUpdate ? t("storage_hub.update_btn", "Update") : t("storage_hub.install_btn", "Install")}</span>
    </button>
  </div>
)
}

const CategoryGroup = ({ category, payloads, multiSources, isPS5, onInstall, srcId, srcUrl, defaultExpanded = false }) => {
  const [expanded, setExpanded] = useState(defaultExpanded)
  return (
    <div className="border-b border-white/5 last:border-0">
      <button 
        onClick={() => setExpanded(!expanded)}
        className="w-full flex items-center justify-between px-4 py-3.5 hover:bg-white/5 transition-colors group/cat"
      >
        {/* [CN-FORK] 分类行：主色竖条 + chip 计数 */}
        <span className="flex items-center gap-2.5 font-semibold text-white/90 text-sm">
          <span className="w-1 h-4 rounded-full bg-ps-blue/70 group-hover/cat:bg-ps-blue transition-colors" />
          {category}
        </span>
        <span className="flex items-center gap-2">
          <span className="chip chip-muted">{payloads.length}</span>
          <ChevronDown className={cn("w-5 h-5 text-zinc-500 transition-transform", expanded && "rotate-180")} />
        </span>
      </button>
      {expanded && (
        <div className={cn(
          multiSources ? "divide-y divide-white/5 bg-black/20" : "grid grid-cols-1 gap-2 p-3",
          "border-t border-white/5"
        )}>
          {payloads.map(p => (
            <PayloadItem 
              key={p.filename} 
              p={p} 
              multiSources={multiSources} 
              isPS5={isPS5} 
              onInstall={onInstall} 
              srcId={srcId} 
              srcUrl={srcUrl} 
            />
          ))}
        </div>
      )}
    </div>
  )
}

const StorageHub = ({ payloads, payloadMeta, onInstall, onDelete, onUpload, onImportFromUsb, config, ip, scrollTarget, onClearScrollTarget }) => {
  const { t, i18n } = useTranslation()
  const multiSources = config?.MULTI_SOURCES_ENABLED === true

  const [repoData, setRepoData] = useState(null)   // null = not loaded yet
  const [loading, setLoading] = useState(false)
  const [error, setError] = useState(false)
  const [expandedSource, setExpandedSource] = useState(null) // id of expanded catalog
  const [search, setSearch] = useState('')
  const [sortMode, setSortMode] = useState(() => {
    return localStorage.getItem('repoSortMode') || 'category'
  })

  const handleSortChange = (mode) => {
    setSortMode(mode)
    localStorage.setItem('repoSortMode', mode)
  }

  const fetchRemote = async (force = false) => {
    setLoading(true)
    setError(false)
    try {
      const endpoint = force ? '/repository_refresh' : '/repository_payloads'
      const res = await fetch(endpoint)
      if (!res.ok) throw new Error()
      const data = await res.json()
      setRepoData(data)

      // For multi-source: auto-expand first source with payloads
      if (data?.sources?.length > 0 && expandedSource === null) {
        const first = data.sources.find(s => s.payloads?.length > 0)
        if (first) setExpandedSource(first.id)
      }

      // Legacy single-source: auto-refresh if older than 24h
      if (!force && data?.last_update) {
        // eslint-disable-next-line react-hooks/purity
        const now = Math.floor(Date.now() / 1000)
        if (now - Number(data.last_update) > 24 * 60 * 60) {
          await fetchRemote(true)
          return
        }
      }
    } catch {
      setError(true)
    } finally {
      setLoading(false)
    }
  }

  useEffect(() => { fetchRemote() }, [])

  useEffect(() => {
    if (scrollTarget) {
      const timer = setTimeout(() => {
        const element = document.getElementById(scrollTarget);
        if (element) element.scrollIntoView({ behavior: 'smooth' });
        if (onClearScrollTarget) onClearScrollTarget();
      }, 300);
      return () => clearTimeout(timer);
    }
  }, [scrollTarget]);

  const localFilenames = useMemo(() => payloads.map(p => p.split('/').pop()), [payloads])
  const internalPayloads = payloads.filter(p => !p.includes('/mnt/usb'))

  const getBaseName = (filename) => {
    if (!filename) return '';
    let clean = filename.replace(/\.(elf|bin)$/i, '');
    const versionMatch = clean.match(/[_-]v?(\d+[\d.a-z-]+)/i);
    if (versionMatch) clean = clean.replace(versionMatch[0], '');
    return clean.replace(/[_-]ps[45]$/i, '');
  }

  /* ---- Derive remote status for a flat list of payloads ---- */
  const enrichPayloads = (list) =>
    list.map(p => {
      const isInstalled = p.filename ? localFilenames.includes(p.filename) : false
      const baseName = getBaseName(p.filename)
      const installedVersion = localFilenames.find(f => getBaseName(f) === baseName)
      const isUpdate = !isInstalled && !!installedVersion
      return { ...p, isInstalled, isUpdate, installedFilename: installedVersion }
    }).sort((a, b) => {
      if (a.isUpdate && !b.isUpdate) return -1
      if (!a.isUpdate && b.isUpdate) return 1
      
      const nameA = (a.name || a.filename || '').toLowerCase()
      const nameB = (b.name || b.filename || '').toLowerCase()
      return nameA.localeCompare(nameB)
    })

  /* ---- Source-grouped data ---- */
  const enrichedSources = useMemo(() => {
    if (multiSources && repoData?.sources) {
      return repoData.sources.map(src => ({
        ...src,
        id: src.id || src.url,
        payloads: enrichPayloads(src.payloads || [])
      }))
    } else if (!multiSources && repoData?.payloads) {
      return [{
        id: 'legacy-repo',
        name: t("storage_hub.default_repository", "Default Repository"),
        url: repoData.repo_url || '',
        last_update: repoData.last_update || 0,
        payloads: enrichPayloads(repoData.payloads)
      }]
    }
    return []
  }, [repoData, multiSources, localFilenames, t])

  const legacyRepoUrl = repoData?.repo_url || ''

  /* ---- Source badge helper: look up source name from metadata ---- */
  const getSourceBadge = (fileName) => {
    if (!multiSources || !payloadMeta) return null
    const meta = payloadMeta[fileName]
    return meta?.source_name || meta?.install_source_detail || null
  }

  return (
    <div className="space-y-6">
      {/* [CN-FORK] 页头升级为 hero 面板：渐变主标题 + 服务器地址胶囊 + 渐变上传按钮 */}
      <div className="panel-card card-enter px-5 py-6 flex flex-col md:flex-row md:items-center justify-between gap-4">
        <div className="min-w-0">
          <p className="text-[11px] font-semibold uppercase tracking-[0.18em] text-zinc-500 mb-1">NayePlmTools</p>
          <h2 className="text-2xl font-bold tracking-tight bg-gradient-to-r from-white via-white to-ps-blue bg-clip-text text-transparent">
            {t("storage_hub.title_1", "Payload")} {t("storage_hub.title_2", "Management")}
          </h2>
        </div>

        {!isPS5 && (
          <div className="flex items-center gap-3 shrink-0">
            <span className="chip chip-muted font-mono">{ip}:8911</span>
            <label className="btn-primary inline-flex items-center gap-2 px-4 py-2 text-sm cursor-pointer shrink-0">
              <Upload className="w-4 h-4" />
              <span>{t("storage_hub.upload_btn", "Upload ELF Payload")}</span>
              <input type="file" className="hidden" onChange={onUpload} accept={isIOS ? undefined : ".elf,.bin"} />
            </label>
          </div>
        )}
      </div>

      {/* Installed Payloads Section */}
      {/* [CN-FORK] 风格统一：板块改用 panel-card 容器 + 标准板块头（图标盒/标题/计数/操作右侧），三板块结构一致 */}
      <section className="panel-card">
        <header className="panel-head">
          <div className="flex items-center gap-3 min-w-0">
            {/* [CN-FORK] 彩色渐变图标盒 */}
            <div className="p-2.5 rounded-xl bg-gradient-to-br from-ps-blue/30 to-ps-blue/5 ring-1 ring-inset ring-ps-blue/20 shrink-0">
              <Database className="w-[18px] h-[18px] text-ps-blue" />
            </div>
            <h3 className="text-sm font-semibold text-white truncate">{t("storage_hub.installed_title", "Installed Payloads")}</h3>
          </div>
          <span className="chip shrink-0">
            {t("storage_hub.files_count", "{{count}} File", { count: internalPayloads.length })}
          </span>
        </header>

        <div className={cn("p-4 grid gap-3", isPS5 ? "grid-cols-2" : "grid-cols-1 sm:grid-cols-2 lg:grid-cols-3")}>
          {internalPayloads.length === 0 ? (
            <div className="col-span-full py-12 border-2 border-dashed border-white/10 rounded-xl flex flex-col items-center justify-center space-y-3">
              <Package className="w-10 h-10 text-white/10" />
              <p className="text-zinc-500 font-medium text-sm">{t("storage_hub.library_empty", "Library Empty")}</p>
            </div>
          ) : (
            internalPayloads.map((path) => {
              const fileName = path.split('/').pop()
              const sourceBadge = getSourceBadge(fileName)
              // Find update in all sources (multi or legacy)
              const allRemote = enrichedSources.flatMap(s => s.payloads)
              const remoteMatch = allRemote.find(rp => rp.filename === fileName || rp.installedFilename === fileName)
              const remoteVersion = remoteMatch?.version || (remoteMatch?.filename ? parsePayloadName(remoteMatch.filename).version : null)
              return (
                <div key={path} className="group card-enter flex flex-col justify-center p-3.5 bg-white/[0.02] border border-white/10 rounded-xl hover:border-ps-blue/40 hover:bg-white/[0.04] hover:shadow-[0_0_20px_rgba(0,149,255,0.06)] transition-all gap-2 relative overflow-hidden">
                  <div className="flex flex-row items-center justify-between w-full gap-3">
                    <div className="flex items-center space-x-3 min-w-0 flex-1">
                      <div className="p-2.5 bg-white/5 rounded-xl group-hover:bg-ps-blue/15 group-hover:shadow-[0_0_14px_rgba(0,149,255,0.15)] transition-all shrink-0">
                        <Package className="w-4 h-4 text-zinc-400 group-hover:text-ps-blue transition-colors" />
                      </div>
                      <div className="min-w-0 flex-1 space-y-0.5">
                        <PayloadName path={fileName} className="text-sm text-white font-semibold" stacked />
                        {sourceBadge && (
                          <div className="flex items-center gap-1 text-zinc-500 text-[11px] select-none font-medium">
                            <Globe className="w-3.5 h-3.5" />
                            <span>{sourceBadge}</span>
                          </div>
                        )}
                      </div>
                    </div>
                    <div className="flex items-center shrink-0">
                      <button
                        onClick={() => onDelete(fileName)}
                        className="p-2 rounded-lg bg-transparent text-zinc-500 border border-transparent hover:bg-red-500/10 hover:text-red-500 hover:border-red-500/30 transition-all"
                        title={t("storage_hub.remove_payload", "Remove Payload")}
                      >
                        <Trash2 className="w-4 h-4" />
                      </button>
                    </div>
                  </div>
                  {remoteMatch?.isUpdate && (
                    <button
                      onClick={() => onInstall(remoteMatch, remoteMatch.source_id, legacyRepoUrl)}
                      className="w-full flex items-center justify-center gap-2 py-1.5 bg-emerald-600 hover:bg-emerald-500 text-white rounded-lg font-semibold text-xs transition-all shadow-[0_4px_14px_rgba(5,150,105,0.25)]"
                    >
                      <RefreshCw className="w-3.5 h-3.5" />
                      <span>
                        {remoteVersion
                          ? t("storage_hub.update_to_version", "Update (to v{{version}})", { version: remoteVersion.replace(/^v/i, '') })
                          : t("storage_hub.update_btn", "Update")}
                      </span>
                    </button>
                  )}
                </div>
              )
            })
          )}
        </div>
      </section>

      {/* Cloud Repository Section */}
      {/* [CN-FORK] 风格统一：同样改用 panel-card + 标准板块头，刷新按钮收进头部右侧 */}
      <section id="cloud-repository" className="panel-card">
        <header className="panel-head">
          <div className="flex items-center gap-3 min-w-0">
            <div className="p-2.5 rounded-xl bg-gradient-to-br from-ps-blue/30 to-ps-blue/5 ring-1 ring-inset ring-ps-blue/20 shrink-0">
              <CloudDownload className="w-[18px] h-[18px] text-ps-blue" />
            </div>
            <h3 className="text-sm font-semibold text-white truncate">{t("storage_hub.cloud_repo_title", "Cloud Repository")}</h3>
          </div>
          <button onClick={() => fetchRemote(true)} className="btn-ghost p-2 text-zinc-500 hover:text-ps-blue" title={t("storage_hub.retry_btn", "Retry Connection")}>
            <RefreshCw className={cn("w-4 h-4", loading && "animate-spin")} />
          </button>
        </header>

        <div className="p-4 space-y-4">
        <div className="flex flex-col md:flex-row gap-3">
          <div className="flex-1 flex items-center bg-black/40 border border-white/10 rounded-xl px-3.5 py-2.5 focus-within:border-ps-blue/60 focus-within:ring-2 focus-within:ring-ps-blue/15 focus-within:bg-black/60 transition-all">
            <Search className="w-4 h-4 text-zinc-500 mr-3 shrink-0" />
            <input
              type="text"
              placeholder={t("storage_hub.search_placeholder", "Search payloads by name or description...")}
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              className="bg-transparent border-none outline-none text-white w-full text-sm font-medium placeholder:text-zinc-600"
            />
          </div>
          <div className="relative flex-shrink-0 flex items-center bg-black/40 border border-white/10 rounded-xl pl-3.5 pr-9 py-2.5 hover:border-white/20 focus-within:border-ps-blue/60 focus-within:ring-2 focus-within:ring-ps-blue/15 transition-all cursor-pointer">
            <span className="text-zinc-500 text-xs font-medium mr-2 whitespace-nowrap">
              {t("storage_hub.sort_by", "Sort by:")}
            </span>
            <span className="text-white font-semibold text-xs md:text-sm">
              {sortMode === 'category' && t("storage_hub.sort_category", "Category")}
              {sortMode === 'update_time' && t("storage_hub.sort_time", "Update Time")}
              {sortMode === 'name' && t("storage_hub.sort_name", "Name")}
            </span>
            <select 
               value={sortMode}
               onChange={e => handleSortChange(e.target.value)}
               className="absolute inset-0 w-full h-full opacity-0 cursor-pointer"
             >
               <option value="category" className="bg-[#121214]">{t("storage_hub.sort_category", "Category")}</option>
               <option value="update_time" className="bg-[#121214]">{t("storage_hub.sort_time", "Update Time")}</option>
               <option value="name" className="bg-[#121214]">{t("storage_hub.sort_name", "Name")}</option>
            </select>
            <div className="absolute inset-y-0 right-0 flex items-center pr-3 pointer-events-none text-zinc-500">
              <ChevronDown className="w-5 h-5" />
            </div>
          </div>
        </div>

        {loading && !repoData ? (
          <div className="py-16 flex flex-col items-center justify-center space-y-4">
            <Loader2 className="w-8 h-8 text-ps-blue animate-spin" />
            <p className="text-zinc-500 text-sm">{t("storage_hub.syncing", "Syncing with Repository...")}</p>
          </div>
        ) : error ? (
          <div className="py-12 rounded-xl border border-red-500/20 flex flex-col items-center justify-center space-y-4 bg-red-950/5">
            <AlertTriangle className="w-10 h-10 text-red-500 opacity-60" />
            <div className="text-center">
              <p className="text-sm font-semibold text-white">{t("storage_hub.repo_unavailable", "Repository Unavailable")}</p>
              <p className="text-zinc-500 text-xs mt-1">{t("storage_hub.repo_unavailable_desc", "Check your internet connection and try again.")}</p>
            </div>
            <button onClick={() => fetchRemote(true)} className="btn-ghost px-5 py-2 text-xs">{t("storage_hub.retry_btn", "Retry Connection")}</button>
          </div>
        ) : enrichedSources.length > 0 ? (
          /* ===== REPOSITORY CATALOGS ===== */
          <div className="space-y-4">
            {enrichedSources.map(src => {
              let availablePayloads = src.payloads.filter(p => !p.isInstalled || p.isUpdate)
              
              if (search.trim() !== '') {
                const q = search.toLowerCase()
                availablePayloads = availablePayloads.filter(p => 
                  (p.name && p.name.toLowerCase().includes(q)) || 
                  (p.filename && p.filename.toLowerCase().includes(q)) || 
                  (p.description && p.description.toLowerCase().includes(q))
                )
              }
              
              // Auto-expand if there's only 1 source, otherwise respect state or active search
              const isExpanded = (enrichedSources.length === 1) || expandedSource === src.id || search.trim() !== ''

              let hasMultipleCategories = false
              let groupedPayloads = {}
              let categories = []

              if (sortMode === 'category' && search.trim() === '') {
                groupedPayloads = availablePayloads.reduce((acc, p) => {
                  // If p.category exists, attempt to translate it using its English name as key.
                  const catName = p.category ? t(`category.${p.category}`, p.category) : t("storage_hub.uncategorized", "Uncategorized")
                  if (!acc[catName]) acc[catName] = []
                  acc[catName].push(p)
                  return acc
                }, {})
                categories = Object.keys(groupedPayloads).sort()
                hasMultipleCategories = categories.length > 1
              } else {
                // sort flat list
                availablePayloads.sort((a, b) => {
                  if (sortMode === 'update_time') {
                    // Descending by last_update
                    const dateA = a.last_update || ''
                    const dateB = b.last_update || ''
                    return dateB.localeCompare(dateA)
                  } else if (sortMode === 'name') {
                    const nameA = a.name || a.filename || ''
                    const nameB = b.name || b.filename || ''
                    return nameA.localeCompare(nameB)
                  }
                  return 0
                })
              }

              return (
                <div key={src.id} className={cn(
                  multiSources ? "bg-ps-card border border-ps-border rounded-ps-3xl overflow-hidden hover:border-ps-blue/30 transition-colors" : "flex flex-col space-y-4"
                )}>
                  {/* Last Sync for single-source mode */}
                  {!multiSources && src.last_update > 0 && (
                    <p className="px-2 text-xs text-zinc-500 font-medium">
                      {t("storage_hub.last_sync", "Last Sync: {{date}}", { date: new Date(src.last_update * 1000).toLocaleString(i18n.resolvedLanguage) })}
                    </p>
                  )}

                  {/* Catalog header (only for multi-source) */}
                  {multiSources && (
                    <button
                      onClick={() => setExpandedSource(isExpanded ? null : src.id)}
                      className="w-full flex items-center justify-between p-4 md:p-5 hover:bg-white/5 transition-colors"
                    >
                    <div className="flex items-center space-x-4">
                      <div className="p-3 rounded-xl bg-gradient-to-br from-ps-blue/30 to-ps-blue/5 ring-1 ring-inset ring-ps-blue/20">
                        <Globe className="w-5 h-5 text-ps-blue" />
                      </div>
                      <div className="text-left">
                        <p className="font-bold text-white text-base">{src.name}</p>
                        {src.error && (
                          <p className="text-xs text-red-400 flex items-center space-x-1 mt-0.5">
                            <AlertTriangle className="w-3 h-3" />
                            <span>{t("storage_hub.fetch_failed", "Fetch failed")}</span>
                          </p>
                        )}
                        {src.last_update > 0 && (
                          <p className="text-xs text-zinc-500 mt-1">
                            {t("storage_hub.updated_at", "Updated {{date}}", { date: new Date(src.last_update * 1000).toLocaleString(i18n.resolvedLanguage) })}
                          </p>
                        )}
                      </div>
                    </div>
                    <div className="flex items-center space-x-3">
                      <span className="chip shrink-0">
                        {t("storage_hub.available_count", "{{count}} available", { count: availablePayloads.length })}
                      </span>
                      {enrichedSources.length > 1 && (
                        <ChevronDown className={cn("w-5 h-5 text-zinc-500 transition-transform", isExpanded && "rotate-180")} />
                      )}
                    </div>
                  </button>
                  )}

                  {/* Catalog payload list */}
                  {isExpanded && (
                    <div className={cn(
                      multiSources ? "border-t border-white/5 divide-y divide-white/5" : "grid grid-cols-1 gap-4"
                    )}>
                      {src.payloads.length === 0 ? (
                        <div className="py-12 flex flex-col items-center justify-center space-y-3 text-zinc-600">
                          <p className="text-sm font-medium">{t("storage_hub.source_empty", "Source is empty")}</p>
                        </div>
                      ) : availablePayloads.length === 0 ? (
                        <div className="py-12 flex flex-col items-center justify-center space-y-3 text-zinc-600">
                          <p className="text-sm font-medium">{t("storage_hub.all_installed", "All payloads installed")}</p>
                        </div>
                      ) : hasMultipleCategories ? (
                        categories.map(cat => (
                          <CategoryGroup
                            key={cat}
                            category={cat}
                            payloads={groupedPayloads[cat]}
                            multiSources={multiSources}
                            isPS5={isPS5}
                            onInstall={onInstall}
                            srcId={src.id}
                            srcUrl={src.url}
                            defaultExpanded={availablePayloads.length <= 10}
                          />
                        ))
                      ) : (
                        availablePayloads.map(p => (
                          <PayloadItem 
                            key={p.filename} 
                            p={p} 
                            multiSources={multiSources} 
                            isPS5={isPS5} 
                            onInstall={onInstall} 
                            srcId={src.id} 
                            srcUrl={src.url} 
                          />
                        ))
                      )}
                    </div>
                  )}
                </div>
              )
            })}
          </div>
        ) : (
          <div className="py-12 border-2 border-dashed border-white/10 rounded-xl flex flex-col items-center justify-center space-y-3">
            <CloudDownload className="w-10 h-10 text-white/10" />
            <p className="text-zinc-500 font-medium text-sm">{t("storage_hub.repo_empty", "Repository is empty")}</p>
          </div>
        )}
        </div>
      </section>

      {/* USB Storage Section */}
      {/* [CN-FORK] 风格统一：USB 板块同样 panel-card + 标准板块头，计数 badge 与另两板块同款式 */}
      <section id="usb-storage" className="panel-card">
        <header className="panel-head">
          <div className="flex items-center gap-3 min-w-0">
            <div className="p-2.5 rounded-xl bg-gradient-to-br from-ps-blue/30 to-ps-blue/5 ring-1 ring-inset ring-ps-blue/20 shrink-0">
              <HardDrive className="w-[18px] h-[18px] text-ps-blue" />
            </div>
            <h3 className="text-sm font-semibold text-white truncate">{t("storage_hub.usb_storage_title", "USB Storage")}</h3>
          </div>
          <span className="chip shrink-0">
            {t("storage_hub.files_count", "{{count}} File", { count: payloads.filter(p => p.includes('/mnt/usb')).length })}
          </span>
        </header>

        <div className={cn("p-4 grid gap-3", isPS5 ? "grid-cols-2" : "grid-cols-1 lg:grid-cols-2")}>
          {payloads.filter(p => p.includes('/mnt/usb')).length === 0 ? (
            <div className="col-span-full py-12 border-2 border-dashed border-white/10 rounded-xl flex flex-col items-center justify-center space-y-4">
              <div className="relative">
                <HardDrive className="w-10 h-10 text-white/10" />
                {!config.SCAN_USB_PAYLOADS && (
                  <div className="absolute -top-1 -right-1 bg-amber-500/20 rounded-full p-1 border border-amber-500/50">
                    <AlertTriangle className="w-4 h-4 text-amber-500" />
                  </div>
                )}
              </div>
              <div className="text-center space-y-2">
                <p className="text-zinc-500 font-medium text-sm">{t("storage_hub.no_usb_payloads", "No USB Payloads Found")}</p>
                {!config.SCAN_USB_PAYLOADS && (
                  <p className="text-xs text-zinc-600 max-w-xs mx-auto leading-relaxed">
                    {t("storage_hub.usb_scan_disabled_1", "Extended USB scanning is currently disabled.")} <br /> 
                    <span dangerouslySetInnerHTML={{ __html: t("storage_hub.usb_scan_disabled_2", "Enable <strong>\"Scan USB Payloads\"</strong> in settings to search for payloads on connected drives.") }} />
                  </p>
                )}
              </div>
            </div>
          ) : (
            payloads.filter(p => p.includes('/mnt/usb')).map((path) => {
              return (
                <div key={path} className="group card-enter flex flex-row items-center justify-between p-3.5 bg-white/[0.02] border border-white/10 rounded-xl hover:border-ps-blue/40 hover:bg-white/[0.04] hover:shadow-[0_0_20px_rgba(0,149,255,0.06)] transition-all gap-3">
                  <div className="flex items-center space-x-3 min-w-0">
                    <div className="p-2.5 bg-white/5 rounded-xl group-hover:bg-ps-blue/15 group-hover:shadow-[0_0_14px_rgba(0,149,255,0.15)] transition-all shrink-0">
                      <Usb className="w-4 h-4 text-zinc-400 group-hover:text-ps-blue transition-colors" />
                    </div>
                    <div className="space-y-0.5 min-w-0 flex-1">
                      <PayloadName path={path} className="text-sm text-white font-semibold" stacked hideIcon={true} />
                      <p className="text-[10px] text-zinc-600 font-mono truncate">{path}</p>
                    </div>
                  </div>
                  <div className="flex items-center shrink-0">
                    <button
                      onClick={() => onImportFromUsb(path)}
                      className="btn-ghost flex items-center gap-2 px-3.5 py-2 text-xs group/btn"
                    >
                      <Database className="w-4 h-4 text-ps-blue group-hover/btn:text-white transition-colors" />
                      <span>{t("storage_hub.move_internal_btn", "Move to Internal")}</span>
                    </button>
                  </div>
                </div>
              )
            })
          )}
        </div>
      </section>

      {/* Footer Info for PS5 */}
      {isPS5 && (
        <div className={cn(
          /* [CN-FORK] QR 页脚：白底二维码加柔光环，标题主色竖条 */
          "panel-card p-6 flex items-center gap-6 bg-black/40",
          isPS5 ? "flex-row" : "flex-col md:flex-row"
        )}>
          <div className="flex flex-col items-center space-y-3 shrink-0">
            <div className="bg-white p-3 rounded-2xl shadow-[0_0_28px_rgba(0,149,255,0.18)] ring-1 ring-white/40">
              <QRCodeSVG value={`http://${ip}:8911`} size={isPS5 ? 140 : 120} level="M" />
            </div>
            <code className="chip chip-muted font-mono">{ip}:8911</code>
          </div>
          <div className="flex-1 space-y-2 text-center md:text-left">
            <h4 className="text-sm font-semibold text-white flex items-center justify-center md:justify-start gap-3">
              <div className="w-1 h-4 bg-ps-blue rounded-full" />
              <span>{t("storage_hub.remote_management_title", "Remote Management")}</span>
            </h4>
            <p className="text-sm text-zinc-400 leading-relaxed font-medium max-w-3xl">
              {t("storage_hub.remote_management_desc", "Access this dashboard from your computer or phone to manage and upload payloads directly from your local network.")}
            </p>
          </div>
        </div>
      )}
    </div>
  )
}

export default StorageHub
