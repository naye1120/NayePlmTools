import React, { useState, useEffect, useRef, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import {
  Save, RefreshCw, Download, Upload, Trash2, AlertTriangle, HardDrive, FileArchive,
  History, RotateCcw, CloudDownload, CloudUpload, Activity,
} from 'lucide-react'
import Modal from '../ui/Modal'
import { Block, Chip, Note, MiniEmpty, ProgressBar, GridRow } from './parts'
import { formatSizeAlways, formatStamp } from './format'
import {
  getSaves, saveDownloadUrl, uploadSave, deleteSave, snapAction,
  syncSaves,
} from '../../utils/gameApi'
import {
  resolveAutoForGame, resolveRemoteForGame, remoteSummary,
} from '../../utils/saveConfig'

/* [CN-FORK] v0.5.2.112 存档管理弹窗（磁贴第一颗按钮）
 *
 * 用户口径（`.145` 收束后的最终形态）：
 *   「把配置功能迁移过去，配置一个全局生效。单个游戏包开启 拉取和推送按钮。」
 * 所以本弹窗**不再有可编辑的配置**，只剩三件事：
 *   ① 这个游戏**实际生效**的自动存档状态（开关 / 两个时机）—— 只读，来自设置页「存档配置」；
 *   ② 远程位置只读摘要 + 「立即拉取 / 立即推送」+ 引擎上次干了什么（auto_state）；
 *   ③ 存档列表（下载 / 上传覆盖 / 删除 / 上传新的）与本地快照列表（立即备份 / 还原 / 删除）。
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
  const fileRef = useRef(null)
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
  }

  useEffect(() => {
    if (!show || !tid) return
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开/换游戏时拉一次存档列表与自动存档配置
    load()
  }, [show, tid, load])

  if (!show || !game) return null

  const saves = (data && data.saves) || []
  const snapshots = (data && data.snapshots) || []
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
       * 两块内容在宽屏上并排：左边是「配置」（开关 + 远程位置摘要），右边是「存档列表」。
       * 窄屏（<1280px）自动退回单列。 */}
      <div className="panel-body leading-relaxed font-normal flex flex-col gap-5">
        <p className="text-zinc-500 panel-note">
          {game.name || tid}
          {data && data.local_path ? <span className="font-mono tracking-wide"> · {data.local_path}</span> : null}
        </p>

        {err && <Note tone="error" icon={AlertTriangle}>{err}</Note>}

        {!data && !err && (
          <div className="flex items-center gap-3 py-16 justify-center text-zinc-500">
            <RefreshCw className="w-5 h-5 animate-spin" />
            <span>{t('game.saves_loading', '正在读取存档…')}</span>
          </div>
        )}

        {data && (
          <div className="grid grid-cols-1 xl:grid-cols-[minmax(0,1.05fr)_minmax(0,1fr)] gap-5 items-start">
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
                  用 GridRow 而不是灰掉的 Switch —— 灰开关会让人一直点它。 */}
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

              {/* 远程位置：只读摘要。来源（全局 / 本游戏已存）标出来，
                  用户才知道该去哪儿改 —— 见文件头「一个控件只有一个家」。 */}
              <div className="mt-4">
                <span className="block panel-tiny font-semibold text-zinc-400 mb-1.5">{t('game.remote_location', '远程位置')}</span>
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
              </div>

              {/* 唯一的「去改配置」出口。单游戏这边一个可编辑控件都没有，
                  不给这条提示的话用户会在弹窗里找不到开关干瞪眼。 */}
              <p className="panel-tiny text-zinc-500 mt-2">
                {t('game.remote_global_hint', '以上开关与远程位置都在「设置 → 存档配置」里统一设置，对所有游戏生效。')}
              </p>

              <div className="flex flex-wrap items-center gap-2 mt-4">
                {/* [CN-FORK] v0.5.2.142 立即推 / 拉。**不受总开关影响** ——
                    手动就是手动，用户想试一次不该被迫先把自动打开。后端读的是它自己
                    那份已保存的配置（全局或本游戏的），与这里的只读展示同源。 */}
                <button
                  type="button"
                  className="btn btn-outline"
                  disabled={!!syncing}
                  onClick={() => onSync('pull')}
                >
                  <CloudDownload className="w-4 h-4" />
                  <span>{syncing === 'pull' ? t('game.syncing', '同步中…') : t('game.sync_pull', '立即拉取')}</span>
                </button>
                <button
                  type="button"
                  className="btn btn-outline"
                  disabled={!!syncing}
                  onClick={() => onSync('push')}
                >
                  <CloudUpload className="w-4 h-4" />
                  <span>{syncing === 'push' ? t('game.syncing', '同步中…') : t('game.sync_push', '立即推送')}</span>
                </button>
              </div>

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

            {/* ── ② 存档列表 ── */}
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
        )}

        {/* ── ③ 本地快照（v0.5.2.140）──
         * 与上面两块并排会太挤（配置 + 列表已经占满两列），所以单独一行铺开。
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
            {/* 参数只读：开关 / 间隔 / 保留数都在设置页「存档配置」里改。
                「立即备份」与下面的列表留在这里 —— 备份的是**这个游戏**的存档，
                本来就属于这个弹窗。 */}
            <GridRow
              cols={RO_COLS}
              label={t('game.snap_enable', '启用本地快照')}
              value={eff.snap.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}
            />
            <GridRow
              cols={RO_COLS}
              label={t('game.snap_interval', '间隔（分钟）')}
              value={String(Math.max(1, Math.round((Number(eff.snap.interval) || 0) / 60)))}
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
          </Block>
        )}

        <input
          ref={fileRef}
          type="file"
          accept=".zip,.tar,.gz,.tgz"
          className="hidden"
          onChange={onFile}
        />
      </div>
    </Modal>
  )
}
