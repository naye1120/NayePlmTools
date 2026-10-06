import React, { useState, useEffect, useRef, useCallback, useMemo } from 'react'
import { useTranslation } from 'react-i18next'
import {
  Image as ImageIcon, RefreshCw, Download, CheckSquare, Square, X,
  Play, Film, AlertTriangle, PackageOpen, Trash2, ChevronLeft, ChevronRight,
} from 'lucide-react'
import Modal from '../ui/Modal'
import Segmented from '../ui/Segmented'
import { Block, Chip, Note, MiniEmpty, ProgressBar, Stat } from './parts'
import { formatSizeAlways, formatDuration, formatRate, formatElapsed, formatStamp } from './format'
import { getMedia, mediaThumbUrl, mediaFileUrl, packMedia, mediaPackUrl, deleteMedia, watchJob, cancelJob } from '../../utils/gameApi'

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
  cancelled: ['game.phase_cancelled', '已取消'],
}
function phaseText(phase, t) {
  const e = PHASE_TEXT[phase]
  return e ? t(e[0], e[1]) : ''
}

export default function MediaManagerModal({ show, game, onClose, addToast, showConfirm }) {
  const { t } = useTranslation()
  const tid = game && game.tid
  const [data, setData] = useState(null)
  const [err, setErr] = useState('')
  const [tab, setTab] = useState('all')
  const [sel, setSel] = useState(() => new Set())
  const [preview, setPreview] = useState(null)      // 预览中的下标
  const [job, setJob] = useState(null)              // 打包任务
  const [packed, setPacked] = useState(null)        // {job_id, count} 打包完成后可下载
  const [busyId, setBusyId] = useState('')
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
    setPacked(null)
  }

  useEffect(() => {
    if (!show || !tid) return
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开/换游戏时拉一次截图与视频列表
    load()
  }, [show, tid, load])

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

  /* 打包：ids 传 null = 全量。任务完成后 packed 记下 job_id，供 mediaPackUrl 用。 */
  const startPack = async (ids) => {
    setPacked(null)
    try {
      const r = await packMedia(tid, ids)
      if (!r || !r.job_id) {
        addToast?.(t('game.pack_started', '打包已开始'), 'success')
        return
      }
      setJob({ job_id: r.job_id, phase: 'preparing', percent: 0, active: true, done: false })
      if (stopJobRef.current) stopJobRef.current()
      stopJobRef.current = watchJob(r.job_id, (st) => {
        if (!aliveRef.current) return
        /* 失联时 onTick 拿到的是一个只有 job_id/error 的残缺对象（见 watchJob）。
         * 直接 setJob(st) 会把已经量出来的体积/文件数抹成 `—`、进度条退回不确定态 ——
         * 恰恰在最需要「最后停在哪」的时候把证据丢掉。所以只在 transient 时做合并，
         * 正常 tick 一律整体替换。 */
        setJob((prev) => (st.transient && prev ? { ...prev, ...st } : st))
        if (st.done) {
          if (stopJobRef.current) { stopJobRef.current(); stopJobRef.current = null }
          /* name 是后端定下的成品文件名（游戏名称-打包时间戳.zip）。存下来给
           * <a download> 用 —— 服务端也下发了同名 Content-Disposition，两边同源。 */
          if (st.ok) setPacked({ job_id: r.job_id, count: ids ? ids.length : items.length, name: st.name || '' })
          else if (st.error && !st.transient) addToast?.(t('game.pack_fail', '打包失败：{{why}}', { why: st.error }), 'error')
        }
      })
    } catch (e) {
      addToast?.(e.message || t('game.pack_fail_generic', '打包失败'), 'error')
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

  const packing = !!(job && !job.done)

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
            <button
              type="button"
              className="btn btn-outline"
              disabled={items.length === 0 || packing}
              onClick={() => startPack(null)}
            >
              <PackageOpen className="w-4 h-4" />
              <span>{t('game.pack_all', '全量打包下载')}</span>
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
                        {job.lost
                          ? t('game.job_lost_title', '与主机失联')
                          : job.done
                            ? (job.ok ? t('game.pack_done', '打包完成') : t('game.pack_failed', '打包失败'))
                            : t('game.packing', '正在打包…')}
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
                  {/* 卡住与否交给**时间**说话：idle 是后端算的「距上次有进展多少秒」。
                   * 没有它，一个不动的进度条分不清「在啃大文件」和「线程早没了」。 */}
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
                          <img
                            src={mediaThumbUrl(it.id, 320)}
                            alt=""
                            loading="lazy"
                            className="w-full h-full object-cover"
                            onError={(e) => { e.currentTarget.style.visibility = 'hidden' }}
                          />
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
          busyId={busyId}
          tid={tid}
          t={t}
        />
      )}
    </>
  )
}

/* 预览层：图片直接铺满，视频用原生 <video controls>（自带播放/进度/全屏）。
 * 左右方向键切换上一张/下一张 —— 遥控器上这是最自然的翻页方式。 */
function Lightbox({ items, index, onIndex, onClose, onDelete, busyId, tid, t }) {
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
