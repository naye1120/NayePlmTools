import React, { useState, useEffect, useRef, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import {
  Save, RefreshCw, Download, Upload, Trash2, Plug, AlertTriangle, HardDrive, FileArchive,
  History, RotateCcw,
} from 'lucide-react'
import Modal from '../ui/Modal'
import Switch from '../ui/Switch'
import Segmented from '../ui/Segmented'
import { Block, Chip, Field, Note, MiniEmpty, ProgressBar } from './parts'
import { formatSizeAlways, formatStamp } from './format'
import {
  getSaves, saveAutoConfig, testRemote, saveDownloadUrl, uploadSave, deleteSave, snapAction,
} from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.112 存档管理弹窗（磁贴第一颗按钮）
 *
 * 用户口径：「第一个代表，存档管理，最终功能就是可以上传下载存档，还可以配置远程自动存档路径
 * 和自动存档开关。」其中「远程」是三种协议都要（SMB / FTP / HTTP）。
 *
 * 三块内容：
 *   ① 自动存档开关 —— 总开关 + 两个时机（启动时拉取 / 退出时回传）。分成三个而不是一个，
 *      是因为这三种组合在玩家侧都有真实用例（只在网吧式机器上回传、只在多机之间拉取）。
 *   ② 远程路径配置 —— SMB / FTP / HTTP 三选一，各自字段不同。用 Segmented 而不是下拉，
 *      因为电视端方向键在分段控件上左右切换比在下拉里翻更省操作。
 *   ③ 存档列表 —— 下载 / 上传覆盖 / 删除，外加「上传新存档」。
 *
 * 密码字段的处理是这块最要紧的一处：后端**永远不回传密码明文**，只回 `has_password`。
 * 所以输入框留空 = 「不修改」，而不是「清空」；要清空得显式点「清除密码」。
 * 如果留空就当清空，用户改个主机地址就会顺手把密码抹掉，而且不会有任何提示。
 *
 * 本轮只做前端：所有请求打到 mock。真机上后端未实现时，弹窗会显示「主机上没有这个接口」，
 * 这是**预期行为**（接口契约见 utils/gameApi.js 顶部），不是 bug。
 */

const MODES = [
  { value: 'smb', label: 'SMB' },
  { value: 'ftp', label: 'FTP' },
  { value: 'http', label: 'HTTP' },
]

/* 每种协议各自的字段定义。写在一张表里而不是三段 JSX：
 * 三种协议的字段名、类型、占位符、是否密码，全在这里一处可查，加协议只改这张表。 */
const MODE_FIELDS = {
  smb: [
    { key: 'host', label: '主机地址', ph: '192.168.1.10' },
    { key: 'share', label: '共享名', ph: 'ps5save' },
    { key: 'sub', label: '子路径', ph: 'PPSA01234' },
    { key: 'user', label: '用户名', ph: 'ps5' },
    { key: 'password', label: '密码', ph: '留空表示不修改', secret: true },
  ],
  ftp: [
    { key: 'host', label: '主机地址', ph: '192.168.1.10' },
    { key: 'port', label: '端口', ph: '2121', numeric: true },
    { key: 'path', label: '远程路径', ph: '/ps5save/PPSA01234' },
    { key: 'user', label: '用户名', ph: 'anonymous' },
    { key: 'password', label: '密码', ph: '留空表示不修改', secret: true },
  ],
  http: [
    { key: 'url', label: '上传地址', ph: 'http://192.168.1.10:8099/save' },
    { key: 'token', label: '访问令牌', ph: '留空表示不修改', secret: true },
  ],
}

const EMPTY_CFG = {
  enabled: false,
  mode: 'smb',
  on_launch: true,
  on_exit: true,
  smb: { host: '', share: '', sub: '', user: '', password: '' },
  ftp: { host: '', port: '', path: '', user: '', password: '' },
  http: { url: '', token: '' },
  /* [CN-FORK] v0.5.2.140 本地快照。默认值必须与后端 gs_auto_defaults 一致。 */
  snap: { enabled: false, interval: 1800, keep: 5 },
}

/* 后端 → 本地编辑态。密码一律清空（后端不发明文），是否已保存由 has_password 标记。 */
function toLocalCfg(auto) {
  if (!auto) return { ...EMPTY_CFG }
  return {
    enabled: !!auto.enabled,
    mode: auto.mode || 'smb',
    on_launch: auto.on_launch !== false,
    on_exit: auto.on_exit !== false,
    smb: { ...EMPTY_CFG.smb, ...(auto.smb || {}), password: '' },
    ftp: { ...EMPTY_CFG.ftp, ...(auto.ftp || {}), password: '' },
    http: { ...EMPTY_CFG.http, ...(auto.http || {}), token: '' },
    snap: { ...EMPTY_CFG.snap, ...(auto.snap || {}) },
  }
}

/* 本地编辑态 → 后端。**空字符串的密码/token 一律剔除**（见文件头：留空 = 不修改）。
 * 其它字段照发，包括空串 —— 用户把共享名清空是有意义的（改回根目录）。 */
function toRemoteCfg(cfg) {
  const out = {
    enabled: cfg.enabled,
    mode: cfg.mode,
    on_launch: cfg.on_launch,
    on_exit: cfg.on_exit,
    snap: {
      enabled: !!cfg.snap.enabled,
      interval: Number(cfg.snap.interval) || EMPTY_CFG.snap.interval,
      keep: Number(cfg.snap.keep) || EMPTY_CFG.snap.keep,
    },
  }
  Object.keys(MODE_FIELDS).forEach((m) => {
    out[m] = { ...cfg[m] }
    MODE_FIELDS[m].forEach((f) => {
      if (f.secret && !out[m][f.key]) delete out[m][f.key]
    })
  })
  return out
}

export default function SaveManagerModal({ show, game, onClose, addToast, showConfirm }) {
  const { t } = useTranslation()
  const tid = game && game.tid
  const [data, setData] = useState(null)
  const [err, setErr] = useState('')
  const [cfg, setCfg] = useState(EMPTY_CFG)
  const [saving, setSaving] = useState(false)
  const [testing, setTesting] = useState(false)
  const [testResult, setTestResult] = useState(null)
  const [busyId, setBusyId] = useState('')
  const [uploading, setUploading] = useState(null)   // {name, percent|null}
  const [snapping, setSnapping] = useState(false)    // 「立即备份」进行中
  const [busySnap, setBusySnap] = useState('')       // `${uid}/${ts}`，该行正在还原/删除
  /* 间隔用**分钟**给用户看/填，但存的是秒（后端口径）。做成独立的文本 state：
   * 直接由 cfg.snap.interval 反推的话，用户清空输入框准备重填时会被立刻弹回原值。 */
  const [snapMin, setSnapMin] = useState('30')
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
      setCfg(toLocalCfg(d.auto))
      const iv = (d.auto && d.auto.snap && d.auto.snap.interval) || EMPTY_CFG.snap.interval
      setSnapMin(String(Math.max(1, Math.round(iv / 60))))
      setErr('')
    }
    if (failed) setErr(failed)
  }, [tid, t])

  /* 打开 / 换游戏时把配置、列表、测试结果、上传状态全部归零。
   * 写在**渲染期**（键为 tid）而不是 effect 里：effect 体里同步 setState 会引发级联渲染，
   * 而「prop 变了就把这几个 state 重置」本来就属于 React 官方说的「渲染期调整 state」。 */
  const openKey = show && tid ? tid : ''
  const [loadedKey, setLoadedKey] = useState('')
  if (openKey !== loadedKey) {
    setLoadedKey(openKey)
    setData(null); setErr(''); setCfg(EMPTY_CFG)
    setTestResult(null); setBusyId(''); setUploading(null); setSaving(false); setTesting(false)
    setSnapping(false); setBusySnap(''); setSnapMin('30')
  }

  useEffect(() => {
    if (!show || !tid) return
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开/换游戏时拉一次存档列表与自动存档配置
    load()
  }, [show, tid, load])

  if (!show || !game) return null

  const saves = (data && data.saves) || []
  const snapshots = (data && data.snapshots) || []
  const mode = cfg.mode
  const fields = MODE_FIELDS[mode] || []
  const modeCfg = cfg[mode] || {}
  const hasStored = (f) => !!(data && data.auto && data.auto[mode] && data.auto[mode][`has_${f.key}`])

  const setModeField = (key, val) => setCfg((c) => ({ ...c, [mode]: { ...c[mode], [key]: val } }))

  const onSaveCfg = async () => {
    setSaving(true)
    setTestResult(null)
    try {
      await saveAutoConfig(tid, toRemoteCfg(cfg))
      addToast?.(t('game.auto_saved', '自动存档设置已保存'))
      await load()
    } catch (e) {
      addToast?.(e.message || t('game.auto_save_fail', '保存失败'), 'error')
    }
    setSaving(false)
  }

  const onTest = async () => {
    setTesting(true)
    setTestResult(null)
    try {
      const r = await testRemote(tid, toRemoteCfg(cfg))
      if (aliveRef.current) setTestResult({ ok: true, latency: r && r.latency_ms })
    } catch (e) {
      if (aliveRef.current) setTestResult({ ok: false, message: e.message })
    }
    setTesting(false)
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

  /* 间隔输入框：值是分钟，改一次就同步回秒。解析不出来（用户清空）就只改文本，
   * 不把 cfg 带坏 —— 清空再重填是再正常不过的操作。 */
  const onSnapMin = (v) => {
    setSnapMin(v)
    const m = parseInt(v, 10)
    if (Number.isFinite(m) && m > 0) {
      setCfg((c) => ({ ...c, snap: { ...c.snap, interval: Math.min(1440, m) * 60 } }))
    }
  }

  return (
    <Modal
      show={show}
      title={t('game.saves_title', '存档管理')}
      onClose={onClose}
      layout="panel"
    >
      {/* [CN-FORK] v0.5.2.112 面板形态（用户：「存档管理页面打开后铺满右边整个区域」）。
       * 两块内容在宽屏上并排：左边是「配置」（开关 + 远程位置），右边是「存档列表」。
       * 这两件事本来就是**一边配置一边对着列表看**的关系（比如照着实测路径填 SMB 子路径），
       * 竖着排会让用户在两屏之间来回滚。窄屏（<1280px）自动退回单列。 */}
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
            {/* ── ① 自动存档 ── */}
            <Block
              className="min-w-0"
              title={t('game.auto_title', '自动存档')}
              icon={Save}
              actions={<Chip tone={cfg.enabled ? 'ok' : 'muted'}>{cfg.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}</Chip>}
            >
              <div className="flex items-center justify-between gap-4">
                <div className="min-w-0">
                  <p className="text-white panel-body font-semibold">{t('game.auto_enable', '启用自动存档')}</p>
                  <p className="text-zinc-500 panel-tiny mt-0.5">
                    {t('game.auto_enable_hint', '开关打开后，主机按下面的时机自动与远程位置同步这个游戏的存档。')}
                  </p>
                </div>
                <Switch
                  checked={cfg.enabled}
                  onChange={(v) => setCfg((c) => ({ ...c, enabled: v }))}
                  label={t('game.auto_enable', '启用自动存档')}
                />
              </div>

              {/* 同步时机：两个独立开关，可以只开一个 */}
              <div className={cfg.enabled ? 'mt-4' : 'mt-4 opacity-50 pointer-events-none'}>
                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                  <TimingRow
                    label={t('game.auto_on_launch', '启动游戏时拉取')}
                    hint={t('game.auto_on_launch_hint', '把远程的存档下载到主机，覆盖本地')}
                    checked={cfg.on_launch}
                    onChange={(v) => setCfg((c) => ({ ...c, on_launch: v }))}
                  />
                  <TimingRow
                    label={t('game.auto_on_exit', '退出游戏时回传')}
                    hint={t('game.auto_on_exit_hint', '把主机的存档上传到远程位置')}
                    checked={cfg.on_exit}
                    onChange={(v) => setCfg((c) => ({ ...c, on_exit: v }))}
                  />
                </div>

                <div className="mt-4">
                  <span className="block panel-tiny font-semibold text-zinc-400 mb-1.5">{t('game.remote_kind', '远程位置类型')}</span>
                  <Segmented
                    options={MODES}
                    value={mode}
                    onChange={(v) => { setCfg((c) => ({ ...c, mode: v })); setTestResult(null) }}
                    label={t('game.remote_kind', '远程位置类型')}
                    className="w-full sm:w-auto"
                  />
                </div>

                <div className="grid grid-cols-1 sm:grid-cols-2 gap-3 mt-4">
                  {fields.map((f) => (
                    <Field
                      key={f.key}
                      label={f.label}
                      className={f.key === 'path' || f.key === 'url' || f.key === 'sub' ? 'sm:col-span-2' : undefined}
                      hint={f.secret && hasStored(f) ? t('game.secret_saved', '已保存过；留空则保持不变') : undefined}
                    >
                      <input
                        className="field"
                        type={f.secret ? 'password' : 'text'}
                        inputMode={f.numeric ? 'numeric' : undefined}
                        autoComplete="off"
                        placeholder={f.ph}
                        value={modeCfg[f.key] || ''}
                        onChange={(e) => setModeField(f.key, e.target.value)}
                      />
                    </Field>
                  ))}
                </div>

                <div className="flex flex-wrap items-center gap-2 mt-4">
                  <button type="button" className="btn btn-primary" disabled={saving} onClick={onSaveCfg}>
                    {saving ? t('game.saving', '保存中…') : t('game.save_settings', '保存设置')}
                  </button>
                  <button type="button" className="btn btn-outline" disabled={testing} onClick={onTest}>
                    <Plug className="w-4 h-4" />
                    <span>{testing ? t('game.testing', '测试中…') : t('game.test_conn', '测试连接')}</span>
                  </button>
                </div>

                {testResult && (
                  <Note
                    tone={testResult.ok ? 'ok' : 'error'}
                    className="mt-3"
                  >
                    {testResult.ok
                      ? t('game.test_ok', '连接成功{{lat}}', { lat: testResult.latency ? `（${testResult.latency} ms）` : '' })
                      : t('game.test_fail', '连接失败：{{why}}', { why: testResult.message })}
                  </Note>
                )}
              </div>
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
                <Chip tone={cfg.snap.enabled ? 'ok' : 'muted'}>
                  {cfg.snap.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}
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
            <div className="flex items-center justify-between gap-4">
              <div className="min-w-0">
                <p className="text-white panel-body font-semibold">{t('game.snap_enable', '启用本地快照')}</p>
                <p className="text-zinc-500 panel-tiny mt-0.5">
                  {t('game.snap_enable_hint',
                    '不依赖网络：主机按下面的间隔把存档整份复制到本地 /data 下，超保留数自动删最旧。远程同步连不上时，这一条是最后的兜底。')}
                </p>
              </div>
              <Switch
                checked={cfg.snap.enabled}
                onChange={(v) => setCfg((c) => ({ ...c, snap: { ...c.snap, enabled: v } }))}
                label={t('game.snap_enable', '启用本地快照')}
              />
            </div>

            <div className={cfg.snap.enabled ? 'mt-4' : 'mt-4 opacity-50 pointer-events-none'}>
              <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
                <Field
                  label={t('game.snap_interval', '间隔（分钟）')}
                  hint={t('game.snap_interval_hint', '最短 1 分钟，最长 1440（一天）。改完记得点保存。')}
                >
                  <input
                    className="field"
                    type="number"
                    min="1"
                    max="1440"
                    inputMode="numeric"
                    value={snapMin}
                    onChange={(e) => onSnapMin(e.target.value)}
                  />
                </Field>
                <Field
                  label={t('game.snap_keep', '保留份数')}
                  hint={t('game.snap_keep_hint', '每份存档最多留这么多份，超了删最旧（1~20）。')}
                >
                  <input
                    className="field"
                    type="number"
                    min="1"
                    max="20"
                    inputMode="numeric"
                    value={cfg.snap.keep}
                    onChange={(e) => setCfg((c) => ({ ...c, snap: { ...c.snap, keep: e.target.value } }))}
                  />
                </Field>
              </div>

              <div className="flex flex-wrap items-center gap-3 mt-4">
                <button type="button" className="btn btn-primary" disabled={saving} onClick={onSaveCfg}>
                  {saving ? t('game.saving', '保存中…') : t('game.save_settings', '保存设置')}
                </button>
                {data.snap_used > 0 && (
                  <span className="panel-tiny text-zinc-500">
                    {t('game.snap_used', '快照已占用 {{size}}', { size: formatSizeAlways(data.snap_used) })}
                  </span>
                )}
              </div>
            </div>

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

/* 同步时机的一行：标题 + 说明 + 开关。做成一行而不是两个 Field，
 * 因为「说明」在电视端必须跟着开关走，否则用户看不出这两个开关的区别。 */
function TimingRow({ label, hint, checked, onChange }) {
  return (
    <div className="flex items-center justify-between gap-3 p-2.5 rounded-lg border border-white/10 bg-white/[0.02]">
      <div className="min-w-0">
        <p className="text-white panel-note font-semibold">{label}</p>
        <p className="text-zinc-500 panel-tiny mt-0.5 leading-snug">{hint}</p>
      </div>
      <Switch checked={checked} onChange={onChange} label={label} />
    </div>
  )
}
