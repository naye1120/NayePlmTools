import React, { useState, useEffect, useRef, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import {
  Save, RefreshCw, Download, Upload, Trash2, AlertTriangle, HardDrive, FileArchive,
  History, RotateCcw, CloudDownload, CloudUpload, Activity, PackagePlus,
} from 'lucide-react'
import Modal from '../ui/Modal'
import Segmented from '../ui/Segmented'
import { Block, Chip, Note, MiniEmpty, ProgressBar, GridRow } from './parts'
import { formatSizeAlways, formatStamp } from './format'
import {
  getSaves, saveDownloadUrl, uploadSave, deleteSave, snapAction, getSnapEvents,
  syncSaves, getRemoteSaveInfo, deleteRemoteSave, importGarlic,
  snapDownloadUrl, remoteSaveDownloadUrl,
} from '../../utils/gameApi'
import {
  resolveAutoForGame, resolveRemoteForGame, remoteSummary,
} from '../../utils/saveConfig'

/* [CN-FORK] v0.5.2.156 从 garlic 导出的文件名里认游戏（与后端 gs_tid_from_name() 同一条规则）。
 *
 * 用户口径：「存档导入的时候要有游戏判定，不是当前游戏的不让导入」。
 * garlic（earthonion/garlic-savemgr）的存档目录名是 `<TID>_<存档目录名>`，
 * 导出的镜像名也带着这一段（`PPSA27836_savedata01_enc`），所以**文件名里就有 TID**
 * —— 这是唯一能用的判据：镜像本身是加密的，内容里读不出游戏身份。
 *
 * 认不出来返回 ''：那只是「说不清」（用户可能改过名），不是「不匹配」，调用方要放行，
 * 否则正常导入会被整个挡住。 */
const tidFromName = (name) => {
  const m = /(^|[^A-Za-z0-9])([A-Za-z]{4}\d{5})([^A-Za-z0-9]|$)/.exec(String(name || ''))
  return m ? m[2].toUpperCase() : ''
}

/* [CN-FORK] v0.5.2.112 存档管理弹窗（磁贴第一颗按钮）
 *
 * 用户口径（`.145` 收束后的最终形态）：
 *   「把配置功能迁移过去，配置一个全局生效。单个游戏包开启 拉取和推送按钮。」
 * 所以本弹窗**不再有可编辑的配置**，只剩三件事：
 *   ① 这个游戏**实际生效**的自动存档状态（开关 / 两个时机）—— 只读，来自设置页「存档配置」；
 *   ② 远程位置只读摘要 + 逐用户远程信息（`.147` 并进本卡，原来是并排的第三张卡）
 *      + 「立即拉取 / 立即推送」+ 引擎上次干了什么（auto_state）；
 *   ③ 存档列表（下载 / 上传覆盖 / 删除 / 上传新的）
 *     与本地快照列表（立即备份 / 还原 / **下载** / 删除）。
 *   ④ **兼容导入（Garlic）单独一块满宽铺开**（`.173` 从 ③ 里搬出来的，见下）。
 *
 * [CN-FORK] v0.5.2.172 **三张列表的每一行都有一颗「下载」**（用户口径：「所有存档列表
 * 删除按钮前边加一个下载按钮」）。三颗按钮下的是**同一层东西的不同一份**：
 *   存档列表 → 主机上**当前**那份   /game_save_download
 *   远程存档 → **远端**那份备份     /game_save_remote_download
 *   本地快照 → **某个时刻**的那份   /game_save_snap_download
 * 三颗都排在「删除」前面。⚠ 远程那颗与左边的「立即拉取」不是一回事：拉取会把主机上
 * 这份**整份替换**掉，下载只把云上那份拿到电脑上（后端走 sr_fetch，一个字节都不碰主机）。
 *
 * 版式（`.147`）：第一行三栏 = ① / ② / ③，各占 1/3；兼容导入（④）与本地快照（⑤）
 * 各自满宽单独一行。
 * [CN-FORK] v0.5.2.173 为什么把 ④ 从 ③ 里搬出来：用户口径「这个调整布局，单独放一行」
 * —— ④ 原来挂在「存档列表」那张卡里，而那张卡只占屏幕的 1/3。目标用户选择器（两颗
 * 分段按钮）+ 导入按钮 + 三行长提示挤在 1/3 宽里，提示被折成 6~7 行，用户看到的
 * 截图就是那一团。搬出来之后宽屏下选择器与按钮并排一行、提示满宽一行。
 * 同时删掉了每行右侧那栏「存档转换」（用户口径：「存档转换的标签和功能删除」）——
 * 它把存档行挤成两栏，而它想解决的事（把别人的存档弄到自己名下）**兼容导入本来就能做**
 * 且做得更干净（见下）。
 * 配置（远程位置 / 自动存档 / 快照参数）全部在「设置 → 存档配置」一处，对所有游戏生效。
 *
 * 为什么把「配置」和「动作」这样分：配置是**跨游戏共享的一份**（同一台 NAS、同一个节奏），
 * 每个游戏各填一遍纯属重复劳动；而推 / 拉 / 备份、以及两串列表天生就是**某个游戏的**。
 * 一个控件只有一个家 —— 用户改了配置不会怀疑「是这个游戏的还是全局的」。
 *
 * 有效值来自 resolveAutoForGame(config, data.auto)：全局写过就用全局，没写过才回落到
 * 这个游戏自己存的那份（老装机行为不变）。后端 game_auto.c / game_saves.c 读同一份全局
 * 配置，所以这里显示什么引擎就按什么跑，不存在「UI 说开着但没跑」。
 *
 * 历史包袱：后端永远不回传密码明文（per-game 路径），全局配置那份原样存（见
 * ArchiveConfigModal 注释）；本条只在「回退到本游戏已存」时相关，密码一律按「不修改」处理。
 *
 * ── [CN-FORK] v0.5.2.154 三处 ──────────────────────────────────────────────
 * ① **游戏运行中置灰「立即拉取 / 立即推送」**。后端 game_auto_sync_action 已经会拒拦并回
 *    人话，但让用户白点一次才被告知是坏体验；更实际的理由是推拉都是**整份替换**存档目录，
 *    而游戏此刻正把存档读在内存里、退出时还要写回去 —— 这时候动它最容易把进度搞丢。
 *    判据用 st.running（后端当场探测，不是守护线程的滞后值），与那颗「游戏运行中」芯片同源。
 * ② **garlic 兼容导入**（earthonion/garlic-savemgr）。与「上传存档」是**两回事**：
 *    上传 = 整份替换（把整个 <TID> 目录换掉）；导入 = **逐文件合并**（只覆盖 garlic 导出的
 *    那几个镜像）。一个存档目录里除了当前镜像还有主机自己的记账（`sdimg_sce_bu_<名>` 备份、
 *    `sdimg_sce_sdmemory` 系统存档），整份替换会把它们一起抹掉，主机很可能就认不出这份存档了
 *    —— 所以从 garlic 搬过来的东西**不能**走上传那条路。单独一块、不去挤标题栏。
 * ③ **快照只读行删掉「间隔（分钟）」**：定时快照已废，触发时机改成了「游戏退出时」，
 *    留一个恒定的数字只会让人以为还能调（参数在设置页「存档配置」里也只剩开关 + 保留份数）。
 *
 * ── [CN-FORK] v0.5.2.173 兼容导入 = 「改签」的唯一入口 ─────────────────────
 * 用户口径：「导入的时候如果是其他用户的存档，直接改签成当前选定用户的。」
 *
 * 语义落在后端（`game_saves_import_garlic`），前端这里只有两条约定：
 *   ① **落点由「导入到哪个用户」这一个选择器唯一决定**，与文件从哪来无关 ——
 *      存档镜像里一个字节都读不出「它原来属于谁」（那是加密 PFS 镜像），所以
 *      「改签」= 把它落到你选定的那个用户的存档目录里 + 把文件名归一（剥掉 garlic 的
 *      `<TID>_` 与别人留下的 `<uid>_` 前缀），而不是去改镜像里的账号信息
 *      （那是内核级动作，见后端注释）。
 *   ② 这个选择器**总是**显示、默认当前用户（`.165`）。用户看不到它就等于
 *      「导入到哪去了」不可知，而「导入报成功、进游戏没存档」的两个根因之一正是落错人。
 * 所以本块自己占一行、不挤进「存档列表」那张 1/3 宽的卡里 —— 它是**独立的动作**，
 * 不是列表的附属品。
 */

/* 只读状态行的标签列要宽一些：`GridRow` 默认的 86px 是给详情页那种短标签
 * （「版本」「大小」）用的，这里全是「启动游戏时拉取」这种五六个字，86px 会**在词中间**
 * 折行（「启动游戏时拉 / 取」），很难看。 */
const RO_COLS = 'grid-cols-[124px_minmax(0,1fr)]'

export default function SaveManagerModal({ show, game, onClose, addToast, showConfirm, config }) {
  const { t } = useTranslation()
  const tid = game && game.tid
  const [data, setData] = useState(null)
  const [err, setErr] = useState('')
  const [remote, setRemote] = useState(null)     // {mode, smb, ftp, http} 只读展示
  const [remoteFrom, setRemoteFrom] = useState('none') // 'global' | 'game' | 'none'
  const [busyId, setBusyId] = useState('')
  const [uploading, setUploading] = useState(null)   // {name, percent|null}
  const [snapping, setSnapping] = useState(false)    // 「立即备份」进行中
  const [busySnap, setBusySnap] = useState('')       // `${uid}/${ts}`，该行正在还原/删除
  const [syncing, setSyncing] = useState('')         // '' | 'push' | 'pull'：立即同步进行中
  const [remoteInfos, setRemoteInfos] = useState({}) // { [uid]: 远程只读元数据（大小/文件数/时间）}
  const [busyRemote, setBusyRemote] = useState('')   // 正在删远程存档的那个 uid
  const [importing, setImporting] = useState(false)  // garlic 兼容导入进行中
  const [gxUid, setGxUid] = useState('')             // garlic 导入的目标用户（多用户时才让人选）
  const [snapEvents, setSnapEvents] = useState([])   // [CN-FORK] .174 「最近活动」事件列表
  const fileRef = useRef(null)
  const gxRef = useRef(null)                         // garlic 导入用的独立 input（accept 与去向都不同）
  const pendingSlot = useRef(null)                   // null = 新存档；字符串 = 覆盖该槽
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false }
  }, [])

  const load = useCallback(async () => {
    if (!tid) return
    let d = null
    let failed = ''
    try {
      d = await getSaves(tid)
    } catch (e) {
      failed = e.message || t('game.saves_read_fail', '读取存档失败')
    }
    if (!aliveRef.current) return
    if (d) {
      setData(d)
      /* 远程位置与自动存档状态都按「全局优先」解析 —— 与引擎同一口径。 */
      setRemote(resolveRemoteForGame(config, d.auto))
      setRemoteFrom(resolveAutoForGame(config, d.auto).from)
      setErr('')
      /* [CN-FORK] v0.5.2.145 逐个用户拉远程存档的只读元数据，供「远程游戏」区块展示。
       * 一次失败不影响其它用户（catch 后该 uid 记成 {error}）。数据来自
       * /game_save_remote_info，是远端真实信息，不是本地。 */
      const infos = {}
      await Promise.all((d.saves || []).map(async (sv) => {
        try {
          infos[sv.id] = await getRemoteSaveInfo(tid, sv.id)
        } catch (e) {
          infos[sv.id] = { configured: true, exists: false, error: (e && e.message) || '查询失败' }
        }
      }))
      if (aliveRef.current) setRemoteInfos(infos)
    }
    if (failed) setErr(failed)
  }, [tid, t, config])

  /* 打开 / 换游戏时把列表与上传状态归零。
   * 写在**渲染期**（键为 tid）而不是 effect 里：effect 体里同步 setState 会引发级联渲染，
   * 而「prop 变了就把这几个 state 重置」本来就属于 React 官方说的「渲染期调整 state」。 */
  const openKey = show && tid ? tid : ''
  const [loadedKey, setLoadedKey] = useState('')
  if (openKey !== loadedKey) {
    setLoadedKey(openKey)
    setData(null); setErr('')
    setRemote(null); setRemoteFrom('none')
    setBusyId(''); setUploading(null)
    setSnapping(false); setBusySnap(''); setSyncing('')
    setImporting(false); setGxUid('')
    setRemoteInfos({}); setBusyRemote('')
    setSnapEvents([])
  }

  useEffect(() => {
    if (!show || !tid) return
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开/换游戏时拉一次存档列表与自动存档配置
    load()
  }, [show, tid, load])

  /* [CN-FORK] v0.5.2.174 「最近活动」轮询。
   *
   * 用户口径：「发生记录也没有写到屏幕上」。快照的**负向结果**（内容没变 / 没得拍 /
   * 拍失败）在 pldmgr.log 里有，但界面上一个像素都不落 —— 用户看不到「到底跑没跑」。
   * 这里在弹窗打开期间每 5 秒拉一次最近事件，把「静默的失败」变成看得见的记录。
   *
   * 与 load() 分开、用独立的 effect，是因为它的节奏（5s）与存档列表（只在打开时拉
   * 一次）不同；合并会让列表每 5 秒重渲染一次，弹窗里的输入焦点/滚动位置都会抖。
   * 换游戏（tid 变）时旧定时器随 effect 清理一起停掉，不会串台。 */
  useEffect(() => {
    if (!show || !tid) return undefined
    let alive = true
    const pull = async () => {
      try {
        const r = await getSnapEvents(tid, 16)
        if (alive && r && Array.isArray(r.events)) setSnapEvents(r.events)
      } catch { /* 只读展示，拉不到就保持上一份，不弹红字 */ }
    }
    pull()
    const timer = setInterval(pull, 5000)
    return () => { alive = false; clearInterval(timer) }
  }, [show, tid])

  if (!show || !game) return null

  const saves = (data && data.saves) || []
  const snapshots = (data && data.snapshots) || []
  /* [CN-FORK] v0.5.2.165 当前用户（后端从 sceUserServiceGetForegroundUser 取，
   * 见 game_saves_current_uid）。**不能拿 saves[0] 当默认值** —— saves 是按 uid 升序
   * 排的，当前用户还没在这款游戏上存过档时 saves[0] 指向的是**别人**，于是 garlic
   * 导入（以及空槽上传）落到错误的人名下。用户报的「兼容导入后的存档，没有对当前
   * 用户生效」就是这个：导入报成功、进游戏还是没有存档。 */
  const currentUid = (data && data.current_uid) || ''
  /* 可选的目标用户 = 主机上**全部**用户目录（后端 users[]）∪ 有存档的那几个
   * （兜底：老后端 / 别的调用方没给 users[] 时，至少还能选有存档的用户）。
   * 当前用户排最前，且是默认选中项。 */
  const targetUsers = (() => {
    const list = []
    const seen = new Set()
    const push = (u) => { if (u && !seen.has(u)) { seen.add(u); list.push(u) } }
    push(currentUid)
    for (const u of ((data && data.users) || [])) push(u)
    for (const sv of saves) push(sv.id)
    return list
  })()
  const gxTarget = gxUid || currentUid || (saves[0] && saves[0].id) || ''
  const userLabel = (u) => `${t('game.user', '用户')} ${u}${u === currentUid ? t('game.user_current_suffix', '（当前）') : ''}`
  /* auto_state 是后端**只读**的运行期状态；没接后端时是 {}，一律当「没有状态」。 */
  const st = (data && data.auto_state) || {}
  const last = st.last || null
  /* 这个游戏**实际生效**的自动存档 + 快照参数（全局优先，见文件头）。
   * 本弹窗只读它 —— 配置本身在设置页「存档配置」里改。 */
  const eff = resolveAutoForGame(config, data && data.auto)

  /* [CN-FORK] v0.5.2.142 立即推 / 拉。与自动那条链走的是同一套后端代码（同一个互斥锁），
   * 所以「手动点」和「游戏退出时自动推」不会互相把对方的目录删掉。
   * 只传 tid，不传 uid —— 后端枚举这个游戏在主机上的全部用户各来一份，与自动一致。 */
  const onSync = async (action) => {
    setSyncing(action)
    try {
      const r = await syncSaves(tid, action)
      /* note 是后端给的人话总结（「已推送 2 份（3.1 MB）」/「远程还没有这个游戏的存档」）。
       * 拿不到就退回一句本地的兜底，不要弹一个空提示。 */
      addToast?.(r && r.note
        ? r.note
        : (action === 'push'
          ? t('game.sync_pushed', '已推送')
          : t('game.sync_pulled', '已拉取')))
      await load()
    } catch (e) {
      addToast?.(e.message || (action === 'push'
        ? t('game.sync_push_fail', '推送失败')
        : t('game.sync_pull_fail', '拉取失败')), 'error')
      await load()
    }
    if (aliveRef.current) setSyncing('')
  }

  /* 选文件 → 上传。两种入口共用一条路径：pendingSlot 为 null 是「新存档」，
   * 为字符串是「覆盖某个槽」。 */
  const pickFile = (slotId) => {
    pendingSlot.current = slotId || null
    if (fileRef.current) { fileRef.current.value = ''; fileRef.current.click() }
  }

  const onFile = async (ev) => {
    const file = ev.target.files && ev.target.files[0]
    if (!file) return
    const slot = pendingSlot.current
    setUploading({ name: file.name, percent: null })
    try {
      const r = await uploadSave(tid, slot || '', file)
      addToast?.(t('game.save_uploaded', '存档已上传（{{n}} 个文件）', { n: (r && r.files) || '?' }))
      await load()
    } catch (e) {
      addToast?.(e.message || t('game.save_upload_fail', '上传失败'), 'error')
    }
    if (aliveRef.current) setUploading(null)
  }

  /* ── garlic 兼容导入（v0.5.2.154）────────────────────────────────────────
   * 独立一个 file input，不复用上传那个：accept 与去向都不同 —— garlic 导出的
   * PS5 存档是**没有后缀**的 `sdimg_<名>`，`accept=".zip,..."` 会在文件选择器里
   * 直接把它藏起来（用户会以为「我明明导出了，怎么找不到」）。所以这里 accept 留空
   * = 任意文件，靠后端按内容判「是 zip 还是裸镜像」。
   *
   * 目标用户：`saves` 里有多个人时才让人选（单人主机多一步选择纯属打扰）；
   * 一个都没有时传空串 —— 后端按「第一个用户」建目录（与上传同口径），
   * 换机 / 刚装好还没玩过的机器也能直接导入。 */
  const pickGarlic = () => {
    if (gxRef.current) { gxRef.current.value = ''; gxRef.current.click() }
  }

  const doGarlicImport = async (file, uid) => {
    setImporting(true)
    try {
      const r = await importGarlic(tid, uid, file.name, file)
      /* 提示里带上落到哪个用户 —— 导入「没生效」最常见的两种原因（落错人 / 名字不对）
       * 里，落错人是用户唯一能从界面上一眼确认的。这里用不带「（当前）」后缀的短标签，
       * 否则提示会变成「已导入到用户 X（当前）（1 个文件）」，两层括号很别扭。
       * [CN-FORK] v0.5.2.173 明确写「已改签到」：用户口径「导入的时候如果是其他用户的
       * 存档，直接改签成当前选定用户的」—— 提示里把这件事说出来，用户才不用去猜
       * 这份存档现在挂在谁名下。 */
      addToast?.(t('game.garlic_imported',
        '已改签并导入到{{target}}（{{n}} 个文件）',
        {
          n: (r && r.files) || '?',
          target: uid
            ? t('game.garlic_to_user', '用户 {{uid}}', { uid })
            : t('game.garlic_to_default', '默认用户'),
        }))
      await load()
    } catch (e) {
      addToast?.(e.message || t('game.garlic_import_fail', '导入失败'), 'error')
    }
    if (aliveRef.current) setImporting(false)
  }

  const onGarlicFile = (ev) => {
    const file = ev.target.files && ev.target.files[0]
    if (!file) return
    /* [CN-FORK] v0.5.2.156 先认一下这是不是当前游戏的存档 —— 后端还会再判一次，
     * 前端这道只是为了不让用户白等一次 11MB 上传。认不出来就放行（说不清 ≠ 不匹配）。 */
    const gxTid = tidFromName(file.name)
    if (gxTid && gxTid !== String(tid).toUpperCase()) {
      addToast?.(
        t('game.garlic_wrong_game',
          '这个存档是 {{other}} 的，不是当前游戏（{{tid}}）—— 请到对应游戏里导入',
          { other: gxTid, tid }),
        'error',
      )
      return
    }
    /* [CN-FORK] v0.5.2.165 目标用户 = 当前用户优先（gxTarget）。
     * 老代码是 `gxUid || saves[0].id || ''`：当前用户没在这款游戏上存过档时，
     * saves[0] 是**别人**，而 saves 只有一条时连选择器都不显示 —— 导入静默落错人。
     * [CN-FORK] v0.5.2.173 这个 uid 就是「改签」的落点：后端只用它决定写进哪个
     * 用户的存档目录，文件里读不出原主人（加密镜像），所以不必也不该从文件里猜。 */
    const uid = gxTarget
    if (!showConfirm) { doGarlicImport(file, uid); return }
    showConfirm(
      t('game.garlic_import_title', '兼容导入 Garlic 存档'),
      t('game.garlic_import_confirm',
        '把「{{name}}」改签到{{target}}？不论这份存档原来属于谁，都会落到这个用户名下；只覆盖同名的存档镜像，主机自己的备份（_bu_）与系统存档不会被删。',
        {
          name: file.name,
          target: uid
            ? t('game.garlic_to_user', '用户 {{uid}}', { uid })
            : t('game.garlic_to_default', '默认用户'),
        }),
      () => doGarlicImport(file, uid),
    )
  }

  const onDelete = (sv) => {
    if (!showConfirm) return
    showConfirm(
      t('game.save_delete_title', '删除存档'),
      t('game.save_delete_confirm',
        '删除这个游戏在「用户 {{uid}}」下的全部存档？此操作无法撤销。',
        { uid: sv.name || sv.id }),
      async () => {
        setBusyId(sv.id)
        try {
          await deleteSave(tid, sv.id)
          addToast?.(t('game.save_deleted', '存档已删除'))
          await load()
        } catch (e) {
          addToast?.(e.message || t('game.save_delete_fail', '删除失败'), 'error')
        }
        if (aliveRef.current) setBusyId('')
      },
      true,
    )
  }

  /* ── 远程存档的删除（v0.5.2.159）──────────────────────────────────────────
   * 与上面的 onDelete 是**两个不同的东西**：onDelete 删主机上的存档目录，
   * 这个只删远端那一份 zip（主机本地与本地快照都不动）。确认框里必须把这句
   * 说清楚 —— 同一屏两个垃圾桶，用户按错一个的代价不对称。
   *
   * 删完只刷新这一行的远程信息，不调 load()：load() 会重扫主机存档目录，
   * 而这条动作压根没碰主机目录，重扫出来的还是同一份数据，白等一次 IO。 */
  const onRemoteDelete = (sv) => {
    if (!showConfirm) return
    showConfirm(
      t('game.remote_delete_title', '删除远程存档'),
      t('game.remote_delete_confirm',
        '删除远程位置上的「用户 {{uid}}」这一份？只删远端那份备份，主机上的存档和本地快照都不动。此操作无法撤销。',
        { uid: sv.name || sv.id }),
      async () => {
        setBusyRemote(sv.id)
        try {
          await deleteRemoteSave(tid, sv.id)
          addToast?.(t('game.remote_deleted', '远程存档已删除'))
          try {
            const info = await getRemoteSaveInfo(tid, sv.id)
            if (aliveRef.current) setRemoteInfos((m) => ({ ...m, [sv.id]: info }))
          } catch {
            /* 回读失败不影响「已删除」这个结论，下一行留着上一次的数字就是 */
          }
        } catch (e) {
          addToast?.(e.message || t('game.remote_delete_fail', '删除远程存档失败'), 'error')
        }
        if (aliveRef.current) setBusyRemote('')
      },
      true,
    )
  }

  /* ── 本地快照（v0.5.2.140）───────────────────────────────────────────────
   * 三个动作都走同一条 POST /game_save_snap，只是 action 不同。
   * restore/delete 要把 uid 与 ts **原样回传**（那是快照目录的最后两段路径，
   * 后端校验不过就直接 400）—— 前端不自己拼路径，也不做任何变形。 */
  const onSnapRun = async () => {
    setSnapping(true)
    try {
      const r = await snapAction(tid, 'run')
      const made = (r && r.made) || 0
      addToast?.(made
        ? t('game.snap_made', '已生成 {{n}} 份快照', { n: made })
        : t('game.snap_nochange', '存档没有变化，未新增快照'))
      await load()
    } catch (e) {
      addToast?.(e.message || t('game.snap_fail', '备份失败'), 'error')
    }
    if (aliveRef.current) setSnapping(false)
  }

  const onSnapRestore = (sn) => {
    if (!showConfirm) return
    showConfirm(
      t('game.snap_restore_title', '从快照还原'),
      t('game.snap_restore_confirm',
        '用「{{time}}」的快照整份覆盖「用户 {{uid}}」当前的存档？当前的会被替换掉（替换失败会自动退回）。',
        { time: sn.mtime || sn.ts, uid: sn.uid }),
      async () => {
        setBusySnap(`${sn.uid}/${sn.ts}`)
        try {
          await snapAction(tid, 'restore', { uid: sn.uid, ts: sn.ts })
          addToast?.(t('game.snap_restored', '已从快照还原'))
          await load()
        } catch (e) {
          addToast?.(e.message || t('game.snap_restore_fail', '还原失败'), 'error')
        }
        if (aliveRef.current) setBusySnap('')
      },
      true,
    )
  }

  const onSnapDelete = (sn) => {
    if (!showConfirm) return
    showConfirm(
      t('game.snap_delete_title', '删除快照'),
      t('game.snap_delete_confirm',
        '删除「{{time}}」这份快照？只删快照，不动主机上当前的存档。此操作无法撤销。',
        { time: sn.mtime || sn.ts }),
      async () => {
        setBusySnap(`${sn.uid}/${sn.ts}`)
        try {
          await snapAction(tid, 'delete', { uid: sn.uid, ts: sn.ts })
          addToast?.(t('game.snap_deleted', '快照已删除'))
          await load()
        } catch (e) {
          addToast?.(e.message || t('game.snap_delete_fail', '删除失败'), 'error')
        }
        if (aliveRef.current) setBusySnap('')
      },
      true,
    )
  }


  return (
    <Modal
      show={show}
      title={t('game.saves_title', '存档管理')}
      onClose={onClose}
      layout="panel"
    >
      {/* [CN-FORK] v0.5.2.112 面板形态（用户：「存档管理页面打开后铺满右边整个区域」）。
       * [CN-FORK] v0.5.2.147 第一行三块并排（各 1/3）：自动存档 / 远程位置（含逐用户远程信息）/
       * 存档列表；本地快照仍在下面单独满宽铺开。窄屏（<1280px）第一行退回单列。 */}
      <div className="panel-body leading-relaxed font-normal flex flex-col gap-5">
        {/* [CN-FORK] v0.5.2.163 标题里把游戏 TID 直接显示在名称后边（用户口径：「存档管理
            的游戏名称后边加上游戏编号」），跟原「路径」分开两行：路径放第二行更专业。 */}
        <div className="space-y-0.5">
          <p className="text-zinc-300 panel-note font-semibold">
            {game.name || tid}
            <span className="ml-2 font-mono text-ps-blue text-xs">{tid}</span>
          </p>
          {data && data.local_path ? (
            <p className="font-mono tracking-wide text-[11px] text-zinc-500 truncate">{data.local_path}</p>
          ) : null}
        </div>

        {err && <Note tone="error" icon={AlertTriangle}>{err}</Note>}

        {!data && !err && (
          <div className="flex items-center gap-3 py-16 justify-center text-zinc-500">
            <RefreshCw className="w-5 h-5 animate-spin" />
            <span>{t('game.saves_loading', '正在读取存档…')}</span>
          </div>
        )}

        {data && (
          <div className="flex flex-col gap-5">
            {/* [CN-FORK] v0.5.2.147 第一行三板块并排，每个占 1/3：
                自动存档 / 远程位置（配置摘要 + 逐用户远程信息）/ 存档列表。
                `.146` 时第三栏是独立的「远程存档」卡、`存档列表` 满宽另起一行；
                现在把远程信息并进「远程位置」、让出位置给「存档列表」。
                **刻意不加 `items-start`**（用户口径：「让这三个高度一致保持一样，不管哪个变高，
                另外两个同步变高」）—— 默认的 `items-stretch` 会让三张卡跟着最高那张一起长，
                底边对齐；`items-start` 则各长各的、三栏参差。 */}
            <div className="grid grid-cols-1 xl:grid-cols-3 gap-5">
              {/* ── ① 自动存档（只读展示 + 立即推拉）── */}
              <Block
                className="min-w-0"
                title={t('game.auto_title', '自动存档')}
                icon={Save}
                actions={
                  <>
                    {/* 「游戏正在运行」是引擎**当场探测**的结果（不是守护线程 5 秒前的记忆）。
                        有它在，用户才能理解「为什么我一打开游戏存档就变了」。 */}
                    {st.running && (
                      <Chip tone="info" title={t('game.running_hint', '这款游戏此刻正在运行，退出时若开了自动回传会自动推送')}>
                        <Activity className="w-3.5 h-3.5 mr-1" />
                        {t('game.running', '游戏运行中')}
                      </Chip>
                    )}
                    <Chip tone={eff.enabled ? 'ok' : 'muted'}>{eff.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}</Chip>
                  </>
                }
              >
                {/* 只读状态行。**没有开关**：开关在设置页「存档配置」，全局生效。
                    用 GridRow 而不是灰掉的 Switch —— 灰开关会让人一直点它。
                    `.147` 起回到满宽单栏：`.146` 的 `md:grid-cols-2` 本来只是为了给右半边
                    的「远程游戏」徽标让位；徽标撤掉后左右分栏就只剩副作用 —— 三行被压到半宽，
                    窄窗（1280–1660px，`xl:grid-cols-3` 刚生效那档）下 `RO_COLS` 的 124px 标签列
                    吃掉一半，值列只剩二十几像素，「未启用」一个字一行。 */}
                <div className="min-w-0">
                  <GridRow
                    cols={RO_COLS}
                    label={t('game.auto_enable', '启用自动存档')}
                    value={eff.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}
                  />
                  <GridRow
                    cols={RO_COLS}
                    label={t('game.auto_on_launch', '启动游戏时拉取')}
                    value={eff.on_launch ? t('game.on', '已启用') : t('game.off', '未启用')}
                  />
                  <GridRow
                    cols={RO_COLS}
                    label={t('game.auto_on_exit', '退出游戏时回传')}
                    value={eff.on_exit ? t('game.on', '已启用') : t('game.off', '未启用')}
                  />
                </div>

                {/* 唯一的「去改配置」出口。单游戏这边一个可编辑控件都没有，
                    不给这条提示的话用户会在弹窗里找不到开关干瞪眼。 */}
                <p className="panel-tiny text-zinc-500 mt-2">
                  {t('game.remote_global_hint', '以上开关与远程位置都在「设置 → 存档配置」里统一设置，对所有游戏生效。')}
                </p>

                <div className="flex flex-wrap items-center gap-2 mt-4">
                  {/* [CN-FORK] v0.5.2.142 立即推 / 拉。**不受总开关影响** ——
                      手动就是手动，用户想试一次不该被迫先把自动打开。后端读的是它自己
                      那份已保存的配置（全局或本游戏的），与这里的只读展示同源。
                      [CN-FORK] v0.5.2.154 游戏运行中一律置灰：后端会拒拦并回人话，
                      但让用户白点一次才被告知是坏体验；而且推拉都是**整份替换**存档目录，
                      游戏正把存档读在内存里、退出时还要写回去，这时候动它最容易丢进度。 */}
                  <button
                    type="button"
                    className="btn btn-outline"
                    disabled={!!syncing || !!st.running}
                    title={st.running ? t('game.running_no_sync', '游戏正在运行，请先退出游戏再推送 / 拉取存档。') : undefined}
                    onClick={() => onSync('pull')}
                  >
                    <CloudDownload className="w-4 h-4" />
                    <span>{syncing === 'pull' ? t('game.syncing', '同步中…') : t('game.sync_pull', '立即拉取')}</span>
                  </button>
                  <button
                    type="button"
                    className="btn btn-outline"
                    disabled={!!syncing || !!st.running}
                    title={st.running ? t('game.running_no_sync', '游戏正在运行，请先退出游戏再推送 / 拉取存档。') : undefined}
                    onClick={() => onSync('push')}
                  >
                    <CloudUpload className="w-4 h-4" />
                    <span>{syncing === 'push' ? t('game.syncing', '同步中…') : t('game.sync_push', '立即推送')}</span>
                  </button>
                </div>

                {/* 置灰的按钮必须给一句「为什么」—— 只有 title 的话，电视端遥控器用户
                    根本看不到（没有 hover）。 */}
                {st.running && (
                  <p className="panel-tiny text-amber-400/90 mt-2">
                    {t('game.running_no_sync', '游戏正在运行，请先退出游戏再推送 / 拉取存档。')}
                  </p>
                )}

                {/* 引擎上次干了什么。没有这一段的话，「开关打开了但什么都没发生」完全没法自查。 */}
                {last && (
                  <p className={`panel-tiny mt-2 ${last.ok ? 'text-zinc-500' : 'text-amber-400/90'}`}>
                    {t('game.last_sync', '上次：{{act}} · {{at}}{{why}}', {
                      act: last.action === 'push' ? t('game.act_push', '推送') : t('game.act_pull', '拉取'),
                      at: last.at || '',
                      why: last.ok ? '' : ` · ${last.message || t('game.failed', '失败')}`,
                    })}
                  </p>
                )}
              </Block>

              {/* ── ② 远程位置（配置摘要 + 逐用户远程信息）── */}
              <Block
                className="min-w-0"
                title={t('game.remote_location', '远程位置')}
                icon={CloudUpload}
              >
                {/* 远程位置：只读摘要。来源（全局 / 本游戏已存）标出来，
                    用户才知道该去哪儿改 —— 见文件头「一个控件只有一个家」。 */}
                {remote && remoteFrom !== 'none' ? (
                  <div className="rounded-lg border border-white/10 bg-white/[0.02] p-2.5">
                    <p className="text-white panel-note font-semibold">{remoteSummary(remote)}</p>
                    <p className="panel-tiny text-zinc-500 mt-0.5">
                      {remoteFrom === 'global'
                        ? t('game.remote_from_global', '（来自全局设置）')
                        : t('game.remote_from_game', '（本游戏已配置）')}
                    </p>
                  </div>
                ) : (
                  <Note tone="warn">
                    {t('game.remote_unset', '尚未配置远程位置，请到「设置 → 存档配置」填写。')}
                  </Note>
                )}

                {/* [CN-FORK] v0.5.2.147 逐用户远程信息 —— 原来它是并排的第三张卡（「远程存档」），
                    现在并进本卡。理由：它和「存档列表」是**同一批用户的两份列表**，各占一张
                    1/3 宽的卡反而看不出对应关系；并进来还能把并排的位置让给「存档列表」。
                    数据来源不变：/game_save_remote_info 的远端真实元数据（不是本地），
                    展示口径与「存档列表」一致 —— 用户 · PS5 · 3.63 MB · 8 个文件 · 时间。 */}
                {remoteFrom !== 'none' && (
                  <div className="mt-3">
                    <p className="panel-tiny text-zinc-500 mb-1.5">{t('game.remote_saves', '远程存档')}</p>
                    {saves.length === 0 ? (
                      /* [CN-FORK] v0.6.3.196 换机/重装之后本机一个存档目录都没有，这段
                       * 是用户唯一能看到的话。原来只说「无法定位远程用户」，等于把
                       * 「你的存档还在远端、但界面查不到」这件事瞒住了 —— 真机上用户
                       * 就是卡在这里（远端明明有 1a1d3627.zip，界面一片空白）。
                       * 现在把出路一起写出来：走「兼容导入」，它会改签到当前用户。 */
                      <Note tone="muted">
                        {t('game.remote_no_local', '本机还没有这款游戏的存档，查不到远端属于谁。远端若有旧账号的备份，请用「兼容导入」导入（会自动改签到当前用户）。')}
                      </Note>
                    ) : (
                      <div className="space-y-1.5">
                        {saves.map((sv) => {
                          const ri = remoteInfos[sv.id]
                          /* 按钮只在这行「确实有远程存档」时出现；协议上也只有 SMB
                           * 会真删（FTP/HTTP 后端回 400，见 save_remote.h），所以
                           * 非 SMB 干脆不给按钮 —— 点了才知道不支持是最差的一种提示。 */
                          const canDel = !!ri && ri.exists && !ri.error && ri.mode === 'smb'
                          return (
                            <div key={sv.id} className="flex items-center gap-2 rounded-lg border border-white/10 bg-white/[0.02] px-2.5 py-1.5 text-sm">
                              <div className="min-w-0 flex-1">
                                <span className="text-white font-medium">{t('game.user', '用户')} {sv.id}</span>
                                <span className="text-zinc-500"> · PS5 · </span>
                                {ri === undefined ? (
                                  <span className="text-zinc-500">{t('game.remote_loading', '加载中…')}</span>
                                ) : !ri.configured ? (
                                  <span className="text-zinc-500">{t('game.remote_unset_short', '未配置远程位置')}</span>
                                ) : !ri.exists ? (
                                  <span className="text-zinc-500">{t('game.remote_no_save', '远端没有这个用户的存档')}</span>
                                ) : ri.error ? (
                                  <span className="text-amber-400/90">{ri.error}</span>
                                ) : (
                                  <span className="text-zinc-400">
                                    {formatSizeAlways(ri.size)} · {ri.files >= 0 ? `${ri.files} ${t('game.files_unit', '个文件')}` : '—'} · {ri.mtime || '—'}
                                  </span>
                                )}
                              </div>
                              {/* [CN-FORK] v0.5.2.172 下载按钮排在删除**前面**（用户口径：
                                  「所有存档列表删除按钮前边加一个下载按钮」）。它只在
                                  远端确实有这一份时出现 —— 点了才发现没得下是最差的一种提示。
                                  与左边「立即拉取」是两回事：拉取会整份替换主机存档，
                                  下载只把云上那份拿到电脑上（后端走 sr_fetch，不碰主机）。 */}
                              {canDel && (
                                <a
                                  className="icon-btn shrink-0"
                                  href={remoteSaveDownloadUrl(tid, sv.id)}
                                  download
                                  title={t('game.remote_download', '下载远程存档')}
                                  aria-label={t('game.remote_download', '下载远程存档')}
                                >
                                  <Download className="w-4 h-4" />
                                </a>
                              )}
                              {canDel && (
                                <button
                                  type="button"
                                  className="icon-btn is-danger shrink-0"
                                  disabled={busyRemote === sv.id}
                                  title={t('game.remote_delete', '删除远程存档')}
                                  aria-label={t('game.remote_delete', '删除远程存档')}
                                  onClick={() => onRemoteDelete(sv)}
                                >
                                  <Trash2 className="w-4 h-4" />
                                </button>
                              )}
                            </div>
                          )
                        })}
                      </div>
                    )}
                  </div>
                )}
              </Block>

              {/* ── ③ 存档列表 ──
               * [CN-FORK] v0.5.2.147 从「满宽另起一行」提到并排的第三栏：满宽时内容贴左、按钮贴右，
               * 中间一大段是空的；并排后与「远程位置」里的逐用户远程信息挨在一起，同一批用户
               * 的本地/远程两份数据在同一屏内可对照。 */}
              <Block
                className="min-w-0"
                title={t('game.saves_list', '存档列表')}
                icon={HardDrive}
                actions={
                  <>
                    <button type="button" className="icon-btn" title={t('common.refresh', '刷新')} aria-label={t('common.refresh', '刷新')} onClick={load}>
                      <RefreshCw className="w-4 h-4" />
                    </button>
                    <button type="button" className="btn btn-outline" disabled={!!uploading} onClick={() => pickFile(null)}>
                      <Upload className="w-4 h-4" />
                      <span>{t('game.save_upload_new', '上传存档')}</span>
                    </button>
                  </>
                }
              >
                {uploading && (
                  <div className="mb-3">
                    <div className="flex items-center gap-2 panel-note text-zinc-400 mb-1.5">
                      <FileArchive className="w-3.5 h-3.5" />
                      <span className="truncate">{t('game.save_uploading', '正在上传 {{name}}…', { name: uploading.name })}</span>
                    </div>
                    <ProgressBar indeterminate />
                  </div>
                )}

{saves.length === 0 ? (
                <MiniEmpty icon={HardDrive}>{t('game.saves_empty', '这个游戏还没有存档')}</MiniEmpty>
              ) : (
                <ul className="space-y-1.5">
                  {saves.map((sv) => (
                    /* [CN-FORK] v0.5.2.173 这一行**只剩一栏**：右侧那栏「存档转换」已删
                     * （用户口径：「存档转换的标签和功能删除」）。它原本把整行劈成两半，
                     * 而它想干的事（把别人的存档弄到自己名下）兼容导入本来就能做。 */
                    <li
                      key={sv.id}
                      className="flex items-center gap-3 p-2.5 rounded-lg border border-white/10 bg-white/[0.02]"
                    >
                      <div className="min-w-0 flex-1">
                        <div className="flex items-center gap-2 min-w-0">
                          <span className="text-white panel-body font-semibold truncate">
                            {t('game.save_user', '用户 {{uid}}', { uid: sv.name || sv.id })}
                          </span>
                          {sv.kind && <Chip tone="info">{sv.kind}</Chip>}
                        </div>
                        <div className="panel-tiny text-zinc-500 mt-0.5 tabular-nums">
                          {formatSizeAlways(sv.size)}
                          {sv.files ? ` · ${t('game.n_files', '{{n}} 个文件', { n: sv.files })}` : ''}
                          {formatStamp(sv.mtime) ? ` · ${formatStamp(sv.mtime)}` : ''}
                        </div>
                      </div>
                      <div className="flex items-center gap-1 shrink-0">
                        <a
                          className="icon-btn"
                          href={saveDownloadUrl(tid, sv.id)}
                          download
                          title={t('game.save_download', '下载')}
                          aria-label={t('game.save_download', '下载')}
                        >
                          <Download className="w-4 h-4" />
                        </a>
                        <button
                          type="button"
                          className="icon-btn"
                          disabled={!!uploading || busyId === sv.id}
                          title={t('game.save_overwrite', '上传覆盖')}
                          aria-label={t('game.save_overwrite', '上传覆盖')}
                          onClick={() => pickFile(sv.id)}
                        >
                          <Upload className="w-4 h-4" />
                        </button>
                        <button
                          type="button"
                          className="icon-btn is-danger"
                          disabled={busyId === sv.id}
                          title={t('game.save_delete', '删除')}
                          aria-label={t('game.save_delete', '删除')}
                          onClick={() => onDelete(sv)}
                        >
                          <Trash2 className="w-4 h-4" />
                        </button>
                      </div>
                    </li>
                  ))}
                </ul>
              )}

                <p className="panel-tiny text-zinc-500 mt-3">
                  {t('game.save_hint', '一行 = 一个用户在这个游戏上的整份存档（含主机自己的 _bu_ 备份）。下载与上传都是整份 zip，上传会整份替换。')}
                </p>
              </Block>
            </div>
          </div>
        )}

        {/* ── ④ 兼容导入（Garlic）──
         * [CN-FORK] v0.5.2.173 从「存档列表」那张 1/3 宽的卡里搬出来，单独满宽一行
         * （用户口径：「这个调整布局，单独放一行」）。
         *
         * 为什么它必须是**独立的一块**而不是列表的附属：
         *   · 它和「上传存档」是**两回事** —— 上传 = 整份替换（把整个 <TID> 目录换掉），
         *     导入 = **逐文件合并**（只覆盖 garlic 导出的那几个镜像）。并排放在一起，
         *     用户会以为「都是上传，选一个就行」。
         *   · 它是**跨用户**的动作：落点由「导入到哪个用户」决定，与列表里当前有几条
         *     存档无关。挂在某一行下面会让人以为「只对这一行生效」。
         *   · 1/3 宽装不下：目标用户分段按钮 + 导入按钮 + 三行长提示挤在一起会被折成
         *     六到七行（用户截的图就是那一团）。搬出来之后宽屏下选择器与按钮并排一行、
         *     提示满宽一行。 */}
        {data && (
          <Block
            title={t('game.garlic_title', '兼容导入（Garlic）')}
            icon={PackagePlus}
          >
            <div className="flex flex-col sm:flex-row sm:items-end gap-3">
              {/* [CN-FORK] v0.5.2.165 目标用户**总是**显示，而且默认是当前用户。
                  老代码只在 saves.length > 1 时才画选择器、默认值取 saves[0] ——
                  当前用户没在这款游戏上存过档时，saves 里可能只有别人一条，
                  于是连选择器都不出现、导入静默落到错误的人名下。 */}
              <div className="min-w-0 sm:w-[360px] shrink-0">
                <p className="panel-tiny text-zinc-500 mb-1.5">
                  {t('game.garlic_target', '导入到哪个用户')}
                </p>
                {targetUsers.length > 1 ? (
                  <Segmented
                    options={targetUsers.map((u) => ({ value: u, label: userLabel(u) }))}
                    value={gxTarget}
                    onChange={setGxUid}
                    label={t('game.garlic_target', '导入到哪个用户')}
                    className="w-full"
                    itemClassName="flex-1"
                  />
                ) : (
                  <p className="panel-tiny text-zinc-300">
                    {targetUsers.length
                      ? userLabel(targetUsers[0])
                      : t('game.garlic_to_default', '默认用户')}
                  </p>
                )}
              </div>
              <button
                type="button"
                className="btn btn-outline w-full sm:w-[240px] shrink-0"
                disabled={!!importing || !!uploading}
                onClick={pickGarlic}
              >
                <PackagePlus className="w-4 h-4" />
                <span>
                  {importing
                    ? t('game.garlic_importing', '正在导入…')
                    : t('game.garlic_import', '兼容导入（Garlic）')}
                </span>
              </button>
            </div>
            {importing && <div className="mt-3"><ProgressBar indeterminate /></div>}
            <p className="panel-tiny text-zinc-500 mt-3 leading-snug">
              {t('game.garlic_hint',
                '导入 garlic-savemgr 导出的加密存档：PS5 是一个 sdimg_ 开头的镜像文件（没有后缀），PS4 是一个 zip 包。只覆盖同名的镜像，主机自己的备份与系统存档不动。')}
            </p>
            <p className="panel-tiny text-zinc-500 mt-1.5 leading-snug">
              {t('game.garlic_resign_hint',
                '不论这份存档原来属于谁，都会落到你上面选定的用户名下（存档镜像里读不出原主人，所以「改签」= 换落点 + 把文件名归一）。')}
            </p>
          </Block>
        )}

        {/* ── ⑤ 本地快照（v0.5.2.140）──
         * 与上面并排会太挤，所以单独一行铺开。
         * 语义上与「远程同步」是**两件不同的事**：远程要网络、要对面在线；
         * 本地快照不依赖任何外部条件，是存档的最后一层兜底。 */}
        {data && (
          <Block
            title={t('game.snap_title', '本地快照')}
            icon={History}
            actions={
              <>
                <Chip tone={eff.snap.enabled ? 'ok' : 'muted'}>
                  {eff.snap.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}
                </Chip>
                <button
                  type="button"
                  className="btn btn-outline"
                  disabled={snapping || saves.length === 0}
                  onClick={onSnapRun}
                >
                  <History className="w-4 h-4" />
                  <span>{snapping ? t('game.snap_running', '备份中…') : t('game.snap_now', '立即备份')}</span>
                </button>
              </>
            }
          >
            {/* 参数只读：开关 / 保留数都在设置页「存档配置」里改。
                「立即备份」与下面的列表留在这里 —— 备份的是**这个游戏**的存档，
                本来就属于这个弹窗。
                [CN-FORK] v0.5.2.154 删掉「间隔（分钟）」那一行：定时快照已废，
                触发时机改成了「游戏退出时」，留一个恒定数字只会让人以为还能调
                （设置页那边也只剩开关 + 保留份数）。 */}
            <GridRow
              cols={RO_COLS}
              label={t('game.snap_enable', '启用本地快照')}
              value={eff.snap.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}
            />
            <GridRow
              cols={RO_COLS}
              label={t('game.snap_when', '备份时机')}
              value={t('game.snap_when_exit', '每次游戏退出时（与推送远程同一时刻）')}
            />
            <GridRow
              cols={RO_COLS}
              label={t('game.snap_keep', '保留份数')}
              value={String(eff.snap.keep)}
            />
            {data.snap_used > 0 && (
              <p className="panel-tiny text-zinc-500 mt-2">
                {t('game.snap_used', '快照已占用 {{size}}', { size: formatSizeAlways(data.snap_used) })}
              </p>
            )}
            <p className="panel-tiny text-zinc-500 mt-2">
              {t('game.snap_global_hint', '快照的开关与参数在「设置 → 存档配置」里统一设置，对所有游戏生效。')}
            </p>

            <div className="mt-4">
              {snapshots.length === 0 ? (
                <MiniEmpty icon={History}>{t('game.snap_empty', '还没有本地快照')}</MiniEmpty>
              ) : (
                <ul className="space-y-1.5">
                  {snapshots.map((sn) => {
                    const key = `${sn.uid}/${sn.ts}`
                    const busy = busySnap === key
                    return (
                      <li
                        key={key}
                        className="flex items-center gap-3 p-2.5 rounded-lg border border-white/10 bg-white/[0.02]"
                      >
                        <div className="min-w-0 flex-1">
                          <div className="flex items-center gap-2 min-w-0">
                            <span className="text-white panel-body font-semibold truncate">
                              {t('game.save_user', '用户 {{uid}}', { uid: sn.uid })}
                            </span>
                            <Chip tone="muted">{formatStamp(sn.mtime) || sn.ts}</Chip>
                          </div>
                          <div className="panel-tiny text-zinc-500 mt-0.5 tabular-nums">
                            {formatSizeAlways(sn.size)}
                            {sn.files ? ` · ${t('game.n_files', '{{n}} 个文件', { n: sn.files })}` : ''}
                          </div>
                        </div>
                        <div className="flex items-center gap-1 shrink-0">
                          <button
                            type="button"
                            className="icon-btn"
                            disabled={busy}
                            title={t('game.snap_restore', '还原')}
                            aria-label={t('game.snap_restore', '还原')}
                            onClick={() => onSnapRestore(sn)}
                          >
                            <RotateCcw className="w-4 h-4" />
                          </button>
                          {/* [CN-FORK] v0.5.2.172 下载这一份快照（后端把它打成 zip 直出）。
                              与上面「还原」的分工：还原是**就地**整份覆盖主机存档，
                              下载是把它拿到电脑上留个底。排在删除**前面**（用户口径）。 */}
                          <a
                            className="icon-btn"
                            href={snapDownloadUrl(tid, sn.uid, sn.ts)}
                            download
                            title={t('game.snap_download', '下载快照')}
                            aria-label={t('game.snap_download', '下载快照')}
                          >
                            <Download className="w-4 h-4" />
                          </a>
                          <button
                            type="button"
                            className="icon-btn is-danger"
                            disabled={busy}
                            title={t('game.snap_delete', '删除快照')}
                            aria-label={t('game.snap_delete', '删除快照')}
                            onClick={() => onSnapDelete(sn)}
                          >
                            <Trash2 className="w-4 h-4" />
                          </button>
                        </div>
                      </li>
                    )
                  })}
                </ul>
              )}

              <p className="panel-tiny text-zinc-500 mt-3">
                {t('game.snap_hint',
                  '一行 = 某个用户在某时刻的整份存档。「还原」用它整份覆盖当前存档（失败会自动退回），「删除」只删快照、不动主机上的存档。存档内容没变时不会新增，所以份数涨得很慢。')}
              </p>
            </div>

            {/* ── [CN-FORK] v0.5.2.174 「最近活动」──
             * 用户口径：「发生记录也没有写到屏幕上」。
             * 上面那个列表只画**已经存在**的快照，于是「这一轮到底跑了没有」永远是空白
             * —— 内容没变、没得拍、拍失败都不落像素。这一段把后端事件环（每 5 秒拉一次）
             * 摊开，含**负向结果**，时间戳也在（全局 /log 的内存环没有时刻）。
             * 与「快照列表」分工：上面是**结果**（有哪些份），这里是**过程**（跑没跑、为什么没拍）。 */}
            <div className="mt-4 pt-4 border-t border-white/10">
              <div className="flex items-center gap-2 mb-2">
                <Activity className="w-4 h-4 text-zinc-400" />
                <span className="panel-body font-semibold text-white">
                  {t('game.snap_events_title', '最近活动')}
                </span>
              </div>
              {snapEvents.length === 0 ? (
                <p className="panel-tiny text-zinc-500">
                  {t('game.snap_events_empty',
                    '还没有记录。游戏退出后（或点「立即备份」）这里会显示每一次快照的结果。')}
                </p>
              ) : (
                <ul className="space-y-1">
                  {snapEvents.map((ev, i) => {
                    const lv = ev.lv | 0
                    const tone = lv === 1 ? 'text-emerald-400'
                      : lv === 3 ? 'text-rose-400'
                        : lv === 2 ? 'text-amber-400'
                          : 'text-zinc-400'
                    return (
                      <li key={`${ev.t}-${i}`} className="flex items-start gap-2 panel-tiny">
                        <span className="tabular-nums text-zinc-500 shrink-0">{ev.t}</span>
                        <span className={tone}>{ev.x}</span>
                      </li>
                    )
                  })}
                </ul>
              )}
            </div>
          </Block>
        )}

        <input
          ref={fileRef}
          type="file"
          accept=".zip,.tar,.gz,.tgz"
          className="hidden"
          onChange={onFile}
        />

        {/* [CN-FORK] v0.5.2.154 garlic 导入的独立 input：**刻意不写 accept**。
            garlic 在 PS5 上导出的是一个没有后缀的 `sdimg_<名>` 文件，上面那条
            `.zip,.tar,.gz,.tgz` 会在文件选择器里直接把它藏起来（用户会以为
            「我明明导出了，怎么找不到」）。这里是「任意文件」，由后端按内容判
            「是 zip 还是裸镜像」。 */}
        <input
          ref={gxRef}
          type="file"
          className="hidden"
          onChange={onGarlicFile}
        />
      </div>
    </Modal>
  )
}
