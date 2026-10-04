import React, { useState, useEffect, useMemo } from 'react'
import { CloudDownload, CloudOff, Upload, Package, Database, RefreshCw, Trash2, Loader2, AlertTriangle, HardDrive, Usb, ChevronDown, Globe, Search, Play } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { QRCodeSVG } from 'qrcode.react'
import { cn, isPS5, isIOS, parsePayloadName } from '../../utils/helpers'
import PayloadName from '../ui/PayloadName'
// [CN-FORK] v0.5.2.94 骨架归一：页头/板块/空态/加载态改用共用部件，不再在本页手写
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'

const PayloadItem = ({ p, multiSources, isPS5, onInstall, srcId, srcUrl }) => {
  const { t } = useTranslation()
  return (
  <div
    className={cn(
      /* [CN-FORK] 风格统一：列表行紧凑内边距，与全局 text-sm 尺度对齐 */
      "flex flex-col md:flex-row justify-between p-4 transition-all",
      multiSources
        ? "hover:bg-white/[0.03]"
        : "bg-white/[0.02] border border-white/10 rounded-xl hover:border-ps-blue/40",
      isPS5 ? "flex-row items-center" : "items-start md:items-center"
    )}
  >
    <div className="space-y-1 min-w-0">
      <PayloadName path={p.filename} version={p.version} className="text-white font-semibold" stacked lastUpdate={p.last_update} />
      {p.description && (
        <p className="text-xs md:text-sm text-zinc-400 font-medium leading-relaxed">{p.description}</p>
      )}
    </div>
    <button
      onClick={() => onInstall(p, srcId === 'legacy-repo' ? null : srcId, srcUrl)}
      /* [CN-FORK] v0.5.2.92 安装/更新按钮收进 .btn 体系：命中高度由 --ctl-h 决定
       *（PC 44px、电视 68px），不再靠 px/py 撑出 32px 的小条；间距改用 .btn 的 * + * 外边距
       *（老内核不认 flex gap，原来的 gap-2 在 PS5 上本来就没生效）。 */
      className={cn(
        "btn",
        isPS5 ? "px-8 ml-6" : "w-full md:w-auto mt-3 md:mt-0 md:ml-6",
        p.isUpdate ? "bg-emerald-600 hover:bg-emerald-500 text-white" : "btn-primary"
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
        aria-expanded={expanded}
        className="w-full flex items-center justify-between px-4 min-h-[var(--ctl-h)] hover:bg-white/5 transition-colors"
      >
        {/* [CN-FORK] 分类组头改用主题变量色：原 text-white/90 在 light-theme 重映射层无规则，浅色下白字不可读 */}
        <span className="font-semibold text-ps-text text-sm">{category} <span className="text-zinc-500">({payloads.length})</span></span>
        <ChevronDown className={cn("w-5 h-5 text-zinc-500 transition-transform", expanded && "rotate-180")} />
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

const StorageHub = ({ payloads, payloadMeta, loadPayload, onInstall, onDelete, onUpload, onImportFromUsb, config, ip, scrollTarget, onClearScrollTarget }) => {
  const { t, i18n } = useTranslation()
  const multiSources = config?.MULTI_SOURCES_ENABLED !== false
  /* [CN-FORK] v0.5.2.67 默认云端仓库总开关：本版默认关（后端缺键时按关处理）；
   * 关掉后预置的 repo.630810.xyz 靠多来源模式仍会列出，所以不再受单来源限制 */
  const defaultRepoOn = config?.DEFAULT_REPO_ENABLED === true
  const cloudDisabled = !multiSources && !defaultRepoOn

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
    /* [CN-FORK] v0.5.1.65 默认云端仓库已关闭：不发请求，后端也不会给东西 */
    if (cloudDisabled) {
      setRepoData(null)
      setLoading(false)
      setError(false)
      return
    }
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

  useEffect(() => {
    /* [CN-FORK] v0.5.1.65 开关变化时重跑：关掉时 fetchRemote 自己会清空目录并直接返回（不发请求），
     * 重新打开则自动拉一次 */
    fetchRemote()
  }, [cloudDisabled])

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
      {/* [CN-FORK] v0.5.2.94 页头改用共用的 <PageHead>：粘顶（实色 rail 底 + 发丝线），
          列表再长也能看见「这一页是什么、主操作在哪」；上传仍是本页唯一实心主按钮 */}
      <PageHead
        title={<>{t("storage_hub.title_1", "Payload")} <span className="text-ps-blue">{t("storage_hub.title_2", "Management")}</span></>}
        actions={!isPS5 && (
          <>
            {/* [CN-FORK] 上传按钮前显示服务器 IP:端口 */}
            <span className="text-xs font-mono text-zinc-500 tracking-tight">{ip}:8911</span>
            <label className="btn btn-primary">
              <Upload className="w-4 h-4" />
              <span>{t("storage_hub.upload_btn", "Upload ELF Payload")}</span>
              <input type="file" className="hidden" onChange={onUpload} accept={isIOS ? undefined : ".elf,.bin"} />
            </label>
          </>
        )}
      />

      {/* Installed Payloads Section */}
      <Section
        icon={Database}
        title={t("storage_hub.installed_title", "Installed Payloads")}
        count={t("storage_hub.files_count", "{{count}} File", { count: internalPayloads.length })}
        bodyClass={cn("grid gap-3", isPS5 ? "grid-cols-2" : "grid-cols-1 sm:grid-cols-2 lg:grid-cols-3")}
      >
          {internalPayloads.length === 0 ? (
            <EmptyState
              className="col-span-full"
              icon={Package}
              title={t("storage_hub.library_empty", "Library Empty")}
            />
          ) : (
            internalPayloads.map((path) => {
              const fileName = path.split('/').pop()
              const sourceBadge = getSourceBadge(fileName)
              // Find update in all sources (multi or legacy)
              const allRemote = enrichedSources.flatMap(s => s.payloads)
              const remoteMatch = allRemote.find(rp => rp.filename === fileName || rp.installedFilename === fileName)
              const remoteVersion = remoteMatch?.version || (remoteMatch?.filename ? parsePayloadName(remoteMatch.filename).version : null)
              return (
                <div
                  key={path}
                  onClick={() => loadPayload?.(path)}
                  /* [CN-FORK] v0.5.2.92 整张卡就是「启动这个 payload」的唯一点击/聚焦单元：
                   * 补 role+tabIndex 才能进遥控器导航的候选集（spatialNav 把 OK 键转成一次 click），
                   * min-height 由 .row-link 的 --row-h 决定，电视端一行 88px 才点得着。
                   * 原 gap-2 在 PS5 老内核上不生效，卡片内间距改由下面的 mt 承担。 */
                  role="button"
                  tabIndex={0}
                  aria-label={t("storage_hub.launch", "Launch")}
                  className="row-link group flex flex-col justify-center p-3 bg-white/[0.02] border border-white/10 rounded-xl hover:border-ps-blue/40 transition-colors relative overflow-hidden cursor-pointer"
                >
                  <div className="flex flex-row items-center justify-between w-full">
                    <div className="flex items-center space-x-3 min-w-0 flex-1">
                      <div className="p-2 rounded-lg shrink-0">
                        <Package className="w-4 h-4 text-ps-blue" />
                      </div>
                      <div className="min-w-0 flex-1 space-y-0.5">
                        <PayloadName path={fileName} className="text-white font-semibold" stacked />
                        {sourceBadge && (
                          <div className="flex items-center text-zinc-500 text-[11px] select-none font-medium">
                            <Globe className="w-3.5 h-3.5 mr-1" />
                            <span>{sourceBadge}</span>
                          </div>
                        )}
                      </div>
                    </div>
                    <div className="flex items-center shrink-0 ml-3">
                      {/* [CN-FORK] v0.5.2.92 图标钮统一走 .icon-btn：命中区与 .btn 同档（PC 44 / 电视 68），
                          图标本身仍是小尺寸；title 在触屏与遥控器上都看不见，因此补 aria-label。 */}
                      <button
                        onClick={(e) => { e.stopPropagation(); loadPayload?.(path) }}
                        className="icon-btn"
                        title={t("storage_hub.launch", "Launch")}
                        aria-label={t("storage_hub.launch", "Launch")}
                      >
                        <Play className="w-4 h-4" />
                      </button>
                      <button
                        onClick={(e) => { e.stopPropagation(); onDelete(fileName) }}
                        className="icon-btn hover:bg-red-500/10 hover:text-red-500"
                        title={t("storage_hub.remove_payload", "Remove Payload")}
                        aria-label={t("storage_hub.remove_payload", "Remove Payload")}
                      >
                        <Trash2 className="w-4 h-4" />
                      </button>
                    </div>
                  </div>
                  {remoteMatch?.isUpdate && (
                    <button
                      onClick={(e) => { e.stopPropagation(); onInstall(remoteMatch, remoteMatch.source_id, legacyRepoUrl) }}
                      className="btn mt-1 w-full bg-emerald-600 hover:bg-emerald-500 text-white text-xs"
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
      </Section>

      {/* Cloud Repository Section */}
      <Section
        id="cloud-repository"
        icon={CloudDownload}
        title={t("storage_hub.cloud_repo_title", "Cloud Repository")}
        bodyClass="space-y-4"
        actions={!cloudDisabled && (
          <button onClick={() => fetchRemote(true)} className="icon-btn" title={t("storage_hub.retry_btn", "Retry Connection")} aria-label={t("storage_hub.retry_btn", "Retry Connection")}>
            <RefreshCw className={cn(loading && "animate-spin")} />
          </button>
        )}
      >
        {!cloudDisabled && (
        <div className="flex flex-col md:flex-row">
          {/* [CN-FORK] v0.5.2.92 搜索条改用 .field：命中高度与输入框同档，浅色/深色都由 inset 档驱动 */}
          <div className="field flex-1 flex items-center focus-within:border-ps-blue/50 transition-colors">
            <Search className="w-4 h-4 text-zinc-500 mr-3 shrink-0" />
            <input
              type="text"
              placeholder={t("storage_hub.search_placeholder", "Search payloads by name or description...")}
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              className="bg-transparent border-none outline-none text-white w-full text-sm font-medium placeholder:text-zinc-600"
            />
          </div>
          {/* [CN-FORK] v0.5.2.92 排序控件改版：此前是「隐形原生 select 盖在假控件上」（opacity-0），
              鼠标点得中，遥控器和触屏都点不准，而且看不见当前项。改成三枚芯片按钮，
              选中项用契约里唯一的实心选中档（.btn-primary），间隙用 space-x（margin 实现，老内核可用；
              flex gap 在 PS5 上根本不生效）。 */}
          <div className="flex items-center flex-none space-x-2 mt-3 md:mt-0 md:ml-3">
            <span className="text-zinc-500 text-xs font-medium whitespace-nowrap">
              {t("storage_hub.sort_by", "Sort by:")}
            </span>
            {[
              ['category', t("storage_hub.sort_category", "Category")],
              ['update_time', t("storage_hub.sort_time", "Update Time")],
              ['name', t("storage_hub.sort_name", "Name")],
            ].map(([mode, label]) => (
              <button
                key={mode}
                onClick={() => handleSortChange(mode)}
                aria-pressed={sortMode === mode}
                className={cn("btn !px-3 text-xs", sortMode === mode ? "btn-primary" : "btn-ghost")}
              >
                {label}
              </button>
            ))}
          </div>
        </div>
        )}

        {cloudDisabled ? (
          /* [CN-FORK] v0.5.1.65 默认云端仓库关闭态：明确告知是用户自己关的，不是网络问题 */
          <EmptyState
            dashed={false}
            className="border border-white/10 rounded-xl bg-white/[0.02]"
            icon={CloudOff}
            title={t("storage_hub.repo_disabled", "Cloud Repository Disabled")}
            desc={t("storage_hub.repo_disabled_desc", "The default cloud repository is turned off in Settings, so no online payload catalog is fetched. Turn it back on to browse and install online payloads.")}
          />
        ) : loading && !repoData ? (
          <LoadingState label={t("storage_hub.syncing", "Syncing with Repository...")} />
        ) : error ? (
          <EmptyState
            dashed={false}
            className="border border-red-500/20 rounded-xl bg-red-950/5"
            icon={AlertTriangle}
            iconClassName="text-red-500 opacity-60"
            title={t("storage_hub.repo_unavailable", "Repository Unavailable")}
            desc={t("storage_hub.repo_unavailable_desc", "Check your internet connection and try again.")}
            action={
              <button onClick={() => fetchRemote(true)} className="btn btn-outline text-xs">{t("storage_hub.retry_btn", "Retry Connection")}</button>
            }
          />
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
                  /* [CN-FORK] v0.5.2.91 多来源列表容器原用 bg-ps-card（card 档实色），是 .90 板块并档后漏掉的一块，改走同一条 inset 通道 */
                  multiSources ? "bg-white/[0.03] border border-ps-border rounded-ps-3xl overflow-hidden" : "flex flex-col space-y-4"
                )}>
                  {/* Last Sync for single-source mode */}
                  {!multiSources && src.last_update > 0 && (
                    <p className="px-2 text-xs uppercase tracking-widest text-zinc-500">
                      {t("storage_hub.last_sync", "Last Sync: {{date}}", { date: new Date(src.last_update * 1000).toLocaleString(i18n.resolvedLanguage) })}
                    </p>
                  )}

                  {/* Catalog header (only for multi-source) */}
                  {multiSources && (
                    <button
                      onClick={() => setExpandedSource(isExpanded ? null : src.id)}
                      aria-expanded={isExpanded}
                      className="w-full flex items-center justify-between p-4 md:p-5 min-h-[var(--ctl-h)] hover:bg-white/5 transition-colors"
                    >
                    <div className="flex items-center space-x-4">
                      <div className="p-2.5 rounded-xl">
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
                          <p className="text-xs text-zinc-500 uppercase tracking-widest mt-1">
                            {t("storage_hub.updated_at", "Updated {{date}}", { date: new Date(src.last_update * 1000).toLocaleString(i18n.resolvedLanguage) })}
                          </p>
                        )}
                      </div>
                    </div>
                    <div className="flex items-center space-x-3">
                      <span className="px-3 py-1 rounded-full bg-white/5 text-zinc-500 text-xs font-bold">
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
                        <EmptyState dashed={false} title={t("storage_hub.source_empty", "Source is empty")} />
                      ) : availablePayloads.length === 0 ? (
                        <EmptyState dashed={false} title={t("storage_hub.all_installed", "All payloads installed")} />
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
          <EmptyState icon={CloudDownload} title={t("storage_hub.repo_empty", "Repository is empty")} />
        )}
      </Section>

      {/* USB Storage Section */}
      <Section
        id="usb-storage"
        icon={HardDrive}
        title={t("storage_hub.usb_storage_title", "USB Storage")}
        count={t("storage_hub.files_count", "{{count}} File", { count: payloads.filter(p => p.includes('/mnt/usb')).length })}
        bodyClass={cn("grid gap-3", isPS5 ? "grid-cols-2" : "grid-cols-1 lg:grid-cols-2")}
      >
          {payloads.filter(p => p.includes('/mnt/usb')).length === 0 ? (
            <EmptyState
              className="col-span-full"
              icon={HardDrive}
              title={t("storage_hub.no_usb_payloads", "No USB Payloads Found")}
              desc={!config.SCAN_USB_PAYLOADS && (
                <>
                  {t("storage_hub.usb_scan_disabled_1", "Extended USB scanning is currently disabled.")}{' '}
                  <span dangerouslySetInnerHTML={{ __html: t("storage_hub.usb_scan_disabled_2", "Enable <strong>\"Scan USB Payloads\"</strong> in settings to search for payloads on connected drives.") }} />
                </>
              )}
            />
          ) : (
            payloads.filter(p => p.includes('/mnt/usb')).map((path) => {
              return (
                <div key={path} className="group flex flex-row items-center justify-between p-3 bg-white/[0.02] border border-white/10 rounded-xl hover:border-ps-blue/40 transition-colors">
                  <div className="flex items-center space-x-3 min-w-0">
                    <div className="p-2 rounded-lg shrink-0">
                      <Usb className="w-4 h-4 text-ps-blue" />
                    </div>
                    <div className="space-y-0.5 min-w-0 flex-1">
                      <PayloadName path={path} className="text-white font-semibold" stacked hideIcon={true} />
                      <p className="text-[10px] text-zinc-600 font-mono truncate">{path}</p>
                    </div>
                  </div>
                  <div className="flex items-center shrink-0 ml-3">
                    <button
                      onClick={() => onImportFromUsb(path)}
                      className="btn btn-outline group/btn"
                    >
                      <Database className="w-4 h-4 text-ps-blue group-hover/btn:text-current transition-colors" />
                      <span>{t("storage_hub.move_internal_btn", "Move to Internal")}</span>
                    </button>
                  </div>
                </div>
              )
            })
          )}
      </Section>

      {/* Footer Info for PS5 */}
      {isPS5 && (
        <div className={cn(
          /* [CN-FORK] 风格统一：QR 页脚改用 panel-card 体系，去掉 italic 装饰，与其他板块呼应 */
          "panel-card p-5 flex items-center bg-black/40",
          isPS5 ? "flex-row" : "flex-col md:flex-row"
        )}>
          <div className="flex flex-col items-center space-y-3 shrink-0">
            <div className="bg-white p-3 rounded-xl">
              <QRCodeSVG value={`http://${ip}:8911`} size={isPS5 ? 140 : 120} level="M" />
            </div>
            {/* [CN-FORK] IP 地址改用主题变量色 text-ps-text：原先写死 text-white 靠 !important 重映射，PS5 老内核上失效导致浅色主题白字不可读 */}
            <code className="text-ps-text font-mono text-sm font-semibold opacity-90">{ip}:8911</code>
          </div>
          <div className="flex-1 space-y-2 text-center md:text-left mt-6 md:mt-0 md:ml-6">
            <h4 className="text-sm font-semibold text-white flex items-center justify-center md:justify-start">
              <div className="w-1 h-4 bg-ps-blue rounded-full mr-3" />
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
