import React, { useState, useEffect, useRef } from 'react'
import {
  Settings,
  Cpu,
  Database,
  RefreshCw,
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

// Utilities
import { cn, isPS5 } from './utils/helpers'

// UI Components
import Toast from './components/ui/Toast'
import Modal from './components/ui/Modal'
import NavButton from './components/ui/NavButton'
import LogoIcon from './components/ui/LogoIcon'

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
 * 病灶：PC 侧栏与手机底部栏各自手写一遍 NavButton（改一处忘一处，v0.5.1.64 删「主页」时就是两处分别动），
 * 电视端又复用 PC 侧栏 —— 于是「有哪些页面」这件事在代码里有两份真相。
 * 现在一处定义、两处渲染：字段里的 mobile* 是窄屏用的短标签（放不下整名时才露出）。 */
const NAV_ITEMS = [
  { id: 'storage', group: 'main', icon: Database, label: ['app.nav.storage', 'Manage Payloads'], mobile: ['app.nav.manage_mobile', 'Manage'] },
  { id: 'autoload', group: 'main', icon: RefreshCw, label: ['app.nav.autoload', 'Autoload'], mobile: ['app.nav.auto_mobile', 'Auto'] },
  { id: 'pkg', group: 'main', icon: Package, label: ['app.nav.pkg', 'PKG Manager'], mobile: ['app.nav.pkg_mobile', 'PKG'] },
  { id: 'common_entries', group: 'main', icon: Link2, label: ['app.nav.common_entries', 'Quick Links'], mobile: ['app.nav.common_entries_mobile', 'Links'] },
  { id: 'game_manage', group: 'main', icon: Gamepad2, label: ['app.nav.game_manage', 'Game Manager'], mobile: ['app.nav.game_manage_mobile', 'Games'] },
  { id: 'processes', group: 'system', icon: Cpu, label: ['app.nav.processes', 'Active Processes'], mobile: ['app.nav.processes_mobile', 'Processes'] },
  { id: 'settings', group: 'system', icon: Settings, label: ['app.nav.settings', 'Settings'], mobile: ['app.nav.settings', 'Settings'] },
  /* [CN-FORK] v0.5.2.154 用户口径：「在设置下边增加一个菜单叫关于，把更新日志和支持作者的
   * 内容全部放到关于里面」。所以它紧跟在设置之后、同属「系统管理」组，并取代了原来
   * 藏在设置页里的「更新日志」入口与独立的「支持作者」页。 */
  { id: 'about', group: 'system', icon: Info, label: ['app.nav.about', 'About'], mobile: ['app.nav.about_mobile', 'About'] },
]
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

function App() {
  const { t, i18n } = useTranslation();
  const [view, setView] = useState(readStoredView) /* [CN-FORK] v0.5.1.64 默认页 payload 管理页；v0.5.2.154 起刷新后回到上次那一页 */
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
  const [version, setVersion] = useState('Loading...')
  const [loading, setLoading] = useState(false)
  const [activeLoadingName, setActiveLoadingName] = useState('')
  const [toasts, setToasts] = useState([])
  const [loadingPayloads, setLoadingPayloads] = useState(true)
  const [downloadModal, setDownloadModal] = useState({ show: false, name: '', progress: 0 })
  const [confirmModal, setConfirmModal] = useState({ show: false, title: '', message: '', onConfirm: null, danger: false })
  const [moveFromUsbPath, setMoveFromUsbPath] = useState(null)
  const [storageScrollTarget, setStorageScrollTarget] = useState(null)
  const [showLogs, setShowLogs] = useState(false)
  const [payloadMeta, setPayloadMeta] = useState({})

  useEffect(() => {
    if (!showLogs) return
    const eventSource = new EventSource('/events')
    eventSource.onmessage = (e) => {
      setLogs(prev => [...prev, e.data].slice(-200))
    }
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

  const addToast = (message, type = 'success') => {
    const id = Date.now()
    setToasts(prev => [...prev, { id, message, type }])
  }

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

  const api = async (endpoint, options = {}) => {
    try {
      const response = await fetch(endpoint, options)
      if (options.method === 'POST') return response.text()
      try {
        const text = await response.text()
        if (text.toLowerCase().includes('<!doctype')) return null
        return JSON.parse(text)
      } catch (e) { return null }
    } catch (e) { return null }
  }

  /* [CN-FORK] v0.5.2.125 必须是函数声明：函数体内有 `() => refreshPayloads(retryCount + 1)`
   * 自引用（失败重试那条路），写成 `const refreshPayloads = async () => {…}` 会被 Babel 的
   * async 降级搞坏（自引用不重写，变成悬空标识符）。详见 utils/gameApi.js 里 watchJob 的
   * tick 注释与 HANDOVER §6。 */
  async function refreshPayloads(retryCount = 0) {
    setLoadingPayloads(true)
    const data = await api('/list_payloads')
    if (data?.payloads) {
      const sorted = [...data.payloads].sort((a, b) => {
        const aIsUsb = a.startsWith('/mnt/usb')
        const bIsUsb = b.startsWith('/mnt/usb')
        if (aIsUsb && !bIsUsb) return 1
        if (!aIsUsb && bIsUsb) return -1
        return a.localeCompare(b)
      })
      setPayloads(sorted)
      // Build metadata map: filename -> meta object
      if (data.meta && typeof data.meta === 'object') {
        setPayloadMeta(data.meta)
      }
      setLoadingPayloads(false)
    } else if (retryCount < 5) {
      setTimeout(() => refreshPayloads(retryCount + 1), 1000)
    } else {
      setPayloads([])
      setLoadingPayloads(false)
    }
  }

  /* [CN-FORK] v0.5.2.96 删掉 refreshHistory / launchHistory：拉回来的启动历史没有任何界面消费
   * （消费方是已下线的仪表盘收藏区），但每次进页和每次启动都还在白发一次 /history_list。 */

  const refreshConfig = async () => {
    const data = await api('/get_config')
    if (data) setConfig(data)
  }

  const handleAbort = async () => {
    await fetch('/abort').catch(() => { })
    setAutoloadStatus(prev => prev ? { ...prev, remaining: -1 } : null)
    addToast(t("app.toasts.sequence_aborted", "Sequence Aborted"), "error")
  }

  const handleFinish = async () => {
    await fetch('/autoload_clear').catch(() => { })
    setAutoloadStatus(null)
    window.location.reload()
  }

  const loadPayload = async (path) => {
    const name = path.split('/').pop().replace(/\.(elf|bin)$/i, '').replace(/_/g, ' ')
    setLoading(true)
    setActiveLoadingName(name)
    try {
      const safePath = encodeURI(path)
      const res = await fetch(`/loadpayload:${safePath}`)
      if (!res.ok) throw new Error(`${t("app.toasts.launch_failed", "Launch failed")} (${res.status})`)
      addToast(t("app.toasts.payload_launched", "{{name}} launched", { name }))
    } catch (e) { addToast(e.message || t("app.toasts.launch_failed", "Launch failed"), "error") }
    setTimeout(() => {
      setLoading(false)
      setActiveLoadingName('')
    }, 1500)
  }

  const handleDelete = (fileName) => {
    showConfirm(
      t("app.modals.delete_payload.title", "Delete Payload"),
      t("app.modals.delete_payload.message", "Are you sure you want to remove {{fileName}}?", { fileName }),
      async () => {
        const res = await fetch(`/manage:delete?filename=${encodeURIComponent(fileName)}`)
        if (!res.ok) {
          addToast(`Delete failed (${res.status})`, 'error')
          return
        }
        refreshPayloads()
        addToast(t("app.toasts.payload_removed", "{{fileName}} removed", { fileName }))
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

    const ext = file.name.split('.').pop().toLowerCase();
    if (ext !== 'elf' && ext !== 'bin') {
      addToast(t("app.toasts.unsupported_file", "Unsupported file type. Only .elf and .bin files are allowed."), "error");
      return;
    }

    try {
      const res = await fetch(`/manage:check?filename=${encodeURIComponent(file.name)}`)
      const data = await res.json()
      if (data.file_exists || data.folder_exists) {
        setConfirmModal({
          show: true,
          title: t("app.modals.overwrite.title", "Overwrite Payload"),
          message: data.file_exists
            ? t("app.modals.overwrite.file_exists", "The file {{fileName}} already exists. Overwrite it?", { fileName: file.name })
            : t("app.modals.overwrite.folder_exists", "A different version of this payload exists in the \"{{folderName}}\" folder. Overwrite it?", { folderName: data.folder_name }),
          onConfirm: () => performUpload(file)
        })
      } else {
        performUpload(file)
      }
    } catch (err) {
      performUpload(file)
    }
  }

  const performUpload = async (file) => {
    setConfirmModal({ show: false })
    setDownloadModal({ show: true, name: file.name, progress: 20 })
    try {
      const res = await fetch(`/manage:upload?filename=${encodeURIComponent(file.name)}`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: file
      })
      if (!res.ok) throw new Error(`${t("app.toasts.upload_failed", "Upload failed")} (${res.status})`)
      setDownloadModal(prev => ({ ...prev, progress: 100 }))
      addToast(t("app.toasts.payload_uploaded", "{{fileName}} uploaded", { fileName: file.name }))
      refreshPayloads()
    } catch (e) { addToast(e.message || t("app.toasts.upload_failed", "Upload failed"), "error") }
    setTimeout(() => setDownloadModal({ show: false }), 800)
  }

  const handleInstall = async (p, sourceId, repoUrl) => {
    if (p.isUpdate || p.isInstalled) {
      setConfirmModal({
        show: true,
        title: p.isUpdate ? t("app.modals.reinstall.update_title", "Update Payload") : t("app.modals.reinstall.reinstall_title", "Reinstall Payload"),
        message: t("app.modals.reinstall.message", "A version of {{name}} is already installed. Do you want to replace it with the repository version?", { name: p.name || p.filename }),
        onConfirm: () => performInstall(p, sourceId, repoUrl)
      })
    } else {
      performInstall(p, sourceId, repoUrl)
    }
  }

  const performInstall = async (p, sourceId, repoUrl) => {
    setConfirmModal({ show: false })
    setDownloadModal({ show: true, name: p.filename, progress: 10 })
    try {
      setDownloadModal(prev => ({ ...prev, progress: 30 }))
      let url = `/repository_install?filename=${encodeURIComponent(p.filename)}`
      if (sourceId) url += `&source_id=${encodeURIComponent(sourceId)}`
      if (repoUrl) url += `&repo_url=${encodeURIComponent(repoUrl)}`
      const res = await fetch(url)
      setDownloadModal(prev => ({ ...prev, progress: 80 }))

      const data = await res.json().catch(() => null)
      if (res.ok && data?.ok) {
        setDownloadModal(prev => ({ ...prev, progress: 100 }))
        addToast(t("app.toasts.payload_installed", "{{fileName}} installed", { fileName: p.filename }))
        refreshPayloads()
      } else throw new Error(data?.message || t("app.toasts.install_failed", "Installation failed"))
    } catch (e) { addToast(e.message || t("app.toasts.install_failed", "Installation failed"), 'error') }
    setTimeout(() => setDownloadModal({ show: false }), 800)
  }

  const handleImportFromUsb = (path) => {
    setMoveFromUsbPath(path)
    setView('move_from_usb')
  }

  const handleSaveConfig = async (newConfig) => {
    const merged = { ...config, ...newConfig }
    const success = await api('/set_config', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(merged)
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
    const init = async () => {
      let offline = false
      try {
        const ipRes = await fetch('/getip').then(r => r.text())
        if (ipRes.toLowerCase().includes('<!doctype')) offline = true
        else setIp(ipRes)
      } catch (e) {
        offline = true
      }

      try {
        const verRes = await fetch('/version').then(r => r.text())
        if (verRes.toLowerCase().includes('<!doctype')) offline = true
        else setVersion(verRes)
      } catch (e) {
        offline = true
      }

      if (offline) {
        setIsOffline(true)
      } else {
        refreshPayloads()
        refreshConfig()
      }
    }
    init()
  }, [])




  useEffect(() => {
    if (view === 'autoload' || view === 'storage') {
      refreshConfig()
      refreshPayloads()
    } else if (view === 'game_manage') {
      /* [CN-FORK] v0.5.2.144 进游戏管理页也刷一次配置：单游戏存档弹窗要读全局存档配置
       * （ARCHIVE_GLOBAL_CONFIG，在设置页写）。设置页保存时会自己 refreshConfig，
       * 但若全局配置是被别的途径改的（FTP / 另一台设备），进这个页面就得重新拿一次，
       * 否则弹窗里显示的还是上一次的远程位置。只需 config，不需要 payloads。 */
      refreshConfig()
    }
  }, [view])

  useEffect(() => {
    let statusTimeout
    async function poll() {
      try {
        const res = await fetch('/autoload_status')
        if (res.ok) {
          const data = await res.json()
          setAutoloadStatus(data)

          // Poll as long as autoload is active (remaining >= 0), including DONE state
          // Only stop when remaining goes negative (after /autoload_clear is called)
          const isActive = data && data.remaining >= 0
          if (isActive) {
            const delay = data.remaining > 0 ? 600 : 500
            statusTimeout = setTimeout(poll, delay)
          }
          return
        }
      } catch (e) { }
      statusTimeout = setTimeout(poll, 5000)
    }
    poll()
    return () => clearTimeout(statusTimeout)
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
  const isAutoloadActive = autoloadStatus && autoloadStatus.remaining >= 0;

  if (isAutoloadActive) {
    return <AutoloadOverlay status={autoloadStatus} onCancel={handleAbort} onFinish={handleFinish} isPS5={isPS5} />;
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
                {NAV_ITEMS.filter((i) => i.group === g.id).map((i) => (
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
        style={{ gridTemplateColumns: `repeat(${NAV_ITEMS.length}, minmax(0, 1fr))` }}
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
        {NAV_ITEMS.map((i) => (
          <NavButton
            key={i.id}
            active={view === i.id}
            onClick={() => setView(i.id)}
            icon={i.icon}
            label={t(i.label[0], i.label[1])}
            mobileLabel={t(i.mobile[0], i.mobile[1])}
          />
        ))}
      </nav>

      {/* MAIN CONTENT AREA */}
      <div className={cn(
        "flex flex-col relative",
        isPS5 ? "h-screen flex-1 min-h-0" : "md:h-screen md:flex-1 md:min-h-0"
      )}>
        <main ref={mainRef} className={cn(
          "custom-scrollbar w-full flex flex-col",
          /* [CN-FORK] 「PKG安装」要铺满整个模块页：四边内边距全清零、解除普通页面用的
           * 1800px 居中宽度上限（超宽屏下它会让 iframe 左右留边）、改 overflow-hidden 让
           * iframe 自己接管滚动。本项目没有 fixed 覆盖式顶栏（各视图自带 h1），pkg 视图里
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
          {/* [CN-FORK] v0.5.1.64 主页视图整体删除（硬件信息探测下线 + 不再保留空壳页） */}

          {view === 'storage' && (
            <StorageHub
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
            />
          )}

          {view === 'move_from_usb' && moveFromUsbPath && (
            <MoveFromUsbView
              path={moveFromUsbPath}
              onBack={() => setView('storage')}
              onComplete={() => {
                refreshPayloads()
                setView('storage')
                setMoveFromUsbPath(null)
              }}
              addToast={addToast}
            />
          )}

          {view === 'autoload' && (
            <AutoloadView
              payloads={payloads}
              config={config}
              onSaveConfig={handleSaveConfig}
              onToast={addToast}
              onRedirect={(v, target) => {
                if (target) setStorageScrollTarget(target)
                setView(v)
              }}
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
              onNavigate={(v) => setView(v)}
            />
          )}

          {view === 'sources' && (
            <ManageSourcesView
              onBack={() => setView('settings')}
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
        </main>
      </div>

      {loading && (
        <div className="fixed inset-0 bg-ps-black/95 z-[9999] flex flex-col items-center justify-center space-y-12">
          <div className="ps5-robust-spinner" />
          <div className="text-center">
            <h4 className="text-4xl font-extrabold text-white tracking-tight mb-4 uppercase italic">{activeLoadingName || t("app.loading.title", "Launching...")}</h4>
            <p className="label-caps !text-ps-blue tracking-[0.3em] font-black">{t("app.loading.subtitle", "LAUNCHING PAYLOAD...")}</p>
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
