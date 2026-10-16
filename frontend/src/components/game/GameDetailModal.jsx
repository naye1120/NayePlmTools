import React, { useState, useEffect, useRef, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Gamepad2, RefreshCw, AlertTriangle, ExternalLink, Image as ImageIcon } from 'lucide-react'
import Modal from '../ui/Modal'
import { formatSizeAlways, formatStamp } from './format'
import { Chip, GridRow, Note } from './parts'
import { getGameDetail } from '../../utils/gameApi'
/* [CN-FORK] v0.6.0.177 起 /loadpayload:* 是 POST-only（v0.6.0 安全层），
 * 裸 fetch 走的是 GET，在真机上被方法闸门回 405 —— 症状是「点了没反应」，
 * 而 mock 那侧两条方法都接，掩盖掉了它。 */
import { mutationFetch } from '../../utils/mutationRequest'

/* [CN-FORK] v0.5.2.112 游戏详情弹窗（磁贴点开）
 *
 * 参考实现是 ShadowMountPlus 1.7 的 `web/index.html`（用户口径：「具体参考 showdmount」——
 * 指的就是它那个自带 Web UI，监听 127.0.0.1:10101，详情走 /api/v1/games/info、删除走
 * /api/v1/games/delete）。照抄的是**信息层级**，不是它的外观：
 *   · 左封面 + 右明细的左右分栏，明细是「字段名 / 值」两列网格；
 *   · 明细里每个字段都要能一眼读到，空值显示「—」而不是空着（空着分不清「没有」还是「没读到」）。
 * 外观一律按本 fork 的契约来（.panel-card / .btn / 语义色档），不引它的 CSS。
 *
 * [CN-FORK] v0.5.2.129 弹窗里的操作按钮**全部下线**（只读挂载 / 读写挂载 / 卸载挂载 / 卸载 /
 * 删除源，连带删除任务的进度面板与「取消任务」），只留一颗「去 ShadowMount 管理」跳它自己的
 * 后台。这个弹窗退回**纯只读**的信息面板。
 *
 * 为什么撤：ShadowMount 挂出来的镜像 / 裸目录游戏调 `sceAppInstUtilAppUnInstall` 会先把
 * app.db 的注册和 `/user/app/<TID>/`（连带 mount.lnk）删掉、然后**返回非零**。我们的
 * `game_runtime_uninstall()` 只在 rc==0 时清列表缓存，于是图标在缓存过期前还在、刷新后消失，
 * 而我们**没有挂载 / 重注册的能力**，那颗游戏就找不回来了。既然给不了「挂回去」，就不该把
 * 会把它删掉的按钮放在手边 —— 挂载 / 卸载 / 删除源 这三类操作全部交给 ShadowMount 自己做，
 * 它自己有 API 也有界面。存档 / 截图视频 / 改名称背景 是我们自己的功能，SMP 不做，照旧从
 * 详情里的「打开存档管理」「打开多媒体管理」两行走（改名走磁贴上的那颗按钮）。
 *
 * 两个刻意的取舍：
 * ① 弹窗**恒可关闭**：Esc / 右上角叉。`.129` 之后这里没有任何进行中任务，关掉就是关掉。
 * ② 「存档 / 截图」两格是可点的，直接跳到对应管理弹窗 —— 详情页里看到数字却过不去，
 *    是最没意义的只读。
 *
 * [CN-FORK] v0.5.2.131 「去 ShadowMount 管理」不再直接开链接（它没起时打开就是一块
 * 加载不出的空白页）：先 /smp_status 探活，没起就走上游 /loadpayload: 把它拉起来，
 * 在**按钮里**等探活转活并显示「已等 N 秒」，转活了才把标签页指过去；一直不活就关掉
 * 空白标签，不留打不开的页。做法与 CommonEntriesView 的「灰点条目接管拉起」（.83/.84）
 * 同一套，不是另起一套交互。
 */

/* 明细网格的一行。值为空显示「—」：空着会让人分不清「没有」还是「没读到」。
 * v0.5.2.112 起这个形状与另外三个弹窗共用（见 ./parts.jsx 的 GridRow）。 */
const ROW_COLS = 'grid-cols-[86px_minmax(0,1fr)] sm:grid-cols-[104px_minmax(0,1fr)]'

/* ShadowMount 自带的 Web UI 端口（它的 Library 直接挂在这个端口的根路径上）。
 * 不写 host：用当前页面自己的 host —— 在主机上从 127.0.0.1:8911 打开面板时它自动是
 * 127.0.0.1，从电脑用 192.168.x.x 打开时就是局域网地址，两边都不用配置。 */
const SMP_PORT = 10101

/* [CN-FORK] v0.5.2.131 「去 ShadowMount 管理」的启动链路。全部放模块作用域：组件体内
 * 定义的取数闭包会被 react-hooks/purity 判成「渲染期调用不纯函数」。 */
const SMP_NAME = 'ShadowMountPlus'
/* 拉起后的探活节奏：700ms 一次、最多 45 次（≈31s）。SMP 起进程 + 初始化挂载点比 PKG
 * Manager 慢，所以间隔比 CommonEntriesView 那 500ms 略松。 */
const SMP_PROBE_GAP = 700
const SMP_PROBE_TIMES = 45

const normName = (s) => String(s || '').toLowerCase().replace(/\.elf$/, '').replace(/[^a-z]/g, '')
const prefixHit = (a, b, min) => {
  const x = normName(a), y = normName(b)
  return !!x && !!y && Math.min(x.length, y.length) >= min && (x.startsWith(y) || y.startsWith(x))
}
const getJson = (url) => fetch(url, { cache: 'no-store' }).then((r) => r.json()).catch(() => null)
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

/* /list_payloads 的 payloads 是 "…/<目录>/<文件>.elf"，按目录名与文件名两路匹配。
 * 本面板自身（nayeplmtools*）永远排除，否则可能把面板自己再拉一遍；唯一候选才认，
 * 避免歧义拉起错的程序。版本号/变体差异（1.7beta3 / beta4）由归一化后前缀匹配吸收。 */
const findSmpPath = (data) => {
  const hits = ((data && data.payloads) || []).filter((p) => {
    const seg = String(p).split('/')
    const dir = seg[seg.length - 2] || ''
    return !normName(dir).startsWith('nayeplmtools')
      && (prefixHit(dir, SMP_NAME, 3) || prefixHit(seg[seg.length - 1] || '', SMP_NAME, 3))
  })
  return hits.length === 1 ? hits[0] : ''
}

/* 探活必须问主机，不能浏览器直连 10101：SMP 可能只监听 loopback，电脑端浏览器连不通
 * 不代表主机自己连不通。/smp_status 由主机在 127.0.0.1:10101 上做 TCP 探活后回布尔值。 */
const probeSmp = async () => {
  const s = await getJson(`/smp_status?_=${Date.now()}`)
  return !!(s && s.running)
}

const waitSmpAlive = async () => {
  for (let k = 0; k < SMP_PROBE_TIMES; k++) {
    await sleep(SMP_PROBE_GAP)
    if (await probeSmp()) return true
  }
  return false
}

const smpUrl = () => {
  const raw = window.location.hostname || '127.0.0.1'
  const host = raw.includes(':') ? `[${raw}]` : raw
  return `http://${host}:${SMP_PORT}/`
}

export default function GameDetailModal({ show, game, onClose, onOpenSaves, onOpenMedia }) {
  const { t } = useTranslation()
  const tid = game && game.tid
  const [detail, setDetail] = useState(null)
  const [err, setErr] = useState('')
  /* [CN-FORK] v0.5.2.131 拉起 ShadowMount 的进度：starting=是否在等探活，
   * startAt/now 用来算「已等 N 秒」，phase 决定按钮上写「正在启动」还是「正在等待端口就绪」。 */
  const [smpStarting, setSmpStarting] = useState(false)
  const [smpPhase, setSmpPhase] = useState('')
  const [smpStartAt, setSmpStartAt] = useState(0)
  const [smpNow, setSmpNow] = useState(0)
  const [smpFail, setSmpFail] = useState('')
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false }
  }, [])

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
    setSmpFail('')
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

  /* 拉起期间挂一根本机秒针，只为让「已等 N 秒」自己往前走。不在等就停掉——空转的
   * 1s 定时器在电视端浏览器上也是白烧。依赖抽成布尔量：直接写 `[smpStarting]` 本身
   * 是布尔，没有复杂表达式问题。 */
  useEffect(() => {
    if (!smpStarting) return undefined
    const iv = setInterval(() => setSmpNow(Date.now()), 1000)
    return () => clearInterval(iv)
  }, [smpStarting])

  /* 唯一的动作：跳 ShadowMount 后台（它的 Web UI 就挂在 SMP_PORT 的根路径上）。
   *
   * [CN-FORK] v0.5.2.131 不再直接开链接。SMP 没起时 10101 上什么都没有，打开就是一块
   * 加载不出的空白页——而它常常确实没起（我们面板自己不负责拉起它）。所以先探活，
   * 不活就拉起、在按钮里等探活转活再打开。整条链路的取舍照 CommonEntriesView 的
   * openProgram（.83 通用拉起 / .84 新标签）：
   *   · **点击手势的同步栈里**先开 about:blank —— PS5 老内核里异步的 window.open 会被
   *     拦（.84 那次修过），先占位再慢慢指过去最稳；
   *   · 探活通过才 `tab.location.assign(url)`，失败就 `tab.close()`，不留打不开的空白页；
   *   · **不能**给 window.open 加 'noopener,noreferrer' —— 那样它会返回 null，落到
   *     兜底分支**再开一个**标签，一次点出两个 tab（第一颗还停在 about:blank）。
   *     代价是新 tab 能反向拿到我们的 window，但它只是同机后端，与 .84 一致，可接受。 */
  const openSmp = async () => {
    if (smpStarting) return
    const at = Date.now()
    setSmpFail('')
    setSmpStartAt(at)
    setSmpNow(at)
    setSmpStarting(true)
    setSmpPhase(t('game.smp_checking', '正在检查 ShadowMount 是否已启动…'))
    /* 手势内同步开空窗（见上方注释） */
    const tab = window.open('about:blank', '_blank')
    let go = false
    try {
      if (await probeSmp()) {
        go = true
      } else {
        /* 没起：从 /list_payloads 按名字找 ELF 路径。找不到就不猜程序，直接按普通链接
         * 打开（多半还是空白页，但至少把地址交到用户手里，让他知道去哪儿装 SMP）。 */
        const data = await getJson(`/list_payloads?_=${Date.now()}`)
        const path = findSmpPath(data)
        if (!path) {
          setSmpFail(t('game.smp_not_installed',
            '本机没有找到 ShadowMountPlus，先安装它才能从这里打开。'))
          go = true
        } else {
          setSmpPhase(t('game.smp_launching', '正在启动 ShadowMount…'))
          /* [CN-FORK] v0.6.0.177 必须 POST（方法闸门只收 POST，GET 直接 405）；
           * 带上标记是因为这条路由会过 CSRF 校验，而某些浏览器的同源 GET 不带 Origin。 */
          const res = await mutationFetch(`/loadpayload:${encodeURI(path)}`).catch(() => null)
          if (!res || !res.ok) throw new Error(t('game.smp_start_fail', 'ShadowMount 启动失败'))
          setSmpPhase(t('game.smp_waiting', '已发起启动，正在等待端口就绪…'))
          if (!(await waitSmpAlive()))
            throw new Error(t('game.smp_start_timeout', '已发起启动，但端口迟迟没有就绪'))
          go = true
        }
      }
    } catch (e) {
      setSmpFail((e && e.message) || t('game.smp_start_fail', 'ShadowMount 启动失败'))
    }
    if (aliveRef.current) setSmpStarting(false)
    if (go) {
      const url = smpUrl()
      if (tab) {
        try {
          tab.location.assign(url)
          tab.focus()
        } catch {
          try { tab.close() } catch { /* 关不掉也没关系，下面这个兜底才是真正打开它的那个 */ }
          window.open(url, '_blank')
        }
      } else {
        window.open(url, '_blank')
      }
    } else if (tab) {
      tab.close()
    }
  }

  if (!show || !game) return null

  /* 已等秒数：只在等的时候显示，且至少给 0 秒，不显示负数（刚设 at 那一下 now 可能还没追上来）。 */
  const smpSec = smpStarting && smpStartAt ? Math.max(0, Math.floor((smpNow - smpStartAt) / 1000)) : 0

  /* 列表里已有的字段先顶上，详情到了再覆盖 —— 这样弹窗一打开就有封面和名字，
   * 不会先闪一片空白再填内容。 */
  const g = detail || game
  const platform = g.platform || (String(g.tid || '').toUpperCase().slice(0, 4) === 'CUSA' ? 'PS4' : 'PS5')
  /* 来源类型看**物理源**，不看 g.src：src 记的是「从 app.db 还是扫描路径枚举到的」，
   * 而 ShadowMount 的镜像/裸目录游戏同样注册在 app.db 里（src 也是 installed）。
   * 后端已经给出正确的 source_type（pkg / image / folder，见 game_mgr.c 的
   * gm_class_of_fmt），这里只做「详情还没回来」时的兜底 —— 兜底同样按 fmt 判，
   * 不按 src，否则弹窗一打开那 200ms 会先闪一个错的「已安装 PKG」。 */
  const isPkg = g.installed_pkg === true || (g.installed_pkg === undefined && g.fmt === 'pkg')
  const sourceType = g.source_type
    || (isPkg ? 'pkg' : (g.fmt === 'folder' || g.fmt === 'lz4') ? 'folder' : 'image')
  const SOURCE_LABEL = { pkg: t('game.src_pkg', '已安装 PKG'), image: t('game.src_image', '镜像'), folder: t('game.src_folder', '文件夹') }

  return (
    <Modal
      show={show}
      title={g.name || tid}
      onClose={onClose}
      className="max-w-3xl"
      footer={
        /* [CN-FORK] v0.5.2.131 按钮下面可能有一句失败提示（拉不起来 / 没装 SMP），
         * footer 骨架是横向 .stack-x，这里改成纵向堆叠：提示在上、按钮在下。 */
        <div className="flex flex-col gap-2 w-full">
          {smpFail && (
            <Note tone="error" icon={AlertTriangle}>{smpFail}</Note>
          )}
          <button
            type="button"
            className="btn btn-outline w-full"
            onClick={openSmp}
            disabled={smpStarting}
            title={smpStarting ? smpPhase : t('game.smp_hint', '挂载 / 卸载 / 删除源请到 ShadowMount 后台处理')}
            aria-label={smpStarting ? smpPhase : t('game.smp_open', '去 ShadowMount 管理')}
          >
            {smpStarting ? <RefreshCw className="w-4 h-4 animate-spin" /> : <ExternalLink className="w-4 h-4" />}
            <span>{smpStarting
              ? `${smpPhase} ${t('game.smp_waited', '已等 {{n}} 秒', { n: smpSec })}`
              : t('game.smp_open', '去 ShadowMount 管理')}</span>
          </button>
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
              {/* 徽标行：来源类型 / 镜像格式 / 已挂载 / 自定义图 */}
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

        {/* 这个弹窗不做任何改动，把去向说清楚，别让用户找按钮 */}
        <p className="mt-4 panel-tiny text-zinc-500 flex items-start gap-1.5">
          <ExternalLink className="w-3.5 h-3.5 shrink-0 mt-0.5" />
          <span>{t('game.smp_hint', '这个弹窗只看信息；挂载、卸载、删除源请到 ShadowMount 后台处理。')}</span>
        </p>
        {/* 「没有可删的物理源」只对**真·内置安装的 PKG** 成立。判据用后端的
             installed_pkg（= 物理源那两个 lnk 都没有），不用 g.src —— ShadowMount 的
             镜像/裸目录游戏也在 app.db 里，用 src 判会给它们也挂上这句话。 */}
        {detail && detail.installed_pkg === true && (
          <p className="mt-2 panel-tiny text-zinc-500 flex items-start gap-1.5">
            <ImageIcon className="w-3.5 h-3.5 shrink-0 mt-0.5" />
            <span>{t('game.pkg_no_source', '这是已安装的 PKG，没有物理源文件；它只存在于主机系统盘里。')}</span>
          </p>
        )}
      </div>
    </Modal>
  )
}
