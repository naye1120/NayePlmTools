import React, { useState, useEffect, useRef, useCallback, useMemo } from 'react'
import { useTranslation } from 'react-i18next'
import {
  Image as ImageIcon, RefreshCw, Download, CheckSquare, Square, X,
  Play, Film, AlertTriangle, PackageOpen, Trash2, ChevronLeft, ChevronRight,
  Share2, Upload,
} from 'lucide-react'
import { QRCodeSVG } from 'qrcode.react'
import Modal from '../ui/Modal'
import Segmented from '../ui/Segmented'
import { Block, Chip, Note, MiniEmpty, ProgressBar, Stat } from './parts'
import { formatSizeAlways, formatDuration, formatRate, formatElapsed, formatStamp } from './format'
import { getMedia, mediaThumbUrl, mediaFileUrl, packMedia, mediaPackUrl, deleteMedia, watchJob, cancelJob, syncMedia } from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.112 多媒体管理弹窗（磁贴第二颗按钮）
 *
 * 用户口径：「第二个代表 多媒体管理，包括截图和视频的查看，支持多选打包下载，全量打包下载，
 * 单个预览。」
 *
 * 四件事，各自的坑：
 *   ① 截图和视频混在一个网格里，靠**角标**区分（视频有播放三角 + 时长，截图没有），
 *      不靠分区。玩家找东西是按时间找的，分区会打乱时间序。
 *   ② 多选 —— 用**每格左上角的复选框**，不用「长按」或「点一下选中/点两下预览」那类隐式交互。
 *      电视端只有方向键和确定键，隐式交互在遥控器上根本发现不了。所以：**点格子 = 预览，
 *      点角上的框 = 选中**，两个动作各有各的落点，都能用遥控器走到。
 *   ③ 打包下载 —— 后端是异步任务（与删除同一个 worker），有进度；完成后给下载入口。
 *      「全量」与「选中」走同一条后端路径，只是 ids 传 null 还是数组。
 *   ④ 单个预览 —— 全屏 lightbox。视频用原生 <video controls>，不自己造播放器
 *      （PS5 的 WebKit 原生解码器比自己用 canvas 拼的靠谱得多）。
 *
 * [CN-FORK] v0.5.2.131 打包一完成就**自动开始下载**，不再只放一颗「下载打包文件」按钮
 * 等人去找。用户点的是「打包**下载**」——按钮名里就写着下载，包打好了却不下载会让人
 * 以为功能坏了。自动下载之外那颗按钮保留作兜底：PS5 的 WebKit 对非用户手势触发的下载
 * 偶尔拦截，界面上得有个能点的东西。两处指向同一个 URL。
 *
 * 性能上的两条硬约束（参考 game_mgr.c 的教训，那里反复强调「轮询会打死内核」）：
 *   · 缩略图走 /game_media_thumb?id=&w=320 后端**按需缩放并缓存**，绝不把 4K 原图塞进网格
 *     —— 42 张 3840×2160 的 PNG 在电视端浏览器里直接能把内存打满。
 *   · 网格缩略图 loading="lazy"，只加载视口内的。
 */

const TABS = [
  { value: 'all', label: '全部' },
  { value: 'screenshot', label: '截图' },
  { value: 'video', label: '视频' },
]

/* [CN-FORK] v0.5.2.122 后端给的 phase 是机器可读的（preparing / measuring /
 * packing / done / failed / cancelled）。用户看到「正在打包…」时真正想知道的是
 * **卡在哪一步** —— 尤其这一版之前报过「一直卡在这个页面」：进度条不动的时候，
 * 「正在读取媒体库」和「正在写入压缩包」是两件完全不同的事（一个该等、一个该查），
 * 所以这一行必须显示出来，不能只留一个不动的百分比。 */
const PHASE_TEXT = {
  preparing: ['game.phase_preparing', '正在读取媒体库…'],
  measuring: ['game.phase_measuring', '正在计算体积…'],
  packing: ['game.phase_packing', '正在写入压缩包…'],
  syncing: ['game.phase_syncing', '正在传到远程位置…'],
  cancelled: ['game.phase_cancelled', '已取消'],
}
function phaseText(phase, t) {
  const e = PHASE_TEXT[phase]
  return e ? t(e[0], e[1]) : ''
}

/* 进度面板顶上那行标题。打包与同步共用这一块面板（同一个全局任务槽），
 * 所以标题必须按 kind 分：同步跑到一半写「正在打包…」是会直接把人带沟里的。 */
function jobTitle(job, kind, t) {
  if (job.lost) return t('game.job_lost_title', '与主机失联')
  if (kind === 'sync') {
    if (!job.done) return t('game.media_syncing', '正在同步到远程…')
    return job.ok ? t('game.media_sync_done', '远程同步完成') : t('game.media_sync_failed', '同步失败')
  }
  if (job.done) return job.ok ? t('game.pack_done', '打包完成') : t('game.pack_failed', '打包失败')
  return t('game.packing', '正在打包…')
}

/* [CN-FORK] v0.5.2.131 打包一完成就自动下载，不再让用户去找那颗「下载打包文件」按钮。
 *
 * 做法是动态插一个 <a>、click()、再摘掉 —— 这是没有现成可见锚点时的标准程序化下载。
 * 之所以保留界面上那颗手动按钮：程序化下载不一定总能成（PS5 的 WebKit 对非手势触发的
 * 下载偶尔拦截），按钮是兜底。两条路指向同一个 URL，后端下发的 Content-Disposition
 * 与 <a download> 同名，落盘名一致。
 *
 * 放模块作用域而不是组件体内：它只是 DOM 操作，与组件状态无关，且 startPack 的
 * watchJob 回调里要拿到它。 */
const triggerPackDownload = (jobId, name) => {
  const a = document.createElement('a')
  a.href = mediaPackUrl(jobId)
  a.download = name || true
  a.style.display = 'none'
  document.body.appendChild(a)
  a.click()
  document.body.removeChild(a)
}

/* [CN-FORK] v0.5.2.166 分享二维码要装的是**绝对地址**。
 *
 * 旧版把 window.location.href（也就是面板首页）塞进二维码 —— 用户扫出来是「打开面板」，
 * 而他要的是「扫完直接下载这一份」。改成本面板的下载端点之后，端点本身是相对路径
 * （gameApi.js 的约定：所有路径都是相对当前源），**相对路径不能进二维码**：
 * 手机扫出来会当成「手机自己这台机器的 /game_media_file…」，必然打不开。
 * 所以统一在这里补上 origin，与地址栏同源（同一台主机、同一个端口）。
 *
 * 用字符串拼而不是 `new URL(path, base)`：PS5 的 WebKit 虽然支持 URL 构造器，
 * 但这几行是给二维码用的，越少依赖越好（老内核上 URL 解析的边界行为不值得赌）。 */
const absUrl = (path) => {
  if (!path) return ''
  if (/^https?:\/\//i.test(path)) return path
  const origin = (typeof window !== 'undefined' && window.location && window.location.origin) || ''
  if (!origin) return path
  return path.charAt(0) === '/' ? origin + path : `${origin}/${path}`
}

export default function MediaManagerModal({ show, game, onClose, addToast, showConfirm }) {
  const { t } = useTranslation()
  const tid = game && game.tid
  const [data, setData] = useState(null)
  const [err, setErr] = useState('')
  const [tab, setTab] = useState('all')
  const [sel, setSel] = useState(() => new Set())
  const [preview, setPreview] = useState(null)      // 预览中的下标
  const [job, setJob] = useState(null)              // 打包 / 同步任务
  /* 当前那个任务是**哪一种**（pack | sync）。两者共用全局单槽与同一份进度字段，
   * 只有这一行字不同 —— 不带 kind 的话，同步跑到一半界面会写着「正在打包…」，
   * 用户按了取消也不知道自己取消的是什么。 */
  const [jobKind, setJobKind] = useState('pack')
  const [packed, setPacked] = useState(null)        // {job_id, count} 打包完成后可下载
  const [busyId, setBusyId] = useState('')
  /* [CN-FORK] v0.5.2.125 取帧失败的视频 id（退回后端占位图），见网格里那段注释。 */
  const [coverBad, setCoverBad] = useState(() => new Set())
  /* [CN-FORK] v0.5.2.163 分享二维码：{url, title, name?, busy?} | null。
   * [CN-FORK] v0.5.2.166 url 从 window.location.href 换成**下载直链**（见 shareItems）：
   * 扫完要能直接下载，而不是再打开一个面板。busy=true 是多份分享在等打包的那一段。
   * 挂在 state 里而不是单独一个 Modal：Modal 自身堆栈 + 多媒体这个 Modal 已经占了 1000，
   * 这里用 inline 浮层更省事（z 1100 与 Lightbox 同档）。 */
  const [sharing, setSharing] = useState(null)
  /* [CN-FORK] v0.5.2.123 「多久没收到主机的新数据」——**用本机时钟算**。
   *
   * 后端已经在 status 里给了 idle（距上次有进展多少秒），但那把尺子是主机那边的：
   * 主机的时钟一跳、或者任务槽里的 started/last_progress 落到了未来，difftime 就是
   * 负数、被钳成 0 —— 界面于是永远显示「用时 0 秒、没有任何异常提示」，正是用户报的
   * 「一直是正在计算中」。本机时钟不会跟着主机跑偏，所以这里再挂一把：
   * tickAt = 最近一次**真的收到**主机数据的时刻（transient 的那些不算），
   * 只要它不再往前走，面板就自己把话说出来。两把尺子谁先发现谁报。 */
  const [tickAt, setTickAt] = useState(0)
  const [nowMs, setNowMs] = useState(0)
  const stopJobRef = useRef(null)
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false; if (stopJobRef.current) stopJobRef.current() }
  }, [])

  const load = useCallback(async () => {
    if (!tid) return
    let d = null
    let failed = ''
    try {
      d = await getMedia(tid)
    } catch (e) {
      failed = e.message || t('game.media_read_fail', '读取截图 / 视频失败')
    }
    if (!aliveRef.current) return
    if (d) setData(d)
    if (failed) setErr(failed)
  }, [tid, t])

  /* 打开/换游戏时把筛选、选中、预览、任务面板全部归零。
   * 写在**渲染期**（键为 tid）而不是 effect 里：effect 体里同步 setState 会引发级联渲染，
   * 而「prop 变了就把这几个 state 重置」本来就属于 React 官方说的「渲染期调整 state」。 */
  const openKey = show && tid ? tid : ''
  const [loadedKey, setLoadedKey] = useState('')
  if (openKey !== loadedKey) {
    setLoadedKey(openKey)
    setData(null)
    setErr('')
    setTab('all')
    setSel(new Set())
    setPreview(null)
    setJob(null)
    setJobKind('pack')
    setPacked(null)
    setCoverBad(new Set())
    setTickAt(0)
    setNowMs(0)
  }

  useEffect(() => {
    if (!show || !tid) return
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开/换游戏时拉一次截图与视频列表
    load()
  }, [show, tid, load])

  /* 任务在跑时挂一根本机秒针：只为让「已 N 秒没有收到主机的新数据」自己往上走。
   * 不在跑就停掉 —— 空转的 1s 定时器在电视端浏览器上也是白烧。
   * 依赖抽成 jobRunning 这个布尔量：直接写 `[job && !job.done]` 会让
   * react-hooks/exhaustive-deps 报「依赖数组里有复杂表达式」。 */
  const jobRunning = !!(job && !job.done)
  useEffect(() => {
    if (!jobRunning) return undefined
    const iv = setInterval(() => setNowMs(Date.now()), 1000)
    return () => clearInterval(iv)
  }, [jobRunning])

  const items = useMemo(() => (data && data.items) || [], [data])
  const shown = useMemo(
    () => (tab === 'all' ? items : items.filter((i) => i.kind === tab)),
    [items, tab],
  )
  const shots = items.filter((i) => i.kind === 'screenshot').length
  const videos = items.filter((i) => i.kind === 'video').length
  const selCount = sel.size
  const totalBytes = items.reduce((n, i) => n + (Number(i.size) || 0), 0)

  if (!show || !game) return null

  const toggle = (id) => setSel((s) => {
    const n = new Set(s)
    if (n.has(id)) n.delete(id); else n.add(id)
    return n
  })
  const allShownSelected = shown.length > 0 && shown.every((i) => sel.has(i.id))
  const toggleAll = () => setSel((s) => {
    const n = new Set(s)
    if (allShownSelected) shown.forEach((i) => n.delete(i.id))
    else shown.forEach((i) => n.add(i.id))
    return n
  })

  /* 打包：ids 传 null = 全量。任务完成后 packed 记下 job_id，供 mediaPackUrl 用。
   *
   * [CN-FORK] v0.5.2.166 拆出 `runPack` 并返回 Promise，因为「分享」也要走这条打包路：
   * 多选/全量分享的二维码必须指向**一个已经存在的 zip**（/game_media_pack_get?job=N
   * 在包没好时回 409），所以分享按钮得先等打包完成、拿到 job_id，再把链接放进二维码。
   * 两条路的差别只有一处：autoDownload。下载按钮要顺带下载（.131 的行为，保留），
   * 分享按钮**不该**在面板上也下一份 —— 用户点的是「分享」，主机自己下一份只会让人
   * 以为点错了。 */
  const runPack = (ids, { autoDownload = true } = {}) => new Promise((resolve, reject) => {
    setPacked(null)
    packMedia(tid, ids).then((r) => {
      if (!r || !r.job_id) {
        addToast?.(t('game.pack_started', '打包已开始'), 'success')
        reject(new Error(t('game.pack_no_job', '主机没有返回任务号')))
        return
      }
      setJob({ job_id: r.job_id, phase: 'preparing', percent: 0, active: true, done: false })
      setTickAt(Date.now())
      setNowMs(Date.now())
      if (stopJobRef.current) stopJobRef.current()
      stopJobRef.current = watchJob(r.job_id, (st) => {
        if (!aliveRef.current) return
        /* 失联时 onTick 拿到的是一个只有 job_id/error 的残缺对象（见 watchJob）。
         * 直接 setJob(st) 会把已经量出来的体积/文件数抹成 `—`、进度条退回不确定态 ——
         * 恰恰在最需要「最后停在哪」的时候把证据丢掉。所以只在 transient 时做合并，
         * 正常 tick 一律整体替换。 */
        setJob((prev) => (st.transient && prev ? { ...prev, ...st } : st))
        /* 只有**真的拿到主机数据**才算「心跳」：transient 是失联重试，不算。 */
        if (!st.transient) setTickAt(Date.now())
        if (st.done) {
          if (stopJobRef.current) { stopJobRef.current(); stopJobRef.current = null }
          /* name 是后端定下的成品文件名（游戏名称-打包时间戳.zip）。存下来给
           * <a download> 用 —— 服务端也下发了同名 Content-Disposition，两边同源。 */
          if (st.ok) {
            const count = ids ? ids.length : items.length
            setPacked({ job_id: r.job_id, count, name: st.name || '' })
            /* 一完成就自动下载：不用等用户去找那颗按钮（.131）。全量与选中两条路都走
             * 到这里，所以一处改动两个按钮都生效。 */
            if (autoDownload) {
              triggerPackDownload(r.job_id, st.name || '')
              addToast?.(t('game.pack_auto_downloading', '打包完成，正在下载（{{n}} 项）', { n: count }), 'success')
            }
            resolve({ job_id: r.job_id, name: st.name || '', count })
          } else {
            if (st.error && !st.transient) addToast?.(t('game.pack_fail', '打包失败：{{why}}', { why: st.error }), 'error')
            reject(new Error(st.error || t('game.pack_fail_generic', '打包失败')))
          }
        }
      })
    }).catch((e) => {
      addToast?.(e.message || t('game.pack_fail_generic', '打包失败'), 'error')
      reject(e)
    })
  })

  const startPack = (ids) => { runPack(ids).catch(() => { /* runPack 自己已经报过了 */ }) }

  /* 远程同步（v0.6.0.177）：把本机媒体推到设置页配的 SMB/FTP 位置。
   * 与打包**共用全局单槽**（后端同一份 /game_job_status 与 /game_job_cancel），
   * 所以按钮在 packing 时同样禁用；真撞上了后端回 409 EBUSY，message 直接弹出来。
   *   ids = null（没选中任何项）→ 全量，**增量**（账本里已传过且大小没变的跳过）；
   *   ids = 选中项            → 只推这些，且一律强推（用户点名的不看账本）。
   * 进度面板复用打包那一块（kind 只改几个标题），完成后**不**下载、不记 packed ——
   * 同步的产物在远端，主机上没有可以下下来的东西。 */
  const startSync = (ids) => {
    syncMedia(tid, ids).then((r) => {
      if (!r || !r.job_id) {
        addToast?.(t('game.media_sync_no_job', '主机没有返回任务号'), 'error')
        return
      }
      setJobKind('sync')
      setJob({ job_id: r.job_id, phase: 'preparing', percent: 0, active: true, done: false })
      setTickAt(Date.now())
      setNowMs(Date.now())
      if (stopJobRef.current) stopJobRef.current()
      stopJobRef.current = watchJob(r.job_id, (st) => {
        if (!aliveRef.current) return
        /* 与 runPack 同一条合并规则：失联重试只补字段，不抹掉已知的进度。 */
        setJob((prev) => (st.transient && prev ? { ...prev, ...st } : st))
        if (!st.transient) setTickAt(Date.now())
        if (st.done) {
          if (stopJobRef.current) { stopJobRef.current(); stopJobRef.current = null }
          if (st.ok) {
            addToast?.(t('game.media_sync_done', '远程同步完成'), 'success')
          } else if (st.phase !== 'cancelled' && st.error && !st.transient) {
            /* 用户自己按的取消不算失败：面板里 phase 已经写着「已取消」，
             * 再弹一条红色 toast 会像是出了事故。 */
            addToast?.(t('game.media_sync_fail', '远程同步失败：{{why}}', { why: st.error }), 'error')
          }
        }
      })
    }).catch((e) => {
      addToast?.(e.message || t('game.media_sync_fail_generic', '远程同步失败'), 'error')
    })
  }

  /* [CN-FORK] v0.5.2.166 分享 —— 二维码里装的必须是**下载直链**。
   *
   * 用户原话：「分享的二维码内容不对，我需要扫描后直接下载分享的内容。」
   * 旧版塞的是 window.location.href（面板首页），扫出来只是又打开一个面板。
   *
   * 两种分享各自对应一条**已经存在**的下载端点：
   *   · 一份      → /game_media_file?id=&dl=1&tid=   （后端下发同名 Content-Disposition）
   *   · 多份/全量 → /game_media_pack_get?job=N       （zip；必须先打包，见 runPack）
   *
   * 单份刻意**不走打包**：为一张图去开一个全局单槽的后台任务，既慢又会跟用户
   * 同时在做的打包撞 409。一份就直接给文件直链。
   *
   * 多份要先打包，所以这里是一个 async 流程：先把分享浮层开起来显示「正在打包」，
   * 拿到 job_id 再把链接填进去 —— 用户点完立刻有反馈，不会对着没反应的按钮发懵。
   * `ids === null` 表示全量（与打包按钮同口径）。 */
  const shareItems = async (ids, title) => {
    const list = ids === null ? items : items.filter((i) => ids.includes(i.id))
    if (list.length === 0) return

    if (list.length === 1) {
      const it = list[0]
      setSharing({
        title,
        url: absUrl(mediaFileUrl(it.id, { dl: 1, tid })),
        name: it.dlname || it.name || '',
      })
      return
    }

    /* 已经有打好的包且就是这一批 → 直接用，不重打（重复打一次要几十秒，还会把
     * 上一份产物删掉）。packed 只在「刚刚成功打完」时存在，够用了。 */
    if (packed && packed.count === list.length && packed.job_id) {
      setSharing({
        title,
        url: absUrl(mediaPackUrl(packed.job_id)),
        name: packed.name || '',
      })
      return
    }

    setSharing({ title, busy: true, url: '', name: '' })
    try {
      const r = await runPack(list.map((i) => i.id), { autoDownload: false })
      if (!aliveRef.current) return
      setSharing({ title, url: absUrl(mediaPackUrl(r.job_id)), name: r.name || '' })
    } catch (e) {
      if (!aliveRef.current) return
      setSharing(null)
      addToast?.(e.message || t('game.share_pack_fail', '打包失败，无法分享'), 'error')
    }
  }

  const onDeleteOne = (it) => {
    if (!showConfirm) return
    showConfirm(
      t('game.media_delete_title', '删除媒体文件'),
      t('game.media_delete_confirm', '删除「{{name}}」？此操作无法撤销。', { name: it.name || it.id }),
      async () => {
        setBusyId(it.id)
        try {
          await deleteMedia(tid, it.id)
          addToast?.(t('game.media_deleted', '已删除'))
          if (preview !== null) setPreview(null)
          await load()
        } catch (e) {
          addToast?.(e.message || t('game.media_delete_fail', '删除失败'), 'error')
        }
        if (aliveRef.current) setBusyId('')
      },
      true,
    )
  }

  const packing = jobRunning
  /* 距上次真的收到主机数据多少秒（本机时钟）。tickAt/nowMs 都是 0 时算 0 ——
   * 任务还没起来，不该报「没收到数据」。 */
  const staleSec = tickAt && nowMs > tickAt ? Math.floor((nowMs - tickAt) / 1000) : 0

  /* [CN-FORK] v0.5.2.112 面板形态（用户：「多媒体管理也是铺满右边」）。
   * 这一块最吃宽度：缩略图网格是 `minmax(112px,1fr)` 自适应列，面板一铺满，
   * 一屏就能看到整排截图，挑图不用再翻页。 */
  return (
    <>
      <Modal
        show={show}
        title={t('game.media_title', '多媒体管理')}
        onClose={onClose}
        layout="panel"
        footer={
          <div className="flex flex-wrap items-center gap-2 w-full">
            <button
              type="button"
              className="btn btn-primary"
              disabled={selCount === 0 || packing}
              onClick={() => startPack(Array.from(sel))}
            >
              <Download className="w-4 h-4" />
              <span>{t('game.pack_selected', '打包下载选中（{{n}}）', { n: selCount })}</span>
            </button>
            {/* [CN-FORK] v0.5.2.166 分享：二维码里是**下载直链**，扫码即下载。
                一份 = 文件直链；多份 = 先打包再给 zip 直链（shareItems 里分派）。 */}
            <button
              type="button"
              className="btn btn-outline"
              disabled={selCount === 0 || packing}
              onClick={() => shareItems(
                Array.from(sel),
                t('game.share_selected_title', '分享选中（{{n}}）', { n: selCount }),
              )}
            >
              <Share2 className="w-4 h-4" />
              <span>{t('game.share_selected', '分享选中')}</span>
            </button>
            <button
              type="button"
              className="btn btn-outline"
              disabled={items.length === 0 || packing}
              onClick={() => startPack(null)}
            >
              <PackageOpen className="w-4 h-4" />
              <span>{t('game.pack_all', '全量打包下载')}</span>
            </button>
            {/* 全量分享：二维码指向**全量 zip 的下载直链**（先打包再出码）。 */}
            <button
              type="button"
              className="btn btn-outline"
              disabled={items.length === 0 || packing}
              onClick={() => shareItems(
                null,
                t('game.share_all_title', '分享全部 ({{n}})', { n: items.length }),
              )}
            >
              <Share2 className="w-4 h-4" />
              <span>{t('game.share_all', '分享全部')}</span>
            </button>
            {/* [CN-FORK] v0.6.0.177 远程同步：**一颗按钮两件事** —— 有选中就只推选中的
                （后端一律强推，不看账本），没选就推全部（增量：已传过的自动跳过）。
                不拆成两颗是因为「同步」在用户心里就是一件事，拆开只会让人对着两颗
                按钮先想「我该点哪个」；而且两颗按钮的禁用条件一模一样。 */}
            <button
              type="button"
              className="btn btn-outline"
              disabled={items.length === 0 || packing}
              onClick={() => startSync(selCount > 0 ? Array.from(sel) : null)}
            >
              <Upload className="w-4 h-4" />
              <span>
                {selCount > 0
                  ? t('game.media_sync_selected', '远程同步选中（{{n}}）', { n: selCount })
                  : t('game.media_sync_all', '远程同步全部')}
              </span>
            </button>
          </div>
        }
      >
        <div className="panel-body leading-relaxed font-normal space-y-4">
          <p className="text-zinc-500 panel-note">{game.name || tid}</p>

          {err && <Note tone="error" icon={AlertTriangle}>{err}</Note>}

          {!data && !err && (
            <div className="flex items-center gap-3 py-10 justify-center text-zinc-500">
              <RefreshCw className="w-5 h-5 animate-spin" />
              <span>{t('game.media_loading', '正在读取截图 / 视频…')}</span>
            </div>
          )}

          {data && (
            <Block
              title={t('game.media_list', '截图 / 视频')}
              icon={ImageIcon}
              actions={
                <>
                  <Chip tone="muted">{t('game.n_shots', '截图 {{n}}', { n: shots })}</Chip>
                  <Chip tone="muted">{t('game.n_videos', '视频 {{n}}', { n: videos })}</Chip>
                  <button type="button" className="icon-btn" title={t('common.refresh', '刷新')} aria-label={t('common.refresh', '刷新')} onClick={load}>
                    <RefreshCw className="w-4 h-4" />
                  </button>
                </>
              }
            >
              {/* 工具条：筛选 + 全选。放在内容上方而不是 footer，
                  因为「全选」的作用域是当前筛选结果，离网格越近越不容易误解。 */}
              <div className="flex flex-wrap items-center gap-2 mb-3">
                <Segmented
                  options={TABS.map((x) => ({ ...x, label: t(`game.tab_${x.value}`, x.label) }))}
                  value={tab}
                  onChange={setTab}
                  label={t('game.media_filter', '筛选')}
                />
                <button type="button" className="btn btn-ghost" disabled={shown.length === 0} onClick={toggleAll}>
                  {allShownSelected ? <CheckSquare className="w-4 h-4" /> : <Square className="w-4 h-4" />}
                  <span>{allShownSelected ? t('game.deselect_all', '取消全选') : t('game.select_all', '全选')}</span>
                </button>
                {selCount > 0 && (
                  <span className="panel-note text-zinc-400 tabular-nums">
                    {t('game.selected_n', '已选 {{n}} 项', { n: selCount })}
                  </span>
                )}
                {totalBytes > 0 && (
                  <span className="panel-note text-zinc-500 tabular-nums ml-auto">
                    {t('game.media_total', '共 {{n}} 项 · {{size}}', { n: items.length, size: formatSizeAlways(totalBytes) })}
                  </span>
                )}
              </div>

              {/* 打包进度 */}
              {job && (
                <div className="mb-3 p-3 rounded-xl border border-white/10 bg-white/[0.03]">
                  <div className="flex items-center justify-between gap-3 mb-2">
                    <div className="flex items-baseline gap-2 min-w-0">
                      <strong className="text-white panel-note shrink-0">
                        {jobTitle(job, jobKind, t)}
                      </strong>
                      {!job.done && phaseText(job.phase, t) && (
                        <span className="panel-tiny text-zinc-500 truncate">{phaseText(job.phase, t)}</span>
                      )}
                    </div>
                    <span className="text-zinc-400 tabular-nums panel-note shrink-0">
                      {job.done ? '' : `${Math.max(0, Math.min(100, Math.round(job.percent || 0)))}%`}
                    </span>
                  </div>
                  {/* indeterminate 只在**还不知道总量**时用（preparing 那一段）。
                   * 原版把它挂在 phase 上，于是总量 58.7 MB 已经知道了、进度条还在
                   * 来回跑一条 1/3 宽的脉冲 —— 截图里「进度条三成满、右边写着 0%」
                   * 就是这一处造成的：一条不反映真实进度的动画比一条不动的更误导。 */}
                  {!job.done && (
                    <ProgressBar percent={job.percent} indeterminate={!job.bytes_total} className="mb-2" />
                  )}
                  {!job.done && (
                    <div className="grid grid-cols-2 sm:grid-cols-4 gap-x-4 gap-y-1.5">
                      <Stat label={t('game.job_bytes', '体积')} value={job.bytes_total ? `${formatSizeAlways(job.bytes_done)} / ${formatSizeAlways(job.bytes_total)}` : '—'} />
                      <Stat label={t('game.job_files', '文件')} value={job.files_total ? `${job.files_done || 0} / ${job.files_total}` : '—'} />
                      <Stat label={t('game.job_rate', '速率')} value={formatRate(job.rate)} />
                      <Stat label={t('game.job_elapsed', '用时')} value={formatElapsed(job.elapsed)} />
                    </div>
                  )}
                  {/* 卡住与否交给**时间**说话。两把尺子：
                   *  ① 后端的 idle —— 距上次有进展多少秒（正常路径，最准）；
                   *  ② 本机的 staleSec —— 距上次**真的收到主机数据**多少秒。
                   * ② 是给 ① 失效时兜底的：主机的时钟跳一下、或槽里的时间戳落到未来，
                   * difftime 被钳成 0，idle 永远是 0，界面上就永远只有「正在计算中」。
                   * 两把尺子都不走，才说明真的没在动。 */}
                  {!job.done && !job.transient && job.idle >= 20 && (
                    <p className="mt-2 text-amber-400 panel-note break-words">
                      {t('game.job_stalled', '已 {{n}} 秒没有进展；若长时间不动，可取消任务后重试', { n: job.idle })}
                    </p>
                  )}
                  {!job.done && !job.transient && job.idle < 20 && staleSec >= 15 && (
                    <p className="mt-2 text-amber-400 panel-note break-words">
                      {t('game.job_stale', '已 {{n}} 秒没有收到主机的新数据；若长时间不动，可取消任务后重试', { n: staleSec })}
                    </p>
                  )}
                  {job.error && !job.transient && <p className="mt-2 text-red-400 panel-note break-words">{job.error}</p>}
                  {job.transient && (
                    <p className="mt-2 text-amber-400 panel-note break-words">
                      {job.lost
                        ? t('game.job_lost', '与主机失联，任务状态未知；请关掉面板重新打开查看')
                        : t('game.job_retry', '与主机失联，正在重试…')}
                    </p>
                  )}
                  {packed && (
                    <a
                      className="btn btn-ok mt-3"
                      href={mediaPackUrl(packed.job_id)}
                      download={packed.name || true}
                    >
                      <Download className="w-4 h-4" />
                      <span>{t('game.pack_get', '下载打包文件（{{n}} 项）', { n: packed.count })}</span>
                    </a>
                  )}
                  {!job.done && (
                    <button
                      type="button"
                      className="btn btn-ghost mt-3"
                      onClick={async () => {
                        try { await cancelJob(job.job_id); addToast?.(t('game.job_cancel_sent', '已请求取消')) }
                        catch (e) { addToast?.(e.message, 'error') }
                      }}
                    >
                      {t('game.job_cancel', '取消任务')}
                    </button>
                  )}
                </div>
              )}

              {shown.length === 0 ? (
                <MiniEmpty icon={ImageIcon}>
                  {tab === 'video'
                    ? t('game.no_videos', '这个游戏还没有录屏 / 视频')
                    : tab === 'screenshot'
                      ? t('game.no_shots', '这个游戏还没有截图')
                      : t('game.no_media', '这个游戏还没有截图或视频')}
                </MiniEmpty>
              ) : (
                /* 缩略图网格：列宽下限走 --thumb-w 令牌（手机 140 / 桌面 200 / 电视 320px）。
                 * 原来是写死的 112px —— 16:9 里 112px 只有 63px 高，用户在面板铺满右侧之后
                 * 反馈「缩略图也要调大，太小了，预览都看不清」。列宽下限一放大，
                 * 每个格子能看清的内容就完全不同了（320px 宽 = 原本的 9 倍面积）。
                 * 用内联 style 而不是 Tailwind 的 `grid-cols-[repeat(auto-fill,minmax(var(--thumb-w),1fr))]`：
                 * 与 Modal 的面板定位同一条理由 —— var() 写在任意值里要 JIT 扫到才生成，
                 * 漏生成就是「网格退回单列」这种一眼可见的错，而这里本来就只有一条声明。 */
                <div className="grid gap-2.5" style={{ gridTemplateColumns: 'repeat(auto-fill, minmax(var(--thumb-w), 1fr))' }}>
                  {shown.map((it) => {
                    const idx = shown.indexOf(it)
                    const checked = sel.has(it.id)
                    const isVideo = it.kind === 'video'
                    return (
                      <div key={it.id} className="relative">
                        {/* 点格子 = 预览 */}
                        <button
                          type="button"
                          onClick={() => setPreview(idx)}
                          title={it.name}
                          className="group block w-full aspect-video rounded-lg overflow-hidden bg-white/5 border border-white/10 hover:border-white/30 transition-colors relative"
                        >
                          {/* [CN-FORK] v0.5.2.125 视频封面直接取 webm 的帧。
                           *
                           * 真机取证（192.168.1.214）：PS5 给录屏生成的封面是
                           * `/user/av_contents/thumbnails/video/<生成方>/<TID>/<hex>/<名>.webm.jxr`
                           * —— **JPEG XR**（文件头 49 49 BC 01，1920×1080 24bpp，DCT 编码，
                           * 约 75:1）。载荷里只有 stb_image，解不了 jxr，后端只能退占位图；
                           * 照片那条路没事，因为照片封面是 `.png.jpeg`（普通 JPEG）。
                           * 与其在载荷里塞一个 JXR 解码器，不如让浏览器自己从 webm 取一帧 ——
                           * 用户要的本来就是「视频里面某一帧画面」。
                           *
                           * 用 preload="metadata" 直接显示首帧。**刻意不 seek**：载荷的文件直出没有
                           * Range 支持，Chrome 会把该媒体判成「不可定位」（实测 currentTime 设
                           * 不动、seekable 为空），写 seek 只是死代码；首帧本身就是视频里的一帧。
                           * 真机实测（ASTRO BOT 的 43MB webm）：readyState=4、3840×2160，
                           * 画到 canvas 取像素 avg=57 / max=242 —— 是有画面的，不是黑帧。
                           * 取不到（浏览器不支持该编码 / 文件读不出来）就退回后端的占位图。 */}
                          {isVideo && !coverBad.has(it.id) ? (
                            <video
                              src={mediaFileUrl(it.id)}
                              preload="metadata"
                              muted
                              playsInline
                              className="w-full h-full object-cover"
                              onError={() => setCoverBad((s) => new Set(s).add(it.id))}
                            />
                          ) : (
                            <img
                              src={mediaThumbUrl(it.id, 320)}
                              alt=""
                              loading="lazy"
                              className="w-full h-full object-cover"
                              onError={(e) => { e.currentTarget.style.visibility = 'hidden' }}
                            />
                          )}
                          {isVideo && (
                            <>
                              <span className="absolute inset-0 flex items-center justify-center">
                                {/* 播放钮跟着缩略图一起放大：格子从 112 长到 320 之后，
                                    原来那颗 36px 的钮在 320px 的格子里只占 11%，看着像粒灰尘。 */}
                                <span className="w-[2.75rem] h-[2.75rem] rounded-full bg-black/60 border border-white/25 flex items-center justify-center">
                                  <Play className="w-5 h-5 text-white translate-x-[1px]" />
                                </span>
                              </span>
                              {it.duration ? (
                                <span className="absolute bottom-1 right-1 panel-micro font-semibold text-white bg-black/70 rounded px-1 py-px tabular-nums">
                                  {formatDuration(it.duration)}
                                </span>
                              ) : null}
                            </>
                          )}
                        </button>
                        {/* 点角上的框 = 选中。两个落点分开，遥控器都走得到。
                            框的尺寸走 --thumb-check（桌面上比原来大，电视端更大）—— */}
                        <button
                          type="button"
                          role="checkbox"
                          aria-checked={checked}
                          aria-label={t('game.select_item', '选择 {{name}}', { name: it.name || it.id })}
                          onClick={() => toggle(it.id)}
                          style={{ width: 'var(--thumb-check)', height: 'var(--thumb-check)' }}
                          className="absolute top-1 left-1 rounded-md bg-black/65 border border-white/25 flex items-center justify-center hover:bg-black/85 transition-colors"
                        >
                          {checked
                            ? <CheckSquare className="w-4 h-4 text-ps-blue" />
                            : <Square className="w-4 h-4 text-white/70" />}
                        </button>
                        <p className="panel-tiny text-zinc-500 truncate mt-1 px-0.5">{formatStamp(it.mtime) || it.name}</p>
                      </div>
                    )
                  })}
                </div>
              )}

              <p className="panel-tiny text-zinc-500 mt-3 flex items-start gap-1.5">
                <Film className="w-3.5 h-3.5 shrink-0 mt-0.5" />
                <span>{t('game.media_hint', '点缩略图预览；点左上角的方框加入打包列表。打包在主机上后台进行，可以关掉这个窗口。')}</span>
              </p>
            </Block>
          )}
        </div>
      </Modal>

      {/* ── 单个预览（lightbox）──
          挂在 Modal 之外：它是 fixed 全屏层，套在弹窗内部会被弹窗的 overflow 裁掉。
          z-[1100] 压过 Modal 的 z-[1000]。 */}
      {preview !== null && shown[preview] && (
        <Lightbox
          items={shown}
          index={preview}
          onIndex={setPreview}
          onClose={() => setPreview(null)}
          onDelete={onDeleteOne}
          onShare={(it) => shareItems([it.id], it.name || it.id)}
          busyId={busyId}
          tid={tid}
          t={t}
        />
      )}

      {/* ── 分享二维码浮层 ──
          z-[1100] 同 Lightbox 档：用户切到 lightbox 又点「分享」也能浮上来。
          [CN-FORK] v0.5.2.166 二维码里装的是**下载直链**（单份 → /game_media_file?id&dl=1，
          多份/全量 → /game_media_pack_get?job=N），扫完直接下载，不再只是打开面板。
          多份那条要先打包，所以中间有 busy 那一段（浮层先开、打包完再填码）。 */}
      {sharing && (
        <ShareQrModal
          url={sharing.url}
          title={sharing.title}
          name={sharing.name}
          busy={sharing.busy}
          onClose={() => setSharing(null)}
          t={t}
        />
      )}
    </>
  )
}

/* 分享二维码弹层 —— 不是 Modal 组件是因为它要叠在 Lightbox 之上（同一档 z），
 * 而通用 Modal 那一层栈对额外嵌一层有点重。inline div + close-on-backdrop 够用。
 *
 * [CN-FORK] v0.5.2.166 两种形态：
 *   busy=true  —— 多份分享正在打包，码还没法生成（要等 job_id）。这一段显示转圈与
 *                 「正在打包…」；没有它的话用户点完「分享选中（12）」会对着一个
 *                 没有二维码的空浮层发呆，而打包可能要几十秒。
 *   正常       —— 二维码 + 直链 + 落盘文件名。 */
function ShareQrModal({ url, title, name, busy, onClose, t }) {
  useEffect(() => {
    const onKey = (e) => { if (e.key === 'Escape') { e.stopPropagation(); onClose() } }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [onClose])
  return (
    <div
      className="fixed inset-0 z-[1100] bg-black/85 flex items-center justify-center p-4"
      role="dialog" aria-modal="true"
      aria-label={t('game.share_title', '分享')}
      onClick={onClose}
    >
      <div
        className="bg-ps-surface rounded-2xl border border-white/10 p-6 max-w-md w-full space-y-4 shadow-2xl"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="flex items-start justify-between gap-3">
          <div className="min-w-0">
            <p className="text-[11px] uppercase tracking-wider text-ps-blue font-bold">
              {t('game.share_title', '分享')}
            </p>
            <p className="text-white font-bold truncate text-base mt-0.5" title={title}>{title}</p>
          </div>
          <button
            type="button"
            className="icon-btn shrink-0"
            onClick={onClose}
            title={t('app.buttons.close', '关闭')}
            aria-label={t('app.buttons.close', '关闭')}
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {busy ? (
          <div className="flex flex-col items-center justify-center gap-3 py-12 text-zinc-400">
            <RefreshCw className="w-6 h-6 animate-spin text-ps-blue" />
            <span className="panel-note">{t('game.share_preparing', '正在打包，请稍候…')}</span>
            <span className="panel-tiny text-zinc-500 text-center leading-relaxed">
              {t('game.share_preparing_note', '多份内容要先打成一个压缩包，打包完成后这里会显示二维码。')}
            </span>
          </div>
        ) : (
          <>
            <div className="flex justify-center bg-white p-4 rounded-xl">
              <QRCodeSVG value={url} size={220} level="H" />
            </div>
            <div className="space-y-1.5">
              <p className="text-[11px] uppercase tracking-wider text-zinc-500 font-bold">
                {t('game.share_url_label', '下载链接')}
              </p>
              <p className="text-xs text-zinc-400 break-all font-mono leading-relaxed bg-black/30 rounded-lg p-2 select-all">
                {url}
              </p>
              {name ? (
                <p className="panel-tiny text-zinc-500 truncate" title={name}>
                  {t('game.share_filename', '文件：{{name}}', { name })}
                </p>
              ) : null}
            </div>
            <p className="text-[11px] text-zinc-500 leading-relaxed">
              {t('game.share_hint', '手机扫码即可直接下载这份内容（手机需与主机在同一局域网）。')}
            </p>
          </>
        )}
      </div>
    </div>
  )
}

/* 预览层：图片直接铺满，视频用原生 <video controls>（自带播放/进度/全屏）。
 * 左右方向键切换上一张/下一张 —— 遥控器上这是最自然的翻页方式。 */
function Lightbox({ items, index, onIndex, onClose, onDelete, onShare, busyId, tid, t }) {
  const it = items[index]
  const isVideo = it.kind === 'video'

  useEffect(() => {
    const onKey = (e) => {
      if (e.key === 'Escape') {
        /* 删除媒体会在这层之上再弹一个 App 级确认框（z-1900）。capture 阶段先跑，
         * 不判一下就会「确认框还开着，预览先被 Esc 关掉」，那一下点击也就落空了。
         * 但「还有别的 dialog」不能包含**父 Modal**自己——预览嵌在 Modal 里，
         * 如果因为有父 Modal 就放弃，Esc 会关掉父 Modal 而不是预览。
         * 数法：预览自己 + 父弹窗 = 2 个；再多一个（确认框）= 3 个。 */
        if (document.querySelectorAll('[role="dialog"]').length > 2) return
        e.stopPropagation()
        onClose()
      }
      else if (e.key === 'ArrowLeft' && index > 0) onIndex(index - 1)
      else if (e.key === 'ArrowRight' && index < items.length - 1) onIndex(index + 1)
    }
    /* capture 阶段拦：不让方向键冒泡到空间导航，否则按左键会去挪焦点而不是翻页 */
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [index, items.length, onIndex, onClose])

  return (
    /* 这个浮层也是 role=dialog，但没有可访问名的话读屏只会念「对话框」，
     * 而且它和外层 Modal 同为 dialog，自动化测试没法只挑出它。补一个 aria-label 一并解决。 */
    <div className="fixed inset-0 z-[1100] bg-black/95 flex flex-col" role="dialog" aria-modal="true"
      aria-label={t('game.media_preview', '预览')}>
      <div className="flex items-center justify-between gap-3 px-4 py-3 shrink-0">
        <div className="min-w-0">
          <p className="text-white panel-body font-semibold truncate">{it.name || it.id}</p>
          <p className="text-zinc-500 panel-tiny tabular-nums">
            {index + 1} / {items.length}
            {formatStamp(it.mtime) ? ` · ${formatStamp(it.mtime)}` : ''}
            {it.w && it.h ? ` · ${it.w}×${it.h}` : ''}
            {it.size ? ` · ${formatSizeAlways(it.size)}` : ''}
          </p>
        </div>
        <div className="flex items-center gap-1.5 shrink-0">
          <button
            type="button"
            className="icon-btn is-danger"
            disabled={busyId === it.id}
            title={t('game.media_delete', '删除')}
            aria-label={t('game.media_delete', '删除')}
            onClick={() => onDelete(it)}
          >
            <Trash2 className="w-4 h-4" />
          </button>
          {/* 下载：落盘名走后端那份 dlname（游戏名称-截图时间.<ext>）。
              dl=1 让后端再下一遍同名 Content-Disposition —— 旧 WebKit 不一定认
              download 属性，两条一起给才稳。 */}
          <a
            className="icon-btn"
            href={mediaFileUrl(it.id, { dl: 1, tid })}
            download={it.dlname || true}
            title={t('game.media_download', '下载')}
            aria-label={t('game.media_download', '下载')}
          >
            <Download className="w-4 h-4" />
          </a>
          {/* [CN-FORK] v0.5.2.166 单文件分享：二维码指向**这一份的下载直链**
              （/game_media_file?id&dl=1&tid），扫完直接下载，不再是面板首页。
              URL 由父层的 shareItems 统一拼（要补 origin，见 absUrl）。 */}
          <button
            type="button"
            className="icon-btn"
            title={t('game.media_share', '分享')}
            aria-label={t('game.media_share', '分享')}
            onClick={() => onShare?.(it)}
          >
            <Share2 className="w-4 h-4" />
          </button>
          <button type="button" className="icon-btn" onClick={onClose} title={t('app.buttons.close', '关闭')} aria-label={t('app.buttons.close', '关闭')}>
            <X className="w-4 h-4" />
          </button>
        </div>
      </div>

      <div className="flex-1 min-h-0 flex items-center justify-center px-4 pb-4 relative">
        {index > 0 && (
          <button
            type="button"
            className="absolute left-3 top-1/2 -translate-y-1/2 icon-btn bg-black/50 border border-white/20"
            onClick={() => onIndex(index - 1)}
            title={t('game.prev', '上一张')}
            aria-label={t('game.prev', '上一张')}
          >
            <ChevronLeft className="w-5 h-5" />
          </button>
        )}
        {isVideo ? (
          /* 游戏录像没有字幕轨，media-has-caption 在这里不适用；而且本仓库根本没装
             eslint-plugin-react-a11y，写 disable 注释反而会报「Definition for rule not found」。 */
          <video
            key={it.id}
            src={mediaFileUrl(it.id)}
            controls
            autoPlay
            playsInline
            className="max-w-full max-h-full rounded-lg bg-black"
          />
        ) : (
          <img
            key={it.id}
            src={mediaFileUrl(it.id)}
            alt={it.name || ''}
            className="max-w-full max-h-full object-contain rounded-lg"
          />
        )}
        {index < items.length - 1 && (
          <button
            type="button"
            className="absolute right-3 top-1/2 -translate-y-1/2 icon-btn bg-black/50 border border-white/20"
            onClick={() => onIndex(index + 1)}
            title={t('game.next', '下一张')}
            aria-label={t('game.next', '下一张')}
          >
            <ChevronRight className="w-5 h-5" />
          </button>
        )}
      </div>
    </div>
  )
}
