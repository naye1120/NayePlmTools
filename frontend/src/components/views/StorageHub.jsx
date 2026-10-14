import React, { useState, useEffect, useMemo, useLayoutEffect, useRef, useCallback } from 'react'
import { CloudDownload, CloudOff, Upload, Package, Database, RefreshCw, Trash2, Loader2, AlertTriangle, HardDrive, Usb, ChevronDown, Globe, Search, Play } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { QRCodeSVG } from 'qrcode.react'
import { cn, isPS5, isIOS, getBaseName, parsePayloadName } from '../../utils/helpers'
import { getPayloadStatus, resolveSourceDisplayName, isRepositoryPayloadVisible } from '../../utils/payloadStatus'
import { slug } from '../../utils/versions'
import { formatSyncTime } from '../../utils/syncTime'
import PayloadName from '../ui/PayloadName'
import Modal from '../ui/Modal'
import { useRepository } from '../../contexts/RepositoryContext'
import PayloadCard from '../ui/PayloadCard'
import PayloadDetailPanel from '../ui/PayloadDetailPanel'
// [CN-FORK] v0.6.0 合入：本页页头/板块/空态/载入态/互斥档全部走共用部件，不再各页手写
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'
import Segmented from '../ui/Segmented' /* [CN-FORK] v0.5.2.97 互斥档位统一 */

const StorageHub = ({
  items = [],
  self = null,
  payloads = [],
  /* [CN-FORK] /list_payloads 的 meta 表（filename -> {source_name, install_source_detail}）
   * 与「启动这个 payload」的入口：管理 Payload 一页统管就靠这两个 prop。 */
  payloadMeta,
  loadPayload,
  onInstall,
  onDelete,
  onUpload,
  onImportFromUsb,
  config,
  ip,
  scrollTarget,
  onClearScrollTarget,
  selectedPayload: selectedPayloadProp,
  onSelectPayload,
  onClosePayload,
  conflictPrompt: externalConflictPrompt,
  setConflictPrompt: externalSetConflictPrompt,
  externalLinkUrl,
  setExternalLinkUrl,
  installingPayloadId = null,
}) => {
  const { t, i18n } = useTranslation()
  /* [CN-FORK] 多来源缺省开（上游是 === true，缺省关）：本 fork 的口径是
   * 「装上就能同时看到官方源和用户自己加的源」，只有用户显式关掉才收成单来源。 */
  const multiSources = config?.MULTI_SOURCES_ENABLED !== false
  /* [CN-FORK] v0.5.2.67 默认云端仓库总开关：本版默认关（后端缺键时按关处理）；
   * 关掉后预置的 repo.630810.xyz 靠多来源模式仍会列出，所以不再受单来源限制 */
  const defaultRepoOn = config?.DEFAULT_REPO_ENABLED === true
  const cloudDisabled = !multiSources && !defaultRepoOn

  const { sources, repoData, loading, error, fetchRemote } = useRepository()

  /* [CN-FORK] 已安装的 Payload：payloads 是路径数组，USB 的那批归下面的板块 */
  const internalPayloads = payloads.filter(p => !p.includes('/mnt/usb'))

  /* [CN-FORK] v0.5.1.65 默认云端仓库已关闭：这一版不发请求、也不列目录。
   * v0.6.0 把仓库数据搬进 RepositoryContext 后，「mount 即拉取」的行为挪到了 provider
   * （它拿不到 config，关不掉），这里至少保证 UI 上一行都不渲染，并明确告知是用户自己关的。
   * fork 默认 multiSources 开，所以实际上是「官方源照常列 + 用户自选关」的口径。 */
  const stalenessCheckedRef = useRef(false)
  useEffect(() => {
    if (cloudDisabled || !repoData || stalenessCheckedRef.current) return
    stalenessCheckedRef.current = true
    /* [CN-FORK] 保留 v0.5.2 的「陈旧目录自愈」：单来源模式下后端给的是缓存副本，
     * 超过 24h 强刷一次（fetchRemote(true) -> /repository_refresh）。
     * 多来源模式下 repoData 没有顶层 last_update，此处天然空转。 */
    const last = Number(repoData.last_update) || 0
    if (last > 0 && Math.floor(Date.now() / 1000) - last > 24 * 60 * 60) {
      fetchRemote(true)
    }
  }, [repoData, cloudDisabled, fetchRemote])

  /* [CN-FORK] Source badge helper: look up source name from metadata */
  const getSourceBadge = (fileName) => {
    if (!multiSources || !payloadMeta) return null
    const meta = payloadMeta[fileName]
    return meta?.source_name || meta?.install_source_detail || null
  }

  const [expandedSource, setExpandedSource] = useState(null) // id of expanded catalog
  const [search, setSearch] = useState('')
  const [sortMode, setSortMode] = useState(() => {
    return localStorage.getItem('repoSortMode') || 'name'
  })

  const [localConflictPrompt, setLocalConflictPrompt] = useState(null)
  const conflictPrompt = externalConflictPrompt !== undefined ? externalConflictPrompt : localConflictPrompt
  const setConflictPrompt = externalSetConflictPrompt || setLocalConflictPrompt

  const [localSelectedPayload, setLocalSelectedPayload] = useState(null)
  const selectedPayload = selectedPayloadProp !== undefined ? selectedPayloadProp : localSelectedPayload
  const setSelectedPayload = onSelectPayload || setLocalSelectedPayload
  const [savedScroll, setSavedScroll] = useState(0)

  const handlePayloadSelect = (payloadData) => {
    const mainEl = document.querySelector('main')
    if (mainEl) setSavedScroll(mainEl.scrollTop)
    if (onSelectPayload) {
      onSelectPayload(payloadData)
    } else {
      setSelectedPayload(payloadData)
    }
  }

  const handleCloseDetail = useCallback(() => {
    if (onClosePayload) {
      onClosePayload()
    } else {
      setSelectedPayload(null)
    }
  }, [onClosePayload, setSelectedPayload])

  const handleSortChange = (mode) => {
    setSortMode(mode)
    localStorage.setItem('repoSortMode', mode)
  }

  useEffect(() => {
    if (scrollTarget) {
      const timer = setTimeout(() => {
        const element = document.getElementById(scrollTarget)
        if (element) element.scrollIntoView({ behavior: 'smooth' })
        if (onClearScrollTarget) onClearScrollTarget()
      }, 300)
      return () => clearTimeout(timer)
    }
  }, [scrollTarget, onClearScrollTarget])

  /* ---- Source-grouped data with statusInfo ---- */
  const enrichedSources = useMemo(() => {
    return (sources || []).map((src) => {
      const rawPayloads = src.payloads || []
      const filteredPayloads = rawPayloads.filter((p) => isRepositoryPayloadVisible(p, src.url))
      const enrichedPayloads = filteredPayloads.map((p) => {
        const statusInfo = getPayloadStatus({
          payload: p,
          srcUrl: src.url,
          items,
          self,
          sources,
          multiSources,
        })
        return {
          ...p,
          statusInfo,
          isInstalled: statusInfo.isInstalled,
          isUpdate: statusInfo.isUpdate,
        }
      })

      return {
        ...src,
        payloads: enrichedPayloads,
      }
    })
  }, [sources, items, multiSources, self])

  // eslint-disable-next-line react-hooks/preserve-manual-memoization
  const resolvedPayloadData = useMemo(() => {
    if (!selectedPayload) return null

    const targetPayload =
      selectedPayload.payload ||
      (typeof selectedPayload === 'object' && (selectedPayload.name || selectedPayload.id || selectedPayload.filename)
        ? selectedPayload
        : null)

    const targetIdentifier =
      selectedPayload.payload?.id ||
      selectedPayload.id ||
      selectedPayload.payload?.filename ||
      selectedPayload.filename ||
      (typeof selectedPayload === 'string' ? selectedPayload : null)

    const targetSrcId = selectedPayload.srcId || null
    const targetSrcUrl = selectedPayload.srcUrl || null

    // Check candidate sources from enrichedSources first so that fresh status is always picked up
    const candidateSources = targetSrcId
      ? [
          enrichedSources.find((s) => s.id === targetSrcId),
          ...enrichedSources.filter((s) => s.id !== targetSrcId),
        ].filter(Boolean)
      : (targetSrcUrl
          ? [
              enrichedSources.find((s) => s.url === targetSrcUrl),
              ...enrichedSources.filter((s) => s.url !== targetSrcUrl),
            ].filter(Boolean)
          : enrichedSources)

    for (const src of candidateSources) {
      const match = src.payloads?.find(
        (candidate) =>
          (targetIdentifier && candidate.id === targetIdentifier) ||
          (targetIdentifier && candidate.filename === targetIdentifier) ||
          (targetIdentifier && candidate.filename?.toLowerCase() === targetIdentifier.toLowerCase()) ||
          (targetPayload && (candidate.id === targetPayload.id || candidate.filename === targetPayload.filename))
      )
      if (match) {
        return {
          payload: match,
          srcId: src.id,
          srcUrl: src.url,
          statusInfo: match.statusInfo,
          isInstalled: match.isInstalled,
          isUpdate: match.isUpdate,
        }
      }
    }

    if (targetPayload && (targetPayload.name || targetPayload.id || targetPayload.filename)) {
      const srcUrl =
        targetSrcUrl ||
        (targetSrcId ? enrichedSources.find((s) => s.id === targetSrcId)?.url : enrichedSources[0]?.url) ||
        ''
      const statusInfo = getPayloadStatus({
        payload: targetPayload,
        srcUrl,
        items,
        self,
        sources,
        multiSources,
      })
      return {
        ...selectedPayload,
        payload: targetPayload,
        srcId: targetSrcId || enrichedSources[0]?.id || '',
        srcUrl,
        statusInfo,
        isInstalled: statusInfo.isInstalled,
        isUpdate: statusInfo.isUpdate,
      }
    }

    return null
  }, [selectedPayload, enrichedSources, items, sources, multiSources, self])

  const currentDetailId =
    resolvedPayloadData?.payload?.id ||
    (resolvedPayloadData?.payload
      ? slug(resolvedPayloadData.payload.name || resolvedPayloadData.payload.filename)
      : null)
  const isDetailInstalling = Boolean(installingPayloadId && currentDetailId === installingPayloadId)

  useLayoutEffect(() => {
    const mainEl = document.querySelector('main')
    if (!mainEl) return

    if (resolvedPayloadData) {
      mainEl.scrollTop = 0
    } else {
      mainEl.scrollTop = savedScroll
    }
  }, [resolvedPayloadData, savedScroll])

  useEffect(() => {
    if (selectedPayload && !resolvedPayloadData && !loading && repoData) {
      handleCloseDetail()
    }
  }, [selectedPayload, resolvedPayloadData, loading, repoData, handleCloseDetail])

  const handleInstallClick = (payload, srcId, srcUrl, statusInfo) => {
    // If there is an install conflict across repositories
    if (statusInfo?.hasConflict && statusInfo?.conflictSlot) {
      const srcObj = enrichedSources.find((s) => s.id === srcId || s.url === srcUrl)
      setConflictPrompt({
        payload,
        srcId,
        srcUrl,
        conflictSlot: statusInfo.conflictSlot,
        thisSourceName: srcObj?.name || 'this repository',
      })
      return
    }

    onInstall(payload, srcId, srcUrl)
  }

  if (selectedPayload && !resolvedPayloadData && (loading || !repoData)) {
    return (
      <div className="py-24 flex flex-col items-center justify-center space-y-4">
        <Loader2 className="w-10 h-10 text-ps-blue animate-spin" />
        <p className="text-zinc-400 font-medium">{t("storage_hub.loading", "Loading repository...")}</p>
      </div>
    )
  }

  if (resolvedPayloadData) {
    return (
      <div className="w-full min-w-0 max-w-full">
        <PayloadDetailPanel
          payload={resolvedPayloadData.payload}
          srcId={resolvedPayloadData.srcId}
          srcUrl={resolvedPayloadData.srcUrl}
          statusInfo={resolvedPayloadData.statusInfo}
          isInstalled={resolvedPayloadData.isInstalled}
          isUpdate={resolvedPayloadData.isUpdate}
          isInstalling={isDetailInstalling}
          onDelete={onDelete}
          externalLinkUrl={externalLinkUrl}
          setExternalLinkUrl={setExternalLinkUrl}
          onClose={handleCloseDetail}
          onInstall={(p, sid, surl, sInfo) => {
            handleInstallClick(p, sid, surl, sInfo || resolvedPayloadData.statusInfo)
          }}
        />

        {/* Conflict Modal */}
        <Modal
          show={Boolean(conflictPrompt)}
          title={t("storage_hub.conflict_title", "Payload Conflict")}
          onClose={() => setConflictPrompt(null)}
          footer={
            <div className="flex flex-col sm:flex-row w-full gap-3">
              <button
                type="button"
                onClick={() => setConflictPrompt(null)}
                className="flex-1 py-3 px-4 rounded-xl bg-white/5 hover:bg-white/10 text-white font-bold uppercase text-sm transition-all"
              >
                {t("common.cancel", "Cancel")}
              </button>
              <button
                type="button"
                onClick={() => {
                  const cp = conflictPrompt
                  setConflictPrompt(null)
                  onInstall(cp.payload, cp.srcId, cp.srcUrl, {
                    mode: 'replace',
                    replaceSlot: cp.conflictSlot.slot,
                  })
                }}
                className="flex-1 py-3 px-4 rounded-xl bg-amber-600 hover:bg-amber-500 text-white font-black uppercase text-sm transition-all shadow-lg"
              >
                {t("storage_hub.conflict_replace", "Replace")}
              </button>
              <button
                type="button"
                onClick={() => {
                  const cp = conflictPrompt
                  setConflictPrompt(null)
                  onInstall(cp.payload, cp.srcId, cp.srcUrl, {
                    mode: 'keep_both',
                  })
                }}
                className="flex-1 py-3 px-4 rounded-xl bg-ps-blue hover:bg-ps-blue/80 text-white font-black uppercase text-sm transition-all shadow-lg"
              >
                {t("storage_hub.conflict_keep_both", "Keep both")}
              </button>
            </div>
          }
        >
          <p>
            {conflictPrompt &&
              t(
                "storage_hub.conflict_message",
                "\"{{name}}\" is already installed from {{otherSource}} ({{version}}). Install from {{thisSource}} too?",
                {
                  name: conflictPrompt.payload.name || conflictPrompt.payload.id,
                  otherSource: resolveSourceDisplayName(conflictPrompt.conflictSlot, sources),
                  version: conflictPrompt.conflictSlot.version || "",
                  thisSource: conflictPrompt.thisSourceName,
                }
              )}
          </p>
        </Modal>
      </div>
    )
  }
  return (
    <div className="space-y-6">
      {/* [CN-FORK] v0.6.0 合入：仓库安装撞名时的冲突框（Replace / Keep both）。
          fork 原有的上传撞名流程（App.jsx 的 uploadConflictModal）不动，这里管的是
          「同一个 payload 已从别的来源装过」这一路。 */}
      {/* Conflict Modal */}
      <Modal
        show={Boolean(conflictPrompt)}
        title={t("storage_hub.conflict_title", "Payload Conflict")}
        onClose={() => setConflictPrompt(null)}
        footer={
          <div className="flex flex-col sm:flex-row w-full gap-3">
            <button
              type="button"
              onClick={() => setConflictPrompt(null)}
              className="flex-1 py-3 px-4 rounded-xl bg-white/5 hover:bg-white/10 text-white font-bold uppercase text-sm transition-all"
            >
              {t("common.cancel", "Cancel")}
            </button>
            <button
              type="button"
              onClick={() => {
                const cp = conflictPrompt
                setConflictPrompt(null)
                onInstall(cp.payload, cp.srcId, cp.srcUrl, {
                  mode: 'replace',
                  replaceSlot: cp.conflictSlot.slot,
                })
              }}
              className="flex-1 py-3 px-4 rounded-xl bg-amber-600 hover:bg-amber-500 text-white font-black uppercase text-sm transition-all shadow-lg"
            >
              {t("storage_hub.conflict_replace", "Replace")}
            </button>
            <button
              type="button"
              onClick={() => {
                const cp = conflictPrompt
                setConflictPrompt(null)
                onInstall(cp.payload, cp.srcId, cp.srcUrl, {
                  mode: 'keep_both',
                })
              }}
              className="flex-1 py-3 px-4 rounded-xl bg-ps-blue hover:bg-ps-blue/80 text-white font-black uppercase text-sm transition-all shadow-lg"
            >
              {t("storage_hub.conflict_keep_both", "Keep both")}
            </button>
          </div>
        }
      >
        <p>
          {conflictPrompt &&
            t(
              "storage_hub.conflict_message",
              "\"{{name}}\" is already installed from {{otherSource}} ({{version}}). Install from {{thisSource}} too?",
              {
                name: conflictPrompt.payload.name || conflictPrompt.payload.id,
                otherSource: resolveSourceDisplayName(conflictPrompt.conflictSlot, sources),
                version: conflictPrompt.conflictSlot.version || "",
                thisSource: conflictPrompt.thisSourceName,
              }
            )}
        </p>
      </Modal>


      <PageHead
        title={<>{t("storage_hub.title_1", "Payload")} <span className="text-ps-blue">{t("storage_hub.title_2", "Management")}</span></>}
        actions={!isPS5 && (
          /* [CN-FORK] v0.5.2.164 用户口径：「上传按钮放在 ip 和端口上边；ip 和端口显示
             再突出一些（不限于颜色，样式/背景都行）；他们两个的右边放二维码，用来手机扫码访问」。
             于是这一块从「一行两个东西」变成：左列竖排（上传按钮在上、IP:端口在下），右列二维码。
             二维码的内容就是**本页地址**（`http://<ip>:8911`），扫开即本面板 ——
             与页脚那个「远程管理」二维码同一份数据源，只是这里给的是就近入口。 */
          <div className="flex items-center gap-3">
            <div className="flex flex-col items-stretch gap-2">
              <label className="btn btn-primary justify-center">
                <Upload className="w-4 h-4" />
                <span>{t("storage_hub.upload_btn", "Upload ELF Payload")}</span>
                <input type="file" className="hidden" onChange={onUpload} accept={isIOS ? undefined : ".elf,.bin"} />
              </label>
              {/* IP:端口 做成一块「芯片」：底色 + 描边 + 等宽大字 —— 原来那行 12px 灰字
                  在页头里基本看不见，而它正是用户要手输进手机地址栏的那串。 */}
              <div
                className="flex items-center justify-center gap-2 rounded-lg border border-ps-blue/40 bg-ps-blue/10 px-3 py-1.5"
                title={t("storage_hub.server_addr_hint", "在电脑或手机浏览器里输入这个地址即可打开本面板")}
              >
                <span className="text-[10px] font-bold uppercase tracking-widest text-ps-blue/70">
                  {t("storage_hub.lan_badge", "LAN")}
                </span>
                <span className="font-mono text-sm font-bold text-ps-blue tracking-tight select-all">
                  {ip}:8911
                </span>
              </div>
            </div>
            <div
              className="shrink-0 rounded-lg bg-white p-1.5"
              title={t("storage_hub.scan_hint", "手机扫码打开本面板")}
            >
              <QRCodeSVG value={`http://${ip}:8911`} size={isPS5 ? 96 : 80} level="M" />
            </div>
          </div>
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
              /* [CN-FORK] v0.6.0 合入：上游的 enrichedSources 里 payload 不带 source_id，
               * 所以这里一边找一边把命中的来源 id/url 记下来，装的时候直接回传。
               * 判定口径沿用 fork 旧逻辑（基名相同但文件名不同 = 有更新可升）。 */
              let remoteMatch = null
              let remoteSrcId = null
              let remoteSrcUrl = null
              for (const src of enrichedSources) {
                const found = (src.payloads || []).find(
                  (rp) => rp.filename === fileName || getBaseName(rp.filename) === getBaseName(fileName)
                )
                if (found) {
                  remoteMatch = found
                  remoteSrcId = src.id
                  remoteSrcUrl = src.url
                  break
                }
              }
              const hasUpdate = Boolean(remoteMatch && (remoteMatch.isUpdate || remoteMatch.filename !== fileName))
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
                        /* [CN-FORK] v0.5.2.103 收敛档：原写法是「悬停才变红」的灰图标，
                            现在改用 .is-danger —— 图标恒为危险色（小图标染色，不是实心红块），
                            不可逆的信号不再依赖悬停才出现（触屏/遥控器根本没有悬停）。 */
                        className="icon-btn is-danger"
                        title={t("storage_hub.remove_payload", "Remove Payload")}
                        aria-label={t("storage_hub.remove_payload", "Remove Payload")}
                      >
                        <Trash2 className="w-4 h-4" />
                      </button>
                    </div>
                  </div>
                  {hasUpdate && (
                    <button
                      onClick={(e) => { e.stopPropagation(); onInstall(remoteMatch, remoteSrcId, remoteSrcUrl) }}
                      /* [CN-FORK] v0.5.2.103 收敛档：原先是手写的实心 emerald 类（绕过骨架层的漏网实心块），
                          改走 .btn-ok —— 更新属「成功态动作」，语义不变，但形态跟全站一致（淡染底 + 同色字）。 */
                      className="btn btn-ok mt-1 w-full text-xs"
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
          {/* [CN-FORK] v0.5.2.92 排序从「隐形原生 select 盖在假按钮上」改成互斥档；
              v0.5.2.97 再把这三枚档并进全站共用的 Segmented（轨道、圆角、命中高度、选中档一处定义）。 */}
          <div className="flex items-center flex-none mt-3 md:mt-0 md:ml-3">
            <span className="text-zinc-500 text-xs font-medium whitespace-nowrap mr-2">
              {t("storage_hub.sort_by", "Sort by:")}
            </span>
            <Segmented
              label={t("storage_hub.sort_by", "Sort by:")}
              value={sortMode}
              onChange={handleSortChange}
              options={[
                { value: 'category', label: t("storage_hub.sort_category", "Category") },
                { value: 'update_time', label: t("storage_hub.sort_time", "Update Time") },
                { value: 'name', label: t("storage_hub.sort_name", "Name") },
              ]}
            />
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
              <button onClick={() => fetchRemote(true)} className="btn btn-info text-xs">{t("storage_hub.retry_btn", "Retry Connection")}</button>
            }
          />
        ) : enrichedSources.length > 0 ? (
          <div className="space-y-4">
            /* ===== REPOSITORY CATALOGS ===== */
          {enrichedSources.map((src) => {
            let availablePayloads = [...src.payloads]

            if (search.trim() !== '') {
              const q = search.toLowerCase()
              availablePayloads = availablePayloads.filter(
                (p) =>
                  (p.name && p.name.toLowerCase().includes(q)) ||
                  (p.id && p.id.toLowerCase().includes(q)) ||
                  (p.filename && p.filename.toLowerCase().includes(q)) ||
                  (p.description && p.description.toLowerCase().includes(q))
              )
            }

            // Auto-expand if there's only 1 source, otherwise respect state or active search
            const isExpanded =
              enrichedSources.length === 1 || expandedSource === src.id || search.trim() !== ''

            let hasMultipleCategories = false
            let groupedPayloads = {}
            let categories = []

            if (sortMode === 'category' && search.trim() === '') {
              groupedPayloads = availablePayloads.reduce((acc, p) => {
                const catName = p.category ? t(`category.${p.category}`, p.category) : t("storage_hub.uncategorized", "Uncategorized")
                if (!acc[catName]) acc[catName] = []
                acc[catName].push(p)
                return acc
              }, {})
              categories = Object.keys(groupedPayloads).sort()
              hasMultipleCategories = categories.length > 1
            } else {
              availablePayloads.sort((a, b) => {
                if (sortMode === 'update_time') {
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
              <div
                key={src.id}
                className={cn(
                  /* [CN-FORK] v0.5.2.91 多来源列表容器原用 bg-ps-card（card 档实色），
                   * 是 .90 板块并档后漏掉的一块，改走同一条 inset 通道 */
                  multiSources
                    ? "bg-white/[0.03] border border-ps-border rounded-ps-3xl overflow-hidden"
                    : "flex flex-col space-y-4"
                )}
              >
                {!multiSources && src.last_update > 0 && (
                  <p className="px-3 sm:px-4 text-xs uppercase tracking-widest text-zinc-500">
                    {t("storage_hub.last_sync", "Last Sync: {{date}}", {
                      date: formatSyncTime(src.last_update, i18n.resolvedLanguage, isPS5),
                    })}
                  </p>
                )}

                {multiSources && (
                  <button
                    onClick={() => setExpandedSource(isExpanded ? null : src.id)}
                    aria-expanded={isExpanded}
                    className="w-full flex items-center justify-between p-4 md:p-5 min-h-[var(--ctl-h)] hover:bg-white/5 transition-colors"
                  >
                    <div className="flex items-center space-x-4">
                      <div className="p-2.5 bg-ps-blue/10 rounded-xl">
                        <Globe className="w-5 h-5 text-ps-blue" />
                      </div>
                      <div className="text-left">
                        <p className="font-bold text-white text-lg">{src.name}</p>
                        {src.error && (
                          <p className="text-xs text-red-400 flex items-center space-x-1 mt-0.5">
                            <AlertTriangle className="w-3 h-3" />
                            <span>{t("storage_hub.fetch_failed", "Fetch failed")}</span>
                          </p>
                        )}
                        {src.last_update > 0 && (
                          <p className="text-xs text-zinc-500 uppercase tracking-widest mt-1">
                            {t("storage_hub.updated_at", "Updated {{date}}", {
                              date: formatSyncTime(src.last_update, i18n.resolvedLanguage, isPS5),
                            })}
                          </p>
                        )}
                      </div>
                    </div>
                    <div className="flex items-center space-x-3">
                      <span className="px-3 py-1 rounded-full bg-white/5 text-zinc-500 text-xs font-bold">
                        {t("storage_hub.payload_count", "{{count}} payloads", { count: availablePayloads.length })}
                      </span>
                      {enrichedSources.length > 1 && (
                        <ChevronDown
                          className={cn(
                            "w-5 h-5 text-zinc-500 transition-transform",
                            isExpanded && "rotate-180"
                          )}
                        />
                      )}
                    </div>
                  </button>
                )}

                {isExpanded && (
                  <div className={cn(multiSources ? "border-t border-white/5 p-3 sm:p-4 md:p-4.5" : "")}>
                    {src.payloads.length === 0 ? (
                      <EmptyState
                        dashed={false}
                        title={t("storage_hub.source_empty", "Source is empty")}
                      />
                    ) : hasMultipleCategories ? (
                      categories.map((cat) => (
                        <div key={cat} className="space-y-4 mb-8 last:mb-0">
                          <h4 className="text-white/80 font-bold uppercase tracking-widest text-sm px-2">{cat}</h4>
                          <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-3.5 sm:gap-4 md:gap-5">
                            {groupedPayloads[cat].map((p) => (
                              <PayloadCard
                                key={p.id || p.filename}
                                payload={p}
                                statusInfo={p.statusInfo}
                                isUpdate={p.isUpdate}
                                isInstalled={p.isInstalled}
                                onClick={() =>
                                  handlePayloadSelect({
                                    payload: p,
                                    srcId: src.id,
                                    srcUrl: src.url,
                                    statusInfo: p.statusInfo,
                                    isInstalled: p.isInstalled,
                                    isUpdate: p.isUpdate,
                                  })
                                }
                              />
                            ))}
                          </div>
                        </div>
                      ))
                    ) : (
                      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-3.5 sm:gap-4 md:gap-5">
                        {availablePayloads.map((p) => (
                          <PayloadCard
                            key={p.id || p.filename}
                            payload={p}
                            statusInfo={p.statusInfo}
                            isUpdate={p.isUpdate}
                            isInstalled={p.isInstalled}
                            onClick={() =>
                              handlePayloadSelect({
                                payload: p,
                                srcId: src.id,
                                srcUrl: src.url,
                                statusInfo: p.statusInfo,
                                isInstalled: p.isInstalled,
                                isUpdate: p.isUpdate,
                              })
                            }
                          />
                        ))}
                      </div>
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
                      /* [CN-FORK] v0.5.2.102 写入内部存储属「金」档（样张 A：上传/写入＝btn-warn），
                       * 与同页「重试连接」的青档（信息工具类）分开 */
                      className="btn btn-warn group/btn"
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
