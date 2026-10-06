import React, { useState, useEffect, useRef, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import {
  Gamepad2, RefreshCw, HardDrive, Trash2, Power, PowerOff, PackageX,
  AlertTriangle, Play, Image as ImageIcon, FolderOpen,
} from 'lucide-react'
import Modal from '../ui/Modal'
import { formatSizeAlways, formatRate, formatElapsed, formatStamp } from './format'
import { Chip, GridRow, Stat, ProgressBar, Note } from './parts'
import {
  getGameDetail, deleteGame, watchJob, cancelJob,
  mountGame, unmountGame, uninstallGame, GameApiError,
} from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.112 游戏详情弹窗（磁贴点开）
 *
 * 参考实现是 ShadowMountPlus 1.7 的 `web/index.html`（用户口径：「具体参考 showdmount」——
 * 指的就是它那个自带 Web UI，监听 127.0.0.1:10101，详情走 /api/v1/games/info、删除走
 * /api/v1/games/delete）。照抄的是**信息层级与删除语义**，不是它的外观：
 *   · 左封面 + 右明细的左右分栏，明细是「字段名 / 值」两列网格；
 *   · 删除是**异步任务**（202 + job_id），有进度、可取消、可失败；
 *   · 删除有**三种非失败结局**要分开报：源已不存在（source_missing）、主机正忙（409 EBUSY）、
 *     已安装 PKG 没有可删的物理源（403 EPERM）—— 混成一句「删除失败」是最难排查的那种错。
 * 外观一律按本 fork 的契约来（.panel-card / .btn / .icon-btn / 语义色档），不引它的 CSS。
 *
 * 三个刻意的取舍：
 * ① 弹窗**恒可关闭**，删除进行中也能关。关掉只是停止轮询，主机侧的后台任务照跑 ——
 *    v0.5.2.107 踩过「busy 期间锁死弹窗 = 主机一停用户被永久关在里面」这个坑，不再重犯。
 *    关闭时给一条提示说明任务还在跑，而不是假装什么都没发生。
 * ② 删除的确认走 App 级 showConfirm（danger 档），与本项目其余破坏性操作同一条路径，
 *    不自己造第二套确认弹窗。
 * ③ 「存档 / 截图」两格是可点的，直接跳到对应管理弹窗 —— 详情页里看到数字却过不去，
 *    是最没意义的只读。
 */

/* 明细网格的一行。值为空显示「—」：空着会让人分不清「没有」还是「没读到」。
 * v0.5.2.112 起这个形状与另外三个弹窗共用（见 ./parts.jsx 的 GridRow）。 */
const ROW_COLS = 'grid-cols-[86px_minmax(0,1fr)] sm:grid-cols-[104px_minmax(0,1fr)]'

/* 任务阶段 → 中文。后端的 phase 是英文机器码，这里必须翻成人话（v0.5.2.111 口径：
 * 「别报代码了」）。查不到的 phase 原样显示，不做兜底猜测 —— 猜错比露出机器码更难查。 */
const PHASE_LABEL = {
  idle: '准备中',
  preparing: '准备中',
  measuring: '正在统计体积',
  deleting: '正在删除',
  finalizing: '正在收尾',
  done: '已完成',
  failed: '已失败',
  cancelled: '已取消',
}
/* 主机正忙的细分原因 → 中文。这几个来自 ShadowMountPlus 的 error_reason 集合，
 * 后端能分清就一定要分清：用户看到「游戏还在运行」和「主机在待机」要做的事完全不同。 */
const BUSY_REASON = {
  game_active: '这个游戏还在运行，请先在主机上完全退出游戏',
  runtime_prepared: '另一个游戏正占着运行时挂载，请稍后再试',
  rest_mode: '主机正处于待机状态，请先唤醒',
  install_pending: '有安装任务正在进行，请等它结束',
  mutation_gate: '另一个改动操作正在进行，请稍后再试',
  storage_busy: '另一个存储任务正在进行，请等它结束',
}

export default function GameDetailModal({ show, game, onClose, addToast, showConfirm, onOpenSaves, onOpenMedia, onChanged }) {
  const { t } = useTranslation()
  const tid = game && game.tid
  const [detail, setDetail] = useState(null)
  const [err, setErr] = useState('')
  const [busy, setBusy] = useState('')          // '' | 'runtime' | 'uninstall'
  const [job, setJob] = useState(null)          // 删除任务进度
  const stopJobRef = useRef(null)
  const aliveRef = useRef(true)

  /* 关掉弹窗 / 组件卸载时都要停掉轮询，否则会往已卸载的组件里 setState。
   * （React 18 之后不再警告，但泄漏的定时器会一直打主机，必须收干净。） */
  const stopWatch = useCallback(() => {
    if (stopJobRef.current) { stopJobRef.current(); stopJobRef.current = null }
  }, [])

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false; stopWatch() }
  }, [stopWatch])

  /* 每次打开都重新拉一次详情：游戏的状态（挂载/存档数）在主机侧随时会变，
   * 缓存上一次的会让用户看到过期数据。
   *
   * 「打开/换游戏时先把上一次的数据清掉」写在**渲染期**而不是 effect 里 —— 这是 React 官方
   * 对「prop 变化时调整 state」给的做法，同时也满足 react-hooks/set-state-in-effect：
   * effect 体里同步 setState 会引发级联渲染（先渲染旧数据、再渲染空、再渲染新的）。
   * 键用 tid 而不是 game 对象：列表刷新后 game 会是新对象，用对象当键会把用户正在看的
   * 详情整个清掉（而且看不出为什么）。 */
  const openKey = show && tid ? tid : ''
  const [loadedKey, setLoadedKey] = useState('')
  if (openKey !== loadedKey) {
    setLoadedKey(openKey)
    setDetail(null)
    setErr('')
    setJob(null)
  }

  const load = useCallback(async () => {
    if (!tid) return
    /* 第一句就 await：从 effect 里调用时函数体内不会出现「同步 setState」。
     * 顺带把两种失败合并到一处处理，不用 try/catch 包住两个 setState。 */
    let d = null
    let failed = ''
    try {
      d = await getGameDetail(tid)
    } catch (e) {
      failed = e.message || t('game.detail_fail', '读取游戏详情失败')
    }
    if (!aliveRef.current) return
    if (d) setDetail(d)
    if (failed) setErr(failed)
  }, [tid, t])

  useEffect(() => {
    if (!show || !tid) return
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开/换游戏时拉一次详情
    load()
  }, [show, tid, load])

  /* 关掉弹窗就停轮询（不 setState，任务面板的清理由上面的渲染期重置负责） */
  useEffect(() => {
    if (!show) stopWatch()
  }, [show, stopWatch])

  if (!show || !game) return null

  /* 列表里已有的字段先顶上，详情到了再覆盖 —— 这样弹窗一打开就有封面和名字，
   * 不会先闪一片空白再填内容。 */
  const g = detail || game
  const installed = g.src === 'installed'
  const platform = g.platform || (String(g.tid || '').toUpperCase().slice(0, 4) === 'CUSA' ? 'PS4' : 'PS5')
  const sourceType = g.source_type || (installed ? 'pkg' : g.fmt === 'folder' ? 'folder' : 'image')
  const SOURCE_LABEL = { pkg: t('game.src_pkg', '已安装 PKG'), image: t('game.src_image', '镜像'), folder: t('game.src_folder', '文件夹') }
  /* can_manage_source 由**后端**判定（ShadowMountPlus 同名字段：这个游戏的物理源能不能挂载/删除）。
   * 这里刻意**不**再用 `!installed_pkg` 覆盖一遍 —— 把后端的判断复制到前端，两边一定会漂移，
   * 而漂移的后果二选一：要么界面上压根没有删除按钮（功能形同不存在），要么点了必然 403。
   * 详情还没回来时一律不显示：宁可晚 200ms 出现，也不先亮一个可能错的按钮。 */
  const canManageSource = !!detail && detail.can_manage_source !== false
  const canUninstall = g.can_uninstall !== false

  /* 删除：确认 → 发起 → 轮询。三种「非失败结局」分开报（见文件头注释）。 */
  const doDelete = () => {
    if (!showConfirm) return
    showConfirm(
      t('game.delete_title', '删除物理源'),
      t('game.delete_confirm', '永久删除「{{name}}」的物理源文件？\n\n此操作无法撤销。', { name: g.name || tid }),
      async () => {
        try {
          const r = await deleteGame(tid)
          if (r && r.source_missing) {
            addToast?.(t('game.delete_missing', '源文件已不存在，已排队重新扫描资料库'))
            onChanged?.()
            onClose?.()
            return
          }
          if (r && r.job_id) {
            setJob({ job_id: r.job_id, phase: 'preparing', percent: 0, active: true, done: false })
            stopWatch()
            stopJobRef.current = watchJob(r.job_id, (st) => {
              if (!aliveRef.current) return
              /* 失联时 onTick 拿到的是残缺对象（见 watchJob 的注释）：整体替换会把
               * 已经量出来的体积/文件数抹掉，所以 transient 时做合并。 */
              setJob((prev) => (st.transient && prev ? { ...prev, ...st } : st))
              if (st.done) {
                stopWatch()
                if (st.ok) {
                  addToast?.(t('game.delete_ok', '「{{name}}」已删除', { name: g.name || tid }))
                  onChanged?.()
                  onClose?.()
                } else if (st.transient) {
                  /* 失联 ≠ 删除失败：任务可能已经跑完了，只是我们问不到。别谎报失败。 */
                  addToast?.(t('game.job_lost', '与主机失联，任务状态未知；请关掉面板重新打开查看'), 'error')
                } else if (st.error) {
                  addToast?.(t('game.delete_fail', '删除失败：{{why}}', { why: st.error }), 'error')
                }
              }
            })
          } else {
            addToast?.(t('game.delete_ok', '「{{name}}」已删除', { name: g.name || tid }))
            onChanged?.()
            onClose?.()
          }
        } catch (e) {
          addToast?.(deleteErrorText(e, t), 'error')
        }
      },
      true,
    )
  }

  const doRuntime = async (op, mode) => {
    setBusy('runtime')
    try {
      if (op === 'unmount') await unmountGame(tid)
      else await mountGame(tid, mode)
      addToast?.(op === 'unmount'
        ? t('game.unmounted', '已卸载运行时挂载')
        : t('game.mounted', '已挂载（{{mode}}）', { mode: mode === 'rw' ? '读写' : '只读' }))
      onChanged?.()
      load()
    } catch (e) {
      addToast?.(e.message || t('game.runtime_fail', '操作失败'), 'error')
    }
    setBusy('')
  }

  const doUninstall = () => {
    if (!showConfirm) return
    showConfirm(
      t('game.uninstall_title', '卸载游戏'),
      installed
        ? t('game.uninstall_pkg_confirm', '卸载「{{name}}」？已安装的 PKG 游戏将被删除。', { name: g.name || tid })
        : t('game.uninstall_confirm', '请求卸载「{{name}}」？\n\n源镜像或文件夹不会被删除。', { name: g.name || tid }),
      async () => {
        setBusy('uninstall')
        try {
          await uninstallGame(tid)
          addToast?.(t('game.uninstall_ok', '已请求卸载，主机将在后台完成'))
          onChanged?.()
          onClose?.()
        } catch (e) {
          addToast?.(busyErrorText(e, t), 'error')
        }
        setBusy('')
      },
      true,
    )
  }

  /* 关窗：删除进行中要先说清楚任务没停，不能让用户以为点了叉就取消了。 */
  const close = () => {
    if (job && !job.done) {
      addToast?.(t('game.delete_bg', '删除已在主机后台继续，可以稍后刷新列表查看'))
    }
    stopWatch()
    onClose?.()
  }

  const deleting = !!(job && !job.done)

  return (
    <Modal
      show={show}
      title={g.name || tid}
      onClose={close}
      className="max-w-3xl"
      footer={
        <div className="flex flex-wrap items-center gap-2 w-full">
          {canManageSource && (
            g.mounted ? (
              <button type="button" className="btn btn-outline" disabled={busy === 'runtime' || deleting} onClick={() => doRuntime('unmount')}>
                <PowerOff className="w-4 h-4" />
                <span>{t('game.unmount', '卸载挂载')}</span>
              </button>
            ) : (
              <>
                <button type="button" className="btn btn-outline" disabled={busy === 'runtime' || deleting} onClick={() => doRuntime('mount', 'ro')}>
                  <Power className="w-4 h-4" />
                  <span>{t('game.mount_ro', '只读挂载')}</span>
                </button>
                <button type="button" className="btn btn-outline" disabled={busy === 'runtime' || deleting} onClick={() => doRuntime('mount', 'rw')}>
                  <Power className="w-4 h-4" />
                  <span>{t('game.mount_rw', '读写挂载')}</span>
                </button>
              </>
            )
          )}
          {canUninstall && (
            <button type="button" className="btn btn-warn" disabled={busy === 'uninstall' || deleting} onClick={doUninstall}>
              <PackageX className="w-4 h-4" />
              <span>{t('game.uninstall', '卸载')}</span>
            </button>
          )}
          {canManageSource && (
            <button type="button" className="btn btn-danger" disabled={deleting} onClick={doDelete}>
              <Trash2 className="w-4 h-4" />
              <span>{t('game.delete_source', '删除源')}</span>
            </button>
          )}
        </div>
      }
    >
      {/* Modal 的 children 容器自带 text-lg/xl（给确认文案用的），详情页是数据密集型，
          在这里显式压回 13px —— 直接写在子元素上的字号会盖掉继承来的那一档。 */}
      <div className="panel-body leading-relaxed font-normal">
        {/* 副标题：TID · 平台 */}
        <p className="text-zinc-500 panel-note font-mono tracking-wider mb-4">
          {tid}{platform ? ` · ${platform}` : ''}
        </p>

        {err && (
          <Note tone="error" icon={AlertTriangle} className="mb-4">{err}</Note>
        )}

        {!detail && !err && (
          <div className="flex items-center gap-3 py-10 justify-center text-zinc-500">
            <RefreshCw className="w-5 h-5 animate-spin" />
            <span>{t('game.detail_loading', '正在读取游戏详情…')}</span>
          </div>
        )}

        {detail && (
          <div className="grid grid-cols-1 md:grid-cols-[168px_minmax(0,1fr)] gap-5 md:gap-6">
            {/* 封面 */}
            <div className="w-[168px] max-w-full">
              <div className="relative w-full aspect-square rounded-xl overflow-hidden bg-white/5 border border-white/10 flex items-center justify-center">
                <Gamepad2 className="w-12 h-12 text-zinc-600 absolute" />
                {/* [CN-FORK] v0.5.2.112 设过自定义图标就显示它（否则详情里看到的和磁贴上不是同一张图）*/}
                <img
                  src={detail.title_icon_custom
                    ? `/game_tile_icon?tid=${encodeURIComponent(tid)}`
                    : `/game_icon?tid=${encodeURIComponent(tid)}`}
                  alt=""
                  className="relative w-full h-full object-cover"
                  onError={(e) => { e.currentTarget.style.display = 'none' }}
                />
              </div>
            </div>

            <div className="min-w-0">
              {/* 徽标行：来源类型 / 镜像格式 / 已挂载 */}
              <div className="flex flex-wrap gap-1.5 mb-4">
                <Chip tone="info">{SOURCE_LABEL[sourceType] || sourceType}</Chip>
                {detail.image_type && <Chip tone="muted">{String(detail.image_type).toUpperCase()}</Chip>}
                {detail.mounted && <Chip tone="ok">{t('game.badge_mounted', '已挂载')}</Chip>}
                {detail.title_icon_custom && <Chip tone="muted">{t('game.badge_custom_icon', '自定义图标')}</Chip>}
                {detail.title_bg_custom && <Chip tone="muted">{t('game.badge_custom_bg', '自定义背景')}</Chip>}
              </div>

              <div className="space-y-1">
                <GridRow cols={ROW_COLS} label={t('game.f_tid', '标题 ID')} value={tid} mono />
                <GridRow cols={ROW_COLS} label="Content ID" value={detail.content_id} mono />
                <GridRow cols={ROW_COLS} label={t('game.f_version', '版本')} value={detail.version} />
                <GridRow cols={ROW_COLS} label={t('game.f_source_type', '来源类型')} value={SOURCE_LABEL[sourceType] || sourceType} />
                <GridRow cols={ROW_COLS} label={t('game.f_path', '物理路径')} mono value={detail.path} />
                <GridRow cols={ROW_COLS} label={t('game.f_runtime_path', '运行时路径')} mono value={detail.runtime_path} />
                <GridRow cols={ROW_COLS} label={t('game.f_size', '体积')} value={formatSizeAlways(detail.size)} />
                <GridRow cols={ROW_COLS} label="app.db" value={formatSizeAlways(detail.appdb_size)} />
                <GridRow cols={ROW_COLS} label={t('game.f_installed', '安装时间')} value={formatStamp(detail.install_time)} />
                <GridRow cols={ROW_COLS} label={t('game.f_last_launch', '最近启动')} value={formatStamp(detail.last_launch)} />
                <GridRow
                  cols={ROW_COLS}
                  label={t('game.f_saves', '存档')}
                  value={detail.saves_count ? t('game.count_items', '{{n}} 个', { n: detail.saves_count }) : ''}
                  onOpen={onOpenSaves ? () => onOpenSaves(g) : undefined}
                  openLabel={t('game.open_saves', '打开存档管理')}
                />
                <GridRow
                  cols={ROW_COLS}
                  label={t('game.f_media', '截图 / 视频')}
                  value={detail.media_count ? t('game.count_items', '{{n}} 个', { n: detail.media_count }) : ''}
                  onOpen={onOpenMedia ? () => onOpenMedia(g) : undefined}
                  openLabel={t('game.open_media', '打开多媒体管理')}
                />
              </div>
            </div>
          </div>
        )}

        {/* 删除任务进度面板（结构照 ShadowMountPlus 的 transfer panel：阶段 / 百分比 /
            进度条 / 体积 / 文件数 / 速率 / 用时 / 取消） */}
        {job && (
          <div className="mt-5 p-4 rounded-xl border border-white/10 bg-white/[0.03]">
            <div className="flex items-center justify-between gap-3 mb-2">
              <strong className="text-white panel-body">
                {job.lost
                  ? t('game.job_lost_title', '与主机失联')
                  : job.done
                    ? (job.ok ? t('game.job_done', '删除完成') : t('game.job_failed', '删除失败'))
                    : (PHASE_LABEL[job.phase] || job.phase || t('game.job_running', '处理中'))}
              </strong>
              <span className="text-zinc-400 tabular-nums panel-note">
                {job.done ? '' : `${Math.max(0, Math.min(100, Math.round(job.percent || 0)))}%`}
              </span>
            </div>
            {!job.done && (
              /* [CN-FORK] v0.5.2.122 indeterminate 只在**总量还不知道**时用。
               * 挂 phase 上的旧写法会在总量已知后仍然来回跑一条 1/3 宽的脉冲，
               * 而右边百分比写着 0% —— 一条不反映真实进度的动画比一条不动的更误导。 */
              <ProgressBar percent={job.percent} indeterminate={!job.bytes_total} className="mb-3" />
            )}
            <div className="grid grid-cols-2 sm:grid-cols-4 gap-x-4 gap-y-1.5 panel-note">
              <Stat label={t('game.job_bytes', '体积')} value={
                job.bytes_total ? `${formatSizeAlways(job.bytes_done)} / ${formatSizeAlways(job.bytes_total)}` : '—'
              } />
              <Stat label={t('game.job_files', '文件')} value={
                job.files_total ? `${job.files_done || 0} / ${job.files_total}` : '—'
              } />
              <Stat label={t('game.job_rate', '速率')} value={formatRate(job.rate)} />
              <Stat label={t('game.job_elapsed', '用时')} value={formatElapsed(job.elapsed)} />
            </div>
            {!job.done && !job.transient && job.idle >= 20 && (
              <p className="mt-2 text-amber-400 panel-note break-words">
                {t('game.job_stalled', '已 {{n}} 秒没有进展；若长时间不动，可取消任务后重试', { n: job.idle })}
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
            {!job.done && job.cancellable !== false && (
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

        {/* 删除是不可逆的，行动区里再给一句明确的后果说明 —— 与确认弹窗双重告知 */}
        {canManageSource && !deleting && (
          <p className="mt-4 panel-tiny text-zinc-500 flex items-start gap-1.5">
            <FolderOpen className="w-3.5 h-3.5 shrink-0 mt-0.5" />
            <span>
              {t('game.delete_hint', '「删除源」会永久删除物理镜像或文件夹本身；只想让它从主机消失、保留文件，请用「卸载」。')}
            </span>
          </p>
        )}
        {!canManageSource && installed && (
          <p className="mt-4 panel-tiny text-zinc-500 flex items-start gap-1.5">
            <ImageIcon className="w-3.5 h-3.5 shrink-0 mt-0.5" />
            <span>{t('game.pkg_no_source', '这是已安装的 PKG，没有可删除的物理源；要移除请用「卸载」。')}</span>
          </p>
        )}
        {deleting && (
          <p className="mt-4 panel-tiny text-amber-400 flex items-start gap-1.5">
            <AlertTriangle className="w-3.5 h-3.5 shrink-0 mt-0.5" />
            <span>{t('game.delete_running_hint', '删除进行中，此时关闭窗口不会中断主机上的任务。')}</span>
          </p>
        )}
      </div>
    </Modal>
  )
}

/* 删除失败 → 中文。409（主机正忙）与 403（源不可删）是**可预期的正常结局**，
 * 必须给出「下一步该干什么」，不能只说一句失败。
 *
 * 409 走 BUSY_REASON 而不是直接用后端 message：后端只能给一句话，而这里要按 reason
 * 分五种情况给不同的下一步（退出游戏 / 等存储任务 / 退出待机 / 等安装完成…）。
 *
 * 403 反过来 —— **优先用后端 message**。EPERM 的原因不止「没有物理源」一种（源在只读介质上、
 * 在受保护目录里、被系统接管…），前端写死一句话，后端新增一种原因就会被显示错。
 * 这跟本 fork 定下的「后端回中文 message，前端只展示 message」是同一条规矩。 */
function deleteErrorText(e, t) {
  if (e instanceof GameApiError) {
    if (e.status === 409) {
      const why = BUSY_REASON[e.reason]
      return why || e.message || t('game.busy', '主机当前正忙，请稍后再试')
    }
    if (e.status === 403) return e.message || t('game.perm_denied', '这个游戏没有可删除的物理源')
    if (e.status === 404) return t('game.not_found', '主机上找不到这个游戏')
    return e.message || t('game.delete_fail_generic', '删除失败')
  }
  return (e && e.message) || t('game.delete_fail_generic', '删除失败')
}

function busyErrorText(e, t) {
  if (e instanceof GameApiError && e.status === 409) {
    return BUSY_REASON[e.reason] || e.message || t('game.busy', '主机当前正忙，请稍后再试')
  }
  return (e && e.message) || t('game.op_fail', '操作失败')
}
