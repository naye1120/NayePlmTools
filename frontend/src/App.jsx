import { useHistoryNavigation } from './hooks/useHistoryNavigation'
import { createRequestScope } from './utils/requestScope'
import React, { useState, useEffect, useRef, useCallback } from 'react'
import {
  Settings,
  Cpu,
  Database,
  RefreshCw,
  Globe,
  Package,
  Menu,
  Terminal,
  X,
  Link2,
  Gamepad2,
  Info
} from 'lucide-react'
import { useTranslation } from 'react-i18next'

import './App.css'
import { apiFetch, mutationFetch } from './utils/mutationRequest'

// Contexts
import { useRepository } from './contexts/RepositoryContext'

// Utilities
import { cn, isPS5, formatSourceTag } from './utils/helpers'
import { slug } from './utils/versions'
import { resolveSourceDisplayName } from './utils/payloadStatus'

// UI Components
import Toast from './components/ui/Toast'
import Modal from './components/ui/Modal'
import NavButton from './components/ui/NavButton'
// [CN-FORK] v0.6.0 合入：加载遮罩的图标/名称改成上游这两个新组件（PayloadName 是
// fork 里原有的，PayloadIcon 是上游新增的）。PayloadButton 未引入 —— fork 的
// payload 列表全部走自己的 StorageHub，没有上游的 dashboard 视图。
import PayloadIcon from './components/ui/PayloadIcon'
import PayloadName from './components/ui/PayloadName'
import LogoIcon from './components/ui/LogoIcon'
import ErrorBoundary from './components/ui/ErrorBoundary'

// Views
import StorageHub from './components/views/StorageHub'
import AutoloadView from './components/views/AutoloadView'
import SettingsView from './components/views/SettingsView'
// [CN-FORK] v0.5.2.154 「关于」页取代了原来的「支持作者」页（`DonateView` 已删）：
// 更新日志 + 支持作者 + 本 Fork 署名 + 参考致谢 四块合并成一页，挂在设置下面。
import AboutView from './components/views/AboutView'
import AutoloadOverlay from './components/views/AutoloadOverlay'
import MoveFromUsbView from './components/views/MoveFromUsbView'
import LogViewer from './components/views/LogViewer'
import ManageSourcesView from './components/views/ManageSourcesView'
import ActiveProcessesView from './components/views/ActiveProcessesView'
// [CN-FORK] 常用入口页 + 游戏管理页（自常用入口抽出）；PKG安装：内嵌 PKG Manager 自带界面（:8844）到右侧内容区
import CommonEntriesView from './components/views/CommonEntriesView'
import GameManageView from './components/views/GameManageView'
import PkgManagerFrame from './components/views/PkgManagerFrame'
// [CN-FORK] v0.5.1.64 主页（DashboardView）已删除：硬件信息探测下线后它只剩空壳，
// 启动直接落在「管理 Payload」（view='storage'）

// [CN-FORK] 中文排版开关（语言同步）；外观切换控件仅保留在设置页，不常驻侧栏
import { syncHtmlLang } from './theme/useTheme'
// [CN-FORK] v0.5.2.92 遥控器空间导航（仅 PS5 端启用）
import { enableSpatialNav } from './utils/spatialNav'

/* [CN-FORK] v0.5.2.96 导航三形态共用一份菜单表。
 * v0.5.2.163 手机端二级菜单：原 7 项里 6 项合并为「工具 / 系统」两组二级菜单。
 *   · 工具组：PKG安装 / 常用配置 / 游戏管理
 *   · 系统组：活动进程 / 设置 / 关于
 * 桌面侧栏与电视端不变 —— 它们仍有 9 项（两组里要把子项展平出来）。
 *
 * 数据结构：
 *   group       = 'main' | 'system' | 'tools' | 'sys' | null
 *   mobileGroup = 仅手机端有：'top'（顶级直接进入）+ 'sub:tools' / 'sub:sys'（二级子菜单）
 * 顶层与子菜单分两层，每一项 type 决定：
 *   type='nav'   = 真页面（直接进）
 *   type='group' = 二级菜单按钮（点开弹子项）*/
const NAV_ITEMS = [
  // ── 顶级直接进入（PC / 电视 / 手机都展示） ──
  { id: 'storage', type: 'nav', group: 'main', icon: Database, label: ['app.nav.storage', 'Manage Payloads'], mobile: ['app.nav.manage_mobile', 'Manage'] },
  { id: 'autoload', type: 'nav', group: 'main', icon: RefreshCw, label: ['app.nav.autoload', 'Autoload'], mobile: ['app.nav.auto_mobile', 'Auto'] },
  // ── 工具组（仅手机端是二级菜单） ──
  { id: 'tools', type: 'group', group: 'main', icon: Package, label: ['app.nav.tools', 'Tools'], mobile: ['app.nav.tools_mobile', 'Tools'] },
  // ── 工具组子菜单 ──
  { id: 'pkg', type: 'nav', parent: 'tools', group: 'main', icon: Package, label: ['app.nav.pkg', 'PKG Manager'], mobile: ['app.nav.pkg_mobile', 'PKG'] },
  { id: 'common_entries', type: 'nav', parent: 'tools', group: 'main', icon: Link2, label: ['app.nav.common_entries', 'Quick Links'], mobile: ['app.nav.common_entries_mobile', 'Links'] },
  { id: 'game_manage', type: 'nav', parent: 'tools', group: 'main', icon: Gamepad2, label: ['app.nav.game_manage', 'Game Manager'], mobile: ['app.nav.game_manage_mobile', 'Games'] },
  // ── 系统组顶级 ──
  { id: 'system', type: 'group', group: 'system', icon: Cpu, label: ['app.nav.system', 'System'], mobile: ['app.nav.system_mobile', 'System'] },
  // ── 系统组子菜单 ──
  { id: 'processes', type: 'nav', parent: 'system', group: 'system', icon: Cpu, label: ['app.nav.processes', 'Active Processes'], mobile: ['app.nav.processes_mobile', 'Processes'] },
  { id: 'settings', type: 'nav', parent: 'system', group: 'system', icon: Settings, label: ['app.nav.settings', 'Settings'], mobile: ['app.nav.settings', 'Settings'] },
  /* [CN-FORK] v0.5.2.154 用户口径：「在设置下边增加一个菜单叫关于，把更新日志和支持作者的
   * 内容全部放到关于里面」。所以它紧跟在设置之后、同属「系统管理」组，并取代了原来
   * 藏在设置页里的「更新日志」入口与独立的「支持作者」页。
   * v0.5.2.163 移入 system 二级菜单。 */
  { id: 'about', type: 'nav', parent: 'system', group: 'system', icon: Info, label: ['app.nav.about', 'About'], mobile: ['app.nav.about_mobile', 'About'] },
]

/* [CN-FORK] v0.5.2.163 手机端二级菜单的子项按 parent 索引。
 * mobile-only（桌面侧栏仍然按 group='main'|'system' 一栏平铺）。 */
const MOBILE_PARENTS = {
  tools: ['pkg', 'common_entries', 'game_manage'],
  system: ['processes', 'settings', 'about'],
}
const NAV_GROUPS = [
  { id: 'main', label: ['app.nav.group_main', 'Main'] },
  { id: 'system', label: ['app.nav.group_system', 'System'] },
]

/* [CN-FORK] v0.5.2.154 刷新后停在当前页（用户口径：「所有页面刷新后停留在当前页面」）。
 *
 * 存的是**视图 id 本身**，读回来先过一遍白名单 —— localStorage 用户能改，一个不认识的
 * id 会让下面那一串 `view === '…'` 一条都不命中，整页变成空白，比「刷新回首页」更糟。
 * 白名单 = 「刷新之后仍然成立」的那些页：`move_from_usb` 刻意不在里面 —— 它靠一个只在
 * 内存里的路径（从 USB 导入时选中的那个），刷新后那个路径就没了，留在那一页只会看到空白。
 * 不认的 id 一律回落到默认页 'storage'。 */
const VIEW_STORE_KEY = 'pldmgrView'
const RESTORABLE_VIEWS = new Set([...NAV_ITEMS.map((i) => i.id), 'sources'])
const DEFAULT_VIEW = 'storage'

function readStoredView() {
  try {
    const v = localStorage.getItem(VIEW_STORE_KEY)
    return v && RESTORABLE_VIEWS.has(v) ? v : DEFAULT_VIEW
  } catch {
    return DEFAULT_VIEW
  }
}

/* [CN-FORK] v0.6.0 合入：toast id 改用自增序号（原来是数组长度），连点两条
 * 同样的文案时也能拿到不同 key。 */
let toastSeq = 0

function App() {
  const { t, i18n } = useTranslation();
  const [view, setView] = useState(readStoredView) /* [CN-FORK] v0.5.1.64 默认页 payload 管理页；v0.5.2.154 起刷新后回到上次那一页 */
  /* ── 以下状态来自上游 v0.6.0（fork 侧原本是「用到什么才声明什么」，合并后
   * 统一收在这里，声明在首次使用之前）。 */
  const { fetchRemote, clearCache, sources } = useRepository()
  const [items, setItems] = useState([])
  const [selfUpdate, setSelfUpdate] = useState(null)
  const [uploadConflictModal, setUploadConflictModal] = useState({ show: false, file: null, folderName: '' })
  const [conflictPrompt, setConflictPrompt] = useState(null)
  const [externalLinkUrl, setExternalLinkUrl] = useState(null)
  const [installingPayloadId, setInstallingPayloadId] = useState(null)
  const [autoloadDelayModal, setAutoloadDelayModal] = useState(false)
  const [isManageMode, setIsManageMode] = useState(false)
  const [selectedPayload, setSelectedPayload] = useState(null)
  const mainRef = useRef(null)

  /* [CN-FORK] v0.5.2.154 把当前页写回去。**只写白名单里的页**：像 move_from_usb 这种
   * 「离开就失效」的页不写，免得它把上一次那个好页面盖掉、刷新后又被回落成首页。 */
  useEffect(() => {
    if (!RESTORABLE_VIEWS.has(view)) return
    try {
      localStorage.setItem(VIEW_STORE_KEY, view)
    } catch {
      /* 存不进去也不影响使用，下次刷新回默认页而已 */
    }
  }, [view])

  useEffect(() => {
    if (mainRef.current) {
      mainRef.current.scrollTop = 0
    }
    window.scrollTo(0, 0)
  }, [view])

  // [CN-FORK] 把当前语言同步到 <html lang>，触发中文排版层
  useEffect(() => {
    syncHtmlLang(i18n.language)
  }, [i18n.language])

  /* [CN-FORK] v0.5.2.92 设备档位写进 <html data-device>：index.css 里的命中区尺度
   * （--ctl-h / --row-h）读它来整体上抬，别在组件里各写各的 padding */
  useEffect(() => {
    document.documentElement.setAttribute('data-device', isPS5 ? 'ps5' : 'screen')
  }, [])

  /* [CN-FORK] v0.5.2.92 电视端接上方向键导航；PC/手机不动，避免抢走列表滚动用的方向键 */
  useEffect(() => {
    if (!isPS5) return undefined
    return enableSpatialNav()
  }, [])

  const [sidebarExpanded, setSidebarExpanded] = useState(() => {
    const saved = localStorage.getItem('sidebarExpanded')
    return saved !== null ? JSON.parse(saved) : true
  })

  /* [CN-FORK] v0.5.2.163 手机端二级菜单：'tools' | 'system' | null。
   * 点 group 按钮切到对应 parent；再点同一颗关闭；点子项关闭并跳转；点外部关闭。 */
  const [mobileMenu, setMobileMenu] = useState(null)

  useEffect(() => {
    localStorage.setItem('sidebarExpanded', JSON.stringify(sidebarExpanded))
  }, [sidebarExpanded])

  /* [CN-FORK] v0.5.2.112 侧栏展开态写给 <html data-sidebar>：index.css 的 --sidebar-w 读它，
   * 给「铺满右侧」的那类面板定位（面板的 left 必须正好等于侧栏宽度）。与 data-device 同手法
   * —— 属性写在 <html> 上，组件树里任何一层的面板都能靠一条 CSS 变量对齐，不用测量、不用上下文。 */
  useEffect(() => {
    document.documentElement.setAttribute('data-sidebar', sidebarExpanded ? 'expanded' : 'collapsed')
  }, [sidebarExpanded])
  const [autoloadStatus, setAutoloadStatus] = useState(null)
  const [logs, setLogs] = useState([])
  const [payloads, setPayloads] = useState([])
  const [config, setConfig] = useState({})
  const [ip, setIp] = useState('0.0.0.0')
  /* [CN-FORK] v0.6.0 合入：上游删掉了 version 状态（它的侧栏不显示版本号、
   * AboutView 也不要 version）。fork 的侧栏底部常驻「v{version}」、关于页拿它 → 补回来。 */
  const [version, setVersion] = useState('Loading...')
  const [loading, setLoading] = useState(false)
  /* [CN-FORK] v0.6.0 合入：加载态从「只有个名字」升级成「整个 payload 的展示信息」
   * （图标 / 名字 / 版本 / 来源徽标）。fork 原来的 activeLoadingName 随之删除 ——
   * loadPayload 与加载遮罩都被上游版本换掉了，它已无人引用。launchHistory 也未引入：
   * 上游只有 dashboard 的收藏卡会读它，fork 没有 dashboard 视图。 */
  const [activeLoadingPayload, setActiveLoadingPayload] = useState(null)
  const [toasts, setToasts] = useState([])
  /* 只留 setter：加载态本身没有渲染出口（payload 列表自带骨架），但状态留着
   * 给后面的刷新链判「第一次加载完没有」。去掉变量名是为了 no-unused-vars 不报 ——
   * 别顺手把整行删掉，setter 还在用。
   * v0.6.0 上游把变量名接回来了：AutoloadOverlay 的 itemsLoading 与 dashboard 的
   * 骨架分支都读它 → fork 侧恢复 [loadingPayloads, setLoadingPayloads]。 */
  const [loadingPayloads, setLoadingPayloads] = useState(true)
  const [downloadModal, setDownloadModal] = useState({ show: false, name: '', progress: 0 })
  /* [CN-FORK] v0.5.2.93 confirmModal 的 danger 档：删除文件不可逆 → 实心红确认条。
   * 覆盖 / 重装这类可重做的确认仍走主色橙。上游 0.6.0 没有这个字段。
   * payloadMeta 改为只读（fork 原本带个 never-used 的 setter）。 */
  const [confirmModal, setConfirmModal] = useState({ show: false, title: '', message: '', onConfirm: null, danger: false })
  const [moveFromUsbPath, setMoveFromUsbPath] = useState(null)
  const [storageScrollTarget, setStorageScrollTarget] = useState(null)
  const [showLogs, setShowLogs] = useState(false)
  const [payloadMeta, setPayloadMeta] = useState({}) /* [CN-FORK] /list_payloads 的 meta 表：filename -> meta，多源目录读它 */
  const { navigate, handleSelectPayload, handleClosePayload } = useHistoryNavigation({
    view,
    setView,
    confirmModal,
    setConfirmModal,
    downloadModal,
    setDownloadModal,
    showLogs,
    setShowLogs,
    isManageMode,
    setIsManageMode,
    selectedPayload,
    setSelectedPayload,
    moveFromUsbPath,
    setMoveFromUsbPath,
    delayModal: autoloadDelayModal,
    setDelayModal: setAutoloadDelayModal,
    uploadConflictModal,
    setUploadConflictModal,
    conflictPrompt,
    setConflictPrompt,
    externalLinkUrl,
    setExternalLinkUrl,
  })
  /* [CN-FORK] v0.6.0 合入：上游的 payload 收藏夹机制（favoritePayloads 本地状态 +
   * FAVORITES 前后端双向同步 + 退出前补写 /set_config + legacy_paths 迁移）整块未引入。
   * 它唯一的上游消费方是 dashboard 收藏区视图，fork 决定「管理 Payload 一页统管」、
   * 不引入那个视图 → 没有界面会写也没有界面会读，留着只会白发一次 /set_config。
   * 后端 /get_config、/set_config 的 FAVORITES 键保持原样，将来要做收藏 UI 再从这接线。 */
  useEffect(() => {
    if (!showLogs) return
    const eventSource = new EventSource('/events')
    eventSource.onmessage = (e) => {
      setLogs(prev => [...prev, e.data].slice(-200))
    }
    eventSource.addEventListener('gap', eventSource.onmessage)
    return () => eventSource.close()
  }, [showLogs])
  const [isOffline, setIsOffline] = useState(false)

  const showConfirm = (title, message, onConfirm, danger = false) => {
    setConfirmModal({
      show: true,
      title,
      message,
      danger,
      onConfirm: () => {
        setConfirmModal({ show: false })
        onConfirm()
      }
    })
  }

  const addToast = useCallback((message, type = 'success') => {
    toastSeq += 1
    const id = `${Date.now()}-${toastSeq}`
    setToasts(prev => [...prev, { id, message, type }])
  }, [])

  /* [CN-FORK] v0.5.2.92 视图进入顺序记在 ref 栈里，供遥控器「返回」键回退
   * （不写 URL/hash：PS5 老内核的 history 行为不可靠，栈在内存里最简单也够用）。 */
  const viewStackRef = useRef([])
  useEffect(() => {
    const stack = viewStackRef.current
    if (stack[stack.length - 1] !== view) {
      stack.push(view)
      if (stack.length > 12) stack.shift()
    }
  }, [view])

  /* [CN-FORK] v0.5.2.92 遥控器「返回」：先关最上层的浮层，浮层都空了才退视图。
   * 安装进行中不许退——那会儿退只会让用户以为没装上。 */
  useEffect(() => {
    const onBack = () => {
      if (confirmModal.show) { setConfirmModal({ show: false }); return }
      if (downloadModal.show) return
      if (showLogs) { setShowLogs(false); return }
      const stack = viewStackRef.current
      if (stack.length < 2) return
      stack.pop()
      setView(stack[stack.length - 1] || 'storage')
    }
    window.addEventListener('ps5-back', onBack)
    return () => window.removeEventListener('ps5-back', onBack)
  }, [confirmModal.show, downloadModal.show, showLogs])

  const removeToast = (id) => {
    setToasts(prev => prev.filter(t => t.id !== id))
  }

  const api = useCallback(async (endpoint, options = {}) => {
    try {
      const response = await (options.method === 'POST' ? mutationFetch(endpoint, options) : apiFetch(endpoint, options))
      if (!response.ok) return null
      if (options.method === 'POST') return response.text()
      try {
        const text = await response.text()
        if (text.toLowerCase().includes('<!doctype')) return null
        return JSON.parse(text)
      } catch { return null }
    } catch { return null }
  }, [])

  /* [CN-FORK] v0.6.0 合入：refreshPayloads 从「函数声明」改成「命名 useCallback」。
   * fork 那条「必须是函数声明，否则 Babel async 降级会挂」的坑是 v0.5.2.125 的事了 ——
   * 上游用的是**函数表达式**形式（async function retryPayloads(...)），自引用走的是
   * 函数名本身而不是外层 const，所以不受那条坑影响；
   * 重试定时器句柄也改挂 ref（payloadRetryRef），componentWillUnmount 不清也不会
   * 再往已卸载的组件里 setState（appAliveRef 兜住）。 */
  const payloadRetryRef = useRef(null)
  const appAliveRef = useRef(false)
  const refreshPayloads = useCallback(async function retryPayloads(retryCount = 0) {
    setLoadingPayloads(true)
    const data = await api('/list_payloads')
    if (!appAliveRef.current) return
    if (data?.items || data?.payloads) {
      const rawItems = data.items || []
      setItems(rawItems)
      setSelfUpdate(data.self || null)

      const sorted = [...(data.payloads || [])].sort((a, b) => {
        const aIsUsb = a.startsWith('/mnt/usb')
        const bIsUsb = b.startsWith('/mnt/usb')
        if (aIsUsb && !bIsUsb) return 1
        if (!aIsUsb && bIsUsb) return -1
        return a.localeCompare(b)
      })
      setPayloads(sorted)

      // [CN-FORK] Build metadata map: filename -> meta object（多源目录的
      // 「来源徽标 / 版本」读它；fork 原来的 setPayloadMeta 在这里）。
      if (data.meta && typeof data.meta === 'object') {
        setPayloadMeta(data.meta)
      }

      setLoadingPayloads(false)
    } else if (retryCount < 5) {
      clearTimeout(payloadRetryRef.current)
      payloadRetryRef.current = setTimeout(() => retryPayloads(retryCount + 1), 1000)
    } else {
      setItems([])
      setPayloads([])
      setLoadingPayloads(false)
    }
  }, [api])

  /* [CN-FORK] v0.5.2.96 删掉 refreshHistory / launchHistory：拉回来的启动历史没有任何界面消费
   * （消费方是已下线的仪表盘收藏区），但每次进页和每次启动都还在白发一次 /history_list。
   * v0.6.0 上游把它接回来了（launchHistory 只喂给 dashboard 收藏卡的 isLaunched 徽标），
   * fork 没有 dashboard 视图 → 继续不接；后端 /history_list 保留原样。 */

  const refreshConfig = useCallback(async () => {
    const data = await api('/get_config')
    if (!appAliveRef.current) return
    if (data) {
      setConfig(data)
    }
  }, [api])

  const handleAbort = async () => {
    await mutationFetch('/abort').catch(() => { })
    setAutoloadStatus(prev => prev ? { ...prev, remaining: -1 } : null)
    addToast(t("app.toasts.sequence_aborted", "Sequence Aborted"), "error")
  }

  const handleFinish = async () => {
    await mutationFetch('/autoload_clear').catch(() => { })
    setAutoloadStatus(null)
    window.location.reload()
  }

  const loadPayload = async (slotOrPath, presentation = {}) => {
    if (!slotOrPath) return
    const name = presentation.name || slotOrPath.split('/').pop().replace(/\.(elf|bin)$/i, '').replace(/_/g, ' ')
    setLoading(true)
    setActiveLoadingPayload({ ...presentation, key: slotOrPath, name, path: presentation.path || slotOrPath })
    try {
      const safeParam = encodeURI(slotOrPath)
      const res = await mutationFetch(`/loadpayload:${safeParam}`)
      if (!res.ok) throw new Error(`${t("app.toasts.launch_failed", "Launch failed")} (${res.status})`)
      addToast(t("app.toasts.payload_launched", "{{name}} launched", { name }))
      /* [CN-FORK] 上游在这里补了一句 refreshHistory()（刷新启动历史给 dashboard 徽标用），
       * fork 没有 dashboard 视图、且 .96 已主动删掉这条无人消费的请求 → 不接。 */
    } catch (e) { addToast(e.message || t("app.toasts.launch_failed", "Launch failed"), "error") }
    setTimeout(() => {
      setLoading(false)
      setActiveLoadingPayload(null)
    }, 1500)
  }

  const handleDelete = (slotOrFilename, customName) => {
    if (!slotOrFilename) return
    const name = customName || slotOrFilename
    showConfirm(
      t("app.modals.delete_payload.title", "Delete Payload"),
      t("app.modals.delete_payload.message", "Are you sure you want to remove {{fileName}}?", { fileName: name }),
      async () => {
        const res = await mutationFetch(`/manage:delete?slot=${encodeURIComponent(slotOrFilename)}`)
        if (!res.ok) {
          addToast(`Delete failed (${res.status})`, 'error')
          return
        }
        refreshPayloads()
        /* [CN-FORK] v0.6.0 合入：占位变量从「不存在的 fileName」改成 name ——
         * fork 原来这里写 { fileName }，函数里没有这个标识符，删成功时的提示
         * 会显示成「undefined removed」。上游顺手修掉了，采纳。 */
        addToast(t("app.toasts.payload_removed", "{{fileName}} removed", { fileName: name }))
      },
      /* [CN-FORK] v0.5.2.93 删除文件不可逆 → 危险档确认（实心红）；覆盖/重装这类可重做的确认走主色橙 */
      true
    )
  }

  const handleUpload = async (e) => {
    const input = e.target
    const file = input.files?.[0]
    input.value = ''
    if (!file) return

    const ext = file.name.split('.').pop().toLowerCase()
    if (ext !== 'elf' && ext !== 'bin') {
      addToast(
        t("app.toasts.unsupported_file", "Unsupported file type. Only .elf and .bin files are allowed."),
        "error"
      )
      return
    }

    try {
      const res = await apiFetch(`/manage:check?filename=${encodeURIComponent(file.name)}`)
      const data = await res.json()
      if (data?.existing || data?.file_exists || data?.folder_exists) {
        setUploadConflictModal({
          show: true,
          file,
          folderName: data.key || data.folder_name || file.name,
        })
      } else {
        performUpload(file, 'new')
      }
    } catch {
      /* 探活失败就当「没有同名文件」，直接上传 —— 这一步只是省一次确认框。
       * [CN-FORK] v0.6.0 合入：显式传 mode='new'（上游写法；默认值也是 'new'，
       * 但 /manage:upload 的 mode 语义靠它决定「保留同名」还是「覆盖」，写明白更稳）。 */
      performUpload(file, 'new')
    }
  }

  const performUpload = async (file, mode = 'new') => {
    setUploadConflictModal({ show: false, file: null, folderName: '' })
    setDownloadModal({ show: true, name: file.name, progress: 20 })
    try {
      const res = await mutationFetch(
        `/manage:upload?filename=${encodeURIComponent(file.name)}&mode=${encodeURIComponent(mode)}`,
        {
          method: 'POST',
          headers: { 'Content-Type': 'application/octet-stream' },
          body: file,
        }
      )
      if (!res.ok) throw new Error(`${t("app.toasts.upload_failed", "Upload failed")} (${res.status})`)
      setDownloadModal((prev) => ({ ...prev, progress: 100 }))
      addToast(t("app.toasts.payload_uploaded", "{{fileName}} uploaded", { fileName: file.name }))
      refreshPayloads()
    } catch (e) {
      addToast(e.message || t("app.toasts.upload_failed", "Upload failed"), "error")
    }
    setTimeout(() => setDownloadModal({ show: false }), 800)
  }

  const handleInstall = async (p, sourceId, repoUrl, options = {}) => {
    performInstall(p, sourceId, repoUrl, options)
  }

  const performInstall = async (p, sourceId, repoUrl, options = {}) => {
    setConfirmModal({ show: false })
    const displayName = p.name || p.filename
    const payloadId = p.id || slug(p.name || p.filename)
    setInstallingPayloadId(payloadId)
    setDownloadModal({ show: true, name: displayName, progress: 10 })
    try {
      setDownloadModal((prev) => ({ ...prev, progress: 30 }))
      let url = `/repository_install?id=${encodeURIComponent(payloadId)}`
      if (repoUrl) url += `&source_url=${encodeURIComponent(repoUrl)}`
      if (options.mode) url += `&mode=${encodeURIComponent(options.mode)}`
      if (options.replaceSlot) url += `&replace_slot=${encodeURIComponent(options.replaceSlot)}`

      const res = await mutationFetch(url)
      setDownloadModal((prev) => ({ ...prev, progress: 80 }))

      const data = await res.json().catch(() => null)
      if (res.ok && data?.ok) {
        setDownloadModal((prev) => ({ ...prev, progress: 100 }))
        addToast(
          data.self_update ? t("app.toasts.self_update_downloaded", "Update downloaded — applies on next start") : t("app.toasts.payload_installed", "{{fileName}} installed", {
            fileName: displayName,
          })
        )
        await refreshPayloads()
        if (data.refreshed) {
          fetchRemote(true)
        }
      } else if (data?.conflict) {
        setDownloadModal({ show: false, name: '', progress: 0 })
        const conflict = data.conflict
        const thisRepoName = resolveSourceDisplayName({ source_url: repoUrl }, sources)
        setConfirmModal({
          show: true,
          title: t("storage_hub.conflict_title", "Payload Conflict"),
          message: t(
            "storage_hub.conflict_message",
            "\"{{name}}\" is already installed from {{otherSource}} ({{version}}). Install from {{thisSource}} too?",
            {
              name: displayName,
              otherSource: resolveSourceDisplayName(conflict, sources),
              version: conflict.version || "",
              thisSource: thisRepoName,
            }
          ),
          onConfirm: () =>
            performInstall(p, sourceId, repoUrl, {
              mode: 'replace',
              replaceSlot: conflict.slot,
            }),
        })
        return
      } else {
        throw new Error(data?.message || t("app.toasts.install_failed", "Installation failed"))
      }
    } catch (e) {
      addToast(e.message || t("app.toasts.install_failed", "Installation failed"), 'error')
    } finally {
      setInstallingPayloadId(null)
    }
    setTimeout(() => setDownloadModal({ show: false }), 800)
  }

  const handleImportFromUsb = (path) => {
    setMoveFromUsbPath(path)
    navigate('move_from_usb', { path })
  }

  const handleSaveConfig = async (newConfig) => {
    const success = await api('/set_config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(newConfig),
      keepalive: true,
    })
    if (success) {
      refreshConfig()
      return true
    } else {
      addToast(t("app.toasts.save_failed", "Save failed"), "error")
      return false
    }
  }

  useEffect(() => {
    appAliveRef.current = true
    const controller = createRequestScope()
    const init = async () => {
      let offline = false
      try {
        const ipRes = await apiFetch('/getip', { signal: controller.signal }).then(r => r.text())
        if (controller.aborted) return
        if (ipRes.toLowerCase().includes('<!doctype')) offline = true
        else setIp(ipRes)
      } catch {
        offline = true
      }

      try {
        const verRes = await apiFetch('/version', { signal: controller.signal }).then(r => r.text())
        if (verRes.toLowerCase().includes('<!doctype')) offline = true
        /* [CN-FORK] v0.6.0 对比：上游删掉了「拉 /version 并 setVersion」这段（它的侧栏不再显示
         * 版本号、AboutView 也不要 version）。fork 的侧栏底部常驻「v{version}」、关于页要
         * 拿 version → 保留 fork 侧。 */
        else setVersion(verRes)
      } catch {
        offline = true
      }

      if (controller.aborted) return
      if (offline) {
        setIsOffline(true)
      } else {
        refreshPayloads()
        refreshConfig()
      }
    }
    void init()
    return () => {
      appAliveRef.current = false
      controller.abort()
      clearTimeout(payloadRetryRef.current)
    }
  }, [refreshConfig, refreshPayloads])




  useEffect(() => {
    if (view === 'autoload' || view === 'storage') {
      /* 换页时刷一次远端配置与 payload 列表：这是去主机重新拉一份外部系统的读，
       * 不是派生 state。 */
      refreshConfig()
      refreshPayloads()
    } else if (view === 'game_manage') {
      /* [CN-FORK] v0.5.2.144 进游戏管理页也刷一次配置：单游戏存档弹窗要读全局存档配置
       * （ARCHIVE_GLOBAL_CONFIG，在设置页写）。设置页保存时会自己 refreshConfig，
       * 但若全局配置是被别的途径改的（FTP / 另一台设备），进这个页面就得重新拿一次，
       * 否则弹窗里显示的还是上一次的远程位置。只需 config，不需要 payloads。 */
      refreshConfig()
    }
  }, [view, refreshConfig, refreshPayloads])

  useEffect(() => {
    let statusTimeout
    const controller = createRequestScope()
    async function poll() {
      try {
        const res = await apiFetch('/autoload_status', { signal: controller.signal, cache: 'no-store' })
        if (res.ok) {
          const data = await res.json()
          if (controller.aborted) return
          setAutoloadStatus(data)

          // Poll as long as autoload is active (remaining >= 0), including DONE state
          // Only stop when remaining goes negative (after /autoload_clear is called)
          const isActive = data && Number.isFinite(data.remaining) && data.remaining >= 0
          if (isActive) {
            const delay = data.remaining > 0 ? 600 : 500
            statusTimeout = setTimeout(poll, delay)
          }
          return
        }
      } catch {
        /* 读不到运行状态就当「没在跑」，下一轮 5 秒后再问一次。
         * [CN-FORK] v0.6.0 合入：补 controller.aborted 判据 —— 组件已卸载时
         * 不再排下一轮，避免往已卸载组件里 setState 触发 React 告警。 */
        // network or server transient error
      }
      if (!controller.aborted) statusTimeout = setTimeout(poll, 5000)
    }
    poll()
    return () => { controller.abort(); clearTimeout(statusTimeout) }
  }, [])



  if (isOffline) {
    return (
      <div className="min-h-screen ps5-bg text-zinc-100 font-ps5 flex flex-col items-center justify-center p-4 text-center">
        <div className="max-w-lg p-12 bg-black/40 rounded-3xl border border-white/5">
          <div className="text-7xl font-light text-zinc-400 mb-12 font-mono">:(</div>
          <h1 className="text-2xl font-bold mb-4 text-zinc-300">{t("app.offline.title", "NayePlmTools is not running...")}</h1>
          <p className="text-lg text-zinc-400 leading-relaxed">{t("app.offline.message_1", "Please ensure you have loaded")} <strong>NayePlmTools</strong> {t("app.offline.message_2", "on your PS5 before launching this application.")}</p>
        </div>
      </div>
    )
  }

  // Keep overlay active while remaining >= 0, including DONE state.
  // The overlay is only dismissed when handleFinish() calls /autoload_clear,
  // which resets remaining to -1 on the backend.
  const isAutoloadActive = autoloadStatus && Number.isFinite(autoloadStatus.remaining) && autoloadStatus.remaining >= 0;

  if (isAutoloadActive) {
    return <AutoloadOverlay status={autoloadStatus} items={items} itemsLoading={loadingPayloads} onCancel={handleAbort} onFinish={handleFinish} isPS5={isPS5} />;
  }

  return (
    <div className={cn(
      "min-h-screen min-h-[100dvh] ps5-bg text-zinc-100 font-ps5 flex",
      isPS5 ? "flex-row overflow-hidden" : "flex-col md:flex-row md:overflow-hidden"
    )}>
      {/* Toast Container
          [CN-FORK] v0.5.2.109 用户口径：「提示标签…太大太占地方了，能不能放到中下边」。
          从右上角挪到**底部居中**；手机端要抬到底部导航栏之上（--bar-h + 安全区），
          电视端没有底部栏（isPS5 时那根 bar 是 hidden），留一点边距就行。 */}
      <div className={cn(
        "fixed inset-x-0 bottom-0 z-[2000] flex flex-col items-center gap-2 px-4 pointer-events-none",
        isPS5 ? "pb-6" : "pb-[calc(var(--bar-h)+env(safe-area-inset-bottom)+1rem)] md:pb-6"
      )}>
        {toasts.map(t => (
          <Toast key={t.id} message={t.message} type={t.type} onClose={() => removeToast(t.id)} />
        ))}
      </div>

      {/* Modals */}
      {/* [CN-FORK] v0.5.2.112 层级约定（这几档是**跨组件**的，写在这里当唯一出处）：
       *   1000  页面内弹窗（游戏详情 / 存档 / 多媒体 / 改名 / 待定）
       *   1100  多媒体预览浮层（要压过它自己那个弹窗）
       *   1900  App 级浮层：确认框、安装进度 —— **必须**高于 1000，否则被页面内弹窗盖住
       *   2000  通知条容器（pointer-events-none，不吃点击）
       *   9999  全屏加载 / 日志
       * 1900 这一档是修事故加上的：确认框原来和页面内弹窗同为 1000，页面内弹窗在 DOM 里更靠后，
       * 于是「在游戏详情里点删除源」弹出的确认框被详情弹窗整个盖住 —— 看不见也点不到。 */}
      {/* [CN-FORK] v0.5.2.93 安装/上传进行中是「不可打断」浮层：不给关闭入口，也不认返回键 */}
      <Modal show={downloadModal.show} title={t("app.modals.installing.title", "Installing Payload")} dismissible={false} containerClass="z-[1900]">
        <div className="space-y-6">
          <div className="flex justify-between items-end">
            <span className="text-ps-blue font-black uppercase italic tracking-tighter text-lg">{downloadModal.name}</span>
            <span className="text-white font-bold text-base">{downloadModal.progress}%</span>
          </div>
          <div className="h-4 bg-white/5 rounded-full overflow-hidden border border-white/10 p-0.5">
            <div className="h-full bg-ps-blue rounded-full transition-all duration-500" style={{ width: `${downloadModal.progress}%` }} />
          </div>
        </div>
      </Modal>

      <Modal
        show={confirmModal.show}
        title={confirmModal.title}
        onClose={() => setConfirmModal({ show: false })}
        containerClass="z-[1900]"
        footer={
          <>
            <button onClick={() => setConfirmModal({ show: false })} className="btn btn-outline flex-1 uppercase tracking-tight">{t("app.buttons.cancel", "Cancel")}</button>
            {/* [CN-FORK] v0.5.2.93 确认按钮按可逆性分档：不可逆操作（删除、杀进程、移除来源）才是实心红，
                可恢复的确认（覆盖/重装/更新）用主色橙。原来一律红底，等于每个弹窗都在喊「危险」。 */}
            <button onClick={confirmModal.onConfirm} className={cn("btn flex-1 uppercase tracking-tight", confirmModal.danger ? "bg-red-600 hover:bg-red-500 text-white" : "btn-primary")}>{t("app.buttons.confirm", "Confirm")}</button>
          </>
        }
      >
        {confirmModal.message}
      </Modal>

      {/* [CN-FORK] v0.5.2.85 侧栏底改 bg-ps-surface（变量驱动）：深色为藏蓝夜轨、浅色为淡蓝侧栏，
          与画布 ps-black 拉开一档层次，不再靠 bg-black/40 的半透黑叠加 */}
      {/* [CN-FORK] v0.6.0 合入：上传撞名确认框（Replace / Keep both）。fork 的 StorageHub
          自带上传流程、目前不弹它，但 uploadConflictModal 状态本来就要给
          useHistoryNavigation 用，留着这个 Modal 不增加成本。 */}
      <Modal
        show={uploadConflictModal.show}
        title={t("app.modals.overwrite.title", "Upload Payload")}
        onClose={() => setUploadConflictModal({ show: false, file: null, folderName: '' })}
        footer={
          <div className="flex flex-col sm:flex-row w-full gap-3">
            <button
              type="button"
              onClick={() => setUploadConflictModal({ show: false, file: null, folderName: '' })}
              className="flex-1 py-3 px-4 rounded-xl bg-white/5 hover:bg-white/10 text-white font-bold uppercase text-sm transition-all"
            >
              {t("app.buttons.cancel", "Cancel")}
            </button>
            <button
              type="button"
              onClick={() => performUpload(uploadConflictModal.file, 'replace')}
              className="flex-1 py-3 px-4 rounded-xl bg-amber-600 hover:bg-amber-500 text-white font-black uppercase text-sm transition-all shadow-lg"
            >
              {t("storage_hub.conflict_replace", "Replace")}
            </button>
            <button
              type="button"
              onClick={() => performUpload(uploadConflictModal.file, 'keep_both')}
              className="flex-1 py-3 px-4 rounded-xl bg-ps-blue hover:bg-ps-blue/80 text-white font-black uppercase text-sm transition-all shadow-lg"
            >
              {t("storage_hub.conflict_keep_both", "Keep both")}
            </button>
          </div>
        }
      >
        <p>
          {t(
            "app.modals.overwrite.folder_exists",
            "A version of this payload exists in internal storage ({{folderName}}). Replace the existing version or keep both?",
            { folderName: uploadConflictModal.folderName }
          )}
        </p>
      </Modal>

      <aside className={cn(
        "flex-col bg-ps-surface border-r border-white/5 transition-all duration-500 z-[100] h-screen",
        isPS5 ? "flex" : "hidden md:flex",
        sidebarExpanded ? "w-80" : "w-28"
      )}>
        <div className="p-6 flex flex-col h-full">
          <div className="flex items-center mb-12 h-10">
            <button
              onClick={() => setSidebarExpanded(!sidebarExpanded)}
              /* [CN-FORK] v0.5.2.96 折叠钮归到 .icon-btn：命中区跟着 --ctl-h 走（PC 44 / 电视 68），
                 原来固定 p-3 在电视上只有 48px，是全侧栏最小的一颗。悬停不再刷成整块实心橙，
                 与全站图标钮同一档（浮出 inset 底 + 图标转动作色）。 */
              className="icon-btn mr-4"
            >
              <Menu className="w-6 h-6" />
            </button>
            <div className={cn("flex items-center space-x-3 transition-all duration-500", sidebarExpanded ? "opacity-100 scale-100" : "opacity-0 scale-90 absolute pointer-events-none")}>
              {/* [CN-FORK] 侧栏 logo 改用统一应用图标位图（自带深色圆角底，去掉原 bg-ps-blue 底板） */}
              <LogoIcon className="w-9 h-9 rounded-xl" />
              <span className="text-2xl font-bold tracking-tight text-white">NayePlmTools</span>
            </div>
          </div>

          {/* 菜单按「常用功能 / 系统管理」分组，符合中文后台习惯；视图 id 与上游保持一致 */}
          <nav className="flex-1 space-y-2 overflow-y-auto custom-scrollbar">
            {NAV_GROUPS.map((g) => (
              <div key={g.id}>
                {sidebarExpanded && <p className="nav-group-title">{t(g.label[0], g.label[1])}</p>}
                {/* [CN-FORK] v0.5.2.163 侧栏过滤：type='group' 的项（Tools / System 触发器）
           * 只在手机端二级菜单出现，侧栏不展示（桌面/电视端把子项直接平铺）。
           * 子项里 `group` 字段已设好对应父组，所以仍然会按组聚合。 */}
              {NAV_ITEMS.filter((i) => i.type !== 'group' && i.group === g.id).map((i) => (
                  <NavButton
                    key={i.id}
                    sidebar
                    sidebarExpanded={sidebarExpanded}
                    active={view === i.id}
                    onClick={() => setView(i.id)}
                    icon={i.icon}
                    label={t(i.label[0], i.label[1])}
                  />
                ))}
              </div>
            ))}
          </nav>
          {/* [CN-FORK] 侧栏底部：fork 维护者 naye 常驻署名 + 当前版本 */}
          <div className="pt-4 mt-2 border-t border-white/5 shrink-0 text-center px-2">
            {sidebarExpanded ? (
              <p className="text-[11px] text-zinc-600 font-medium tracking-wide whitespace-nowrap">
                {t("app.fork.by", "Fork by {{maintainer}}", { maintainer: "naye" })}
                <span className="mx-1 text-zinc-700">·</span>
                <span className="font-mono">v{version}</span>
              </p>
            ) : (
              <p className="text-[10px] text-zinc-700 font-mono">{version !== 'Loading...' ? `v${version}` : ''}</p>
            )}
          </div>
        </div>
      </aside>

      {/* MOBILE BOTTOM NAV */}
      <nav
        style={{ gridTemplateColumns: 'repeat(4, minmax(0, 1fr))' }}
        className={cn(
        /* [CN-FORK] v0.5.2.96 等宽格子取代横向滚动：七个入口全部一屏可见、拇指可及 */
        /* [CN-FORK] v0.5.2.99 底部栏底改实色 rail 档（bg-ps-surface → var(--color-rail)）。
         * 原来是 bg-black/80：收口层只并了 /20 /40 /50 到 inset 档，/80 没人管，于是它是字面的
         * 八成黑 —— 列表滚到底时「Shadow Mount + 自动 / http://127.0.0.1:10101/」直接从导航栏
         * 后面透出来（v0.5.2.88 定的约束：浮层底必须实色）。改走变量还顺带修了浅色主题：
         * 浅色下这条栏此前是块压不黑的死黑。 */
        "fixed bottom-0 inset-x-0 z-[100] bg-ps-surface border-t border-ps-border h-[calc(var(--bar-h)+env(safe-area-inset-bottom))] pb-[env(safe-area-inset-bottom)] grid",
        isPS5 ? "hidden" : "md:hidden"
      )}>
        {/* 手机端任务栏与侧栏同源（NAV_ITEMS）：手机端任务栏此前手动加长到与桌面侧栏同项，
            现在不用再担心两边漂移；捐赠不占底部标签位，由设置页「支持作者」入口进入。 */}
        {NAV_ITEMS.filter((i) => !i.parent).map((i) => {
          /* [CN-FORK] v0.5.2.163 顶栏只放顶级（不带 parent 的）。Tools / System 是
           * group 类型，点开切到 mobileMenu；子项不占顶栏格子，以弹出菜单覆盖
           * 在按钮上方（见下方二级菜单浮层）。 */
          if (i.type === 'group') {
            const open = mobileMenu === i.id
            return (
              <NavButton
                key={i.id}
                active={open || MOBILE_PARENTS[i.id]?.includes(view)}
                onClick={() => setMobileMenu(open ? null : i.id)}
                icon={i.icon}
                label={t(i.label[0], i.label[1])}
                mobileLabel={t(i.mobile[0], i.mobile[1])}
              />
            )
          }
          return (
            <NavButton
              key={i.id}
              active={view === i.id}
              onClick={() => { setView(i.id); setMobileMenu(null) }}
              icon={i.icon}
              label={t(i.label[0], i.label[1])}
              mobileLabel={t(i.mobile[0], i.mobile[1])}
            />
          )
        })}
      </nav>

      {/* [CN-FORK] v0.5.2.163 手机端二级菜单浮层：每组 3 个子项，叠在底部 nav 上方。
       * z 压在 nav 之上但低于 toast / 弹窗；点击子项切换 view 并关闭菜单；
       * 点击外侧关闭。 */}
      {!isPS5 && mobileMenu && MOBILE_PARENTS[mobileMenu] && (
        <div
          className="fixed inset-x-0 bottom-[calc(var(--bar-h)+env(safe-area-inset-bottom))] z-[110] md:hidden"
          onClick={() => setMobileMenu(null)}
        >
          <div className="bg-ps-surface border-t border-ps-border shadow-2xl">
            <ul className="grid grid-cols-3 divide-x divide-white/5">
              {MOBILE_PARENTS[mobileMenu].map((childId) => {
                const child = NAV_ITEMS.find((x) => x.id === childId)
                if (!child) return null
                const Icon = child.icon
                return (
                  <li key={child.id}>
                    <button
                      type="button"
                      onClick={() => { setView(child.id); setMobileMenu(null) }}
                      className={cn(
                        'w-full flex flex-col items-center justify-center gap-1.5 py-3 transition-colors',
                        view === child.id
                          ? 'bg-ps-blue/10 text-ps-blue'
                          : 'text-white hover:bg-white/[0.05]',
                      )}
                      aria-current={view === child.id ? 'page' : undefined}
                    >
                      <Icon className="w-5 h-5" />
                      <span className="text-[12px] font-semibold">{t(child.mobile[0], child.mobile[1])}</span>
                    </button>
                  </li>
                )
              })}
            </ul>
          </div>
        </div>
      )}

      {/* MAIN CONTENT AREA */}
      <div className={cn(
        "flex flex-col relative min-w-0",
        isPS5 ? "h-screen flex-1 min-h-0" : "md:h-screen md:flex-1 md:min-h-0"
      )}>
        <main ref={mainRef} className={cn(
          "custom-scrollbar w-full flex flex-col",
          /* [CN-FORK] 「PKG安装」要铺满整个模块页：四边内边距全清零、解除普通页面用的
           * 1800px 居中宽度上限（超宽屏下它会让 iframe 左右留边）、改 overflow-hidden 让
           * iframe 自己接管滚动。本项目没有 fixed 覆盖式顶标（各视图自带 h1），pkg 视图里
           * main 的第一个子元素就是 iframe，所以原先那 pt-16 只是 64px 纯空白，去掉不会遮内容。
           * 手机端（<md）父容器没有 h-screen，flex-1 拿不到确定高度会让 iframe 塌成 150px，
           * 故显式给一个「视口高 - 底部导航」的高度；md 以上交回 flex-1。 */
          view === 'pkg'
            ? (isPS5
                ? "pt-0 px-0 pb-0 flex-1 overflow-hidden max-w-none"
                : "pt-0 px-0 pb-0 max-w-none overflow-hidden h-[calc(100dvh_-_var(--bar-h))] md:h-auto md:flex-1")
            : cn("max-w-[1800px] mx-auto",
                isPS5 ? "pt-16 px-16 pb-12 flex-1 overflow-y-auto" : "pt-6 px-6 pb-[calc(var(--bar-h)+4rem)] md:pt-16 md:px-16 md:pb-12 md:flex-1 md:overflow-y-auto")
        )}>
          {/* [CN-FORK] v0.5.1.64 主页视图整体删除（硬件信息探测下线 + 不再保留空壳页）
              —— 上游 0.6.0 把它改造成了「收藏夹」并重新启用，但 fork 的产品决定是
              「管理 Payload」一页统管（StorageHub 里已装 / 仓库 / USB 三段），不保留
              单独的 dashboard；收藏能力后端已支持（/get_config 的 FAVORITES），
              哪天要接前端挂个 dashboardItems 视图即可。 */}
          {/* [CN-FORK] v0.6.0 合入：上游给所有视图套了 ErrorBoundary（视图渲染炸了
              不至于整页白屏，可单独关闭重试）—— 采纳，这是纯健壮性增强。 */}
          <ErrorBoundary title={t("app.error.view_error", "View Render Error")} canDismiss>

          {view === 'storage' && (
            <StorageHub
              items={items}
              self={selfUpdate}
              payloads={payloads}
              payloadMeta={payloadMeta}
              loadPayload={loadPayload}
              onInstall={handleInstall}
              onDelete={handleDelete}
              onUpload={handleUpload}
              onImportFromUsb={handleImportFromUsb}
              config={config}
              ip={ip}
              scrollTarget={storageScrollTarget}
              onClearScrollTarget={() => setStorageScrollTarget(null)}
              selectedPayload={selectedPayload}
              onSelectPayload={handleSelectPayload}
              onClosePayload={handleClosePayload}
              conflictPrompt={conflictPrompt}
              setConflictPrompt={setConflictPrompt}
              externalLinkUrl={externalLinkUrl}
              setExternalLinkUrl={setExternalLinkUrl}
              installingPayloadId={installingPayloadId}
            />
          )}

          {view === 'move_from_usb' && moveFromUsbPath && (
            <MoveFromUsbView
              path={moveFromUsbPath}
              onBack={() => navigate('storage')}
              onComplete={() => {
                refreshPayloads()
                navigate('storage')
                setMoveFromUsbPath(null)
              }}
              addToast={addToast}
            />
          )}

          {view === 'autoload' && (
            <AutoloadView
              items={items}
              payloads={payloads}
              config={config}
              onSaveConfig={handleSaveConfig}
              onToast={addToast}
              onRedirect={(v, target) => {
                if (target) setStorageScrollTarget(target)
                navigate(v)
              }}
              delayModal={autoloadDelayModal}
              setDelayModal={setAutoloadDelayModal}
            />
          )}

          {view === 'settings' && (
            <SettingsView
              config={config}
              onSaveConfig={handleSaveConfig}
              addToast={addToast}
              isPS5={isPS5}
              logs={logs}
              setLogs={setLogs}
              showLogs={showLogs}
              setShowLogs={setShowLogs}
              onNavigate={(v) => navigate(v)}
              onClearCache={clearCache}
            />
          )}

          {view === 'sources' && (
            <ManageSourcesView
              onBack={() => navigate('settings')}
              ip={ip}
              addToast={addToast}
              showConfirm={showConfirm}
            />
          )}

          {view === 'processes' && (
            <ActiveProcessesView
              ip={ip}
              addToast={addToast}
              showConfirm={showConfirm}
            />
          )}

          {/* [CN-FORK] v0.5.2.154 「关于」页：设置下面的新菜单，取代原来的「支持作者」页
              （`view === 'donate'` 那条已删 —— 内容全在 AboutView 里了）。 */}
          {view === 'about' && <AboutView version={version} />}

          {/* [CN-FORK] 常用入口 / 游戏管理 / PKG安装（内嵌 PKG Manager 界面） */}
          {view === 'common_entries' && <CommonEntriesView showConfirm={showConfirm} addToast={addToast} />}
          {view === 'game_manage' && <GameManageView addToast={addToast} showConfirm={showConfirm} config={config} />}
          {view === 'pkg' && <PkgManagerFrame />}
          </ErrorBoundary>
        </main>
      </div>

      {loading && (
        <div className="fixed inset-0 bg-ps-black/95 z-[9999] flex flex-col items-center justify-center px-6 py-10" role="status" aria-live="polite">
          <div className="w-full max-w-xl flex flex-col items-center text-center space-y-6">
            <PayloadIcon
              icon={activeLoadingPayload?.icon}
              name={activeLoadingPayload?.path?.split('/').pop() || activeLoadingPayload?.name}
              className="w-24 h-24 md:w-28 md:h-28 text-4xl shadow-lg"
            />
            <div className="w-full space-y-4">
              <PayloadName
                path={activeLoadingPayload?.path}
                displayName={activeLoadingPayload?.name || t("app.loading.title", "Launching...")}
                version={activeLoadingPayload?.version}
                hideVersion hideIcon centered
                className="text-white text-2xl md:text-4xl leading-snug"
              />
              <div className="flex flex-wrap items-center justify-center gap-2">
                {activeLoadingPayload?.version && (
                  <span className="text-sm text-zinc-400 font-bold bg-white/10 px-3 py-1 rounded-lg">
                    {activeLoadingPayload.version}
                  </span>
                )}
                {activeLoadingPayload?.sourceName && (
                  <span className="inline-flex items-center gap-2 text-sm text-zinc-400 font-medium bg-white/10 px-3 py-1 rounded-lg max-w-full" title={activeLoadingPayload.sourceName}>
                    <Globe className="w-4 h-4 shrink-0" />
                    <span className="truncate">{formatSourceTag(activeLoadingPayload.sourceName)}</span>
                  </span>
                )}
              </div>
            </div>
            <div className="ps5-robust-spinner !w-10 !h-10 !border-4" />
            <p className="text-sm text-ps-blue font-medium tracking-wide">{t("app.loading.subtitle", "LAUNCHING PAYLOAD...")}</p>
          </div>
        </div>
      )}
      {showLogs && (
        <div className="fixed inset-0 z-[9999] bg-ps-black flex flex-col animate-in fade-in duration-300">
          {/* [CN-FORK] v0.5.2.99 粘顶条底改实色 rail 档：bg-ps-black/95 留 5% 透影，
              且 backdrop-blur-xl 在 PS5 老内核 WebKit 上根本不支持（写了等于没写）。 */}
          <div className="p-6 md:p-8 border-b border-ps-border flex items-center justify-between bg-ps-surface sticky top-0 z-10">
            <div className="flex items-center space-x-4">
              <Terminal className="w-8 h-8 text-ps-blue" />
              <h3 className="text-3xl font-black text-white uppercase italic tracking-tighter">{t("app.logs.title", "Logs")}</h3>
            </div>
            <button
              onClick={() => setShowLogs(false)}
              className="p-4 rounded-2xl bg-white/5 hover:bg-red-600 hover:text-white transition-all border border-white/10 group"
            >
              <X className="w-8 h-8 transition-transform group-hover:rotate-90" />
            </button>
          </div>
          <div className="flex-1 overflow-hidden bg-black/20">
            <LogViewer logs={logs} />
          </div>
        </div>
      )}
    </div>
  )
}

export default App
