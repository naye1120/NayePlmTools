import React, { useState, useEffect, useCallback } from 'react'
import { useTranslation } from 'react-i18next'
import { Plus, X, RefreshCw, ExternalLink, Check, Link2 } from 'lucide-react'
import { cn } from '../../utils/helpers' /* [CN-FORK] v0.5.2.94 板块/页头/空态归一 */
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'
import EmptyState from '../ui/EmptyState'
import LoadingState from '../ui/LoadingState'
import ThermalSection from './ThermalSection' /* [CN-FORK] 温控管理板块 */

/* [CN-FORK] 「常用入口」页：手动维护的快捷访问清单（IP:port 类服务导航）。
 * 数据存 payload 侧 /data/nayeplmtools/quicklinks.json（非浏览器 localStorage），
 * 手机/电脑/PS5 打开本 UI 看到的是同一份。列表接口顺带对每条 URL 的 host:port
 * 做 TCP 探活：绿点=可连通、灰点=不可达。绿点卡片点卡片新标签打开；灰点卡片点击由
 * openProgram 接管——试着拉起对应 payload，起来后同样开新标签，不覆盖本页。
 * 本机已知服务自动探测表暂未实现，后续在 /quicklinks 里合并生成即可，前端无感。 */

/* [CN-FORK] v0.5.2.85 按钮收口到 index.css 的骨架层三档形态（实心 / 描边 / 幽灵），
 * 各视图不再各自复制一串 Tailwind 类；配色全走 CSS 变量，浅色主题自动跟随。 */
const BTN_PRIMARY = 'btn btn-primary'
const BTN_GHOST = 'btn btn-outline'
const INPUT_CLS = 'field'

/* [CN-FORK] v0.5.2.83 通用「不活即拉起」：入口标题 → 已安装 payload 的 ELF 路径匹配。
 * 不写死任何程序名，以后新增 payload 无需改这里。匹配用「只留字母」的归一化名（丢掉
 * 版本号/变体/中文/符号），再做双向前缀判断；唯一候选才认，避免歧义拉起错的程序。 */
const normName = (s) => String(s || '').toLowerCase().replace(/\.elf$/, '').replace(/[^a-z]/g, '')
const prefixHit = (a, b, min) => {
  const x = normName(a), y = normName(b)
  return !!x && !!y && Math.min(x.length, y.length) >= min && (x.startsWith(y) || y.startsWith(x))
}

/* /list_payloads 的 payloads 是 "…/<目录>/<文件>.elf"，按目录聚合一个候选；
 * 本面板自身（nayeplmtools*）永远排除，否则点任何条目都可能重起自己。 */
const buildLaunchIndex = (data) => {
  const byDir = new Map()
  for (const p of (data && data.payloads) || []) {
    const seg = String(p).split('/')
    const dir = seg[seg.length - 2] || ''
    if (!dir || byDir.has(dir)) continue
    const meta = ((data && data.meta) || {})[seg[seg.length - 1]] || {}
    byDir.set(dir, { dir, path: p, names: [dir, meta.display_name].filter(Boolean) })
  }
  return [...byDir.values()]
}

const findLaunchTarget = (index, name) => {
  const hits = index.filter((c) => !normName(c.dir).startsWith('nayeplmtools') &&
    c.names.some((n) => prefixHit(n, name, 3)))
  return hits.length === 1 ? hits[0].path : ''
}

/* 这些请求辅助函数一律放模块作用域：组件体内定义的取数闭包会被 react-hooks/purity
 * 判成「渲染期调用不纯函数」（Date.now / fetch），逻辑本身没在渲染路径上。 */
const getJson = (url) => fetch(url, { cache: 'no-store' }).then((r) => r.json()).catch(() => null)
const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

/* 已在运行的程序不再重复拉起（elfldr 起新进程不会杀旧的，会叠出多个实例）。
 * 前缀阈值取 8：进程名与目录名同源，短词（dbst 等）不误伤。 */
const isAlreadyRunning = async (path) => {
  const data = await getJson(`/processes_list?_=${Date.now()}`)
  const procs = ((data && data.processes) || []).map((p) => p.name)
  const seg = String(path).split('/')
  return procs.some((pn) => [seg[seg.length - 2], seg[seg.length - 1]]
    .some((c) => c && prefixHit(c, pn, 8)))
}

/* 存活判定只有一条路：PKG Manager 问专有的 /pkgmanager_status（本机 8844 探活），
 * 其余条目重读 /quicklinks 里自己那条的 alive——alive 的算法留在主机侧，前端不另起一套。 */
const probeEntry = async (entry) => {
  if (entry.program === 'pkgmanager') {
    const s = await getJson(`/pkgmanager_status?_=${Date.now()}`)
    return !!(s && s.running)
  }
  const data = await getJson(`/quicklinks?_=${Date.now()}`)
  const hit = ((data && data.entries) || []).find((x) => x.url === entry.url)
  return !!(hit && hit.alive)
}

const waitAlive = async (entry, gap, times) => {
  for (let k = 0; k < times; k++) {
    await sleep(gap)
    if (await probeEntry(entry)) return true
  }
  return false
}

const launchPkgManager = () => getJson(`/pkgmanager_launch?_=${Date.now()}`)

/* 候选表缓存在模块作用域：点击手势里要同步判断「这条有没有对应程序」，异步取完再
 * preventDefault 就来不及了（默认的新标签已经打开）。null=还没取到，此时宁可接管，
 * 也不能因为索引缺失而丢掉拉起能力。 */
let launchIndexCache = null
const refreshLaunchIndex = async () => {
  const data = await getJson(`/list_payloads?_=${Date.now()}`)
  if (data && Array.isArray(data.payloads)) launchIndexCache = buildLaunchIndex(data)
}

/* 灰点条目是否值得接管：本机有同名程序（或 PKG Manager 这类专有入口）才接管拉起；
 * 纯外网链接（NAS/路由器后台等）维持默认新标签打开。 */
const shouldTakeOver = (entry) => {
  if (entry.program === 'pkgmanager') return true
  if (!launchIndexCache) return true
  return !!findLaunchTarget(launchIndexCache, entry.name)
}

export default function CommonEntriesView({ showConfirm, addToast }) {
  const { t } = useTranslation()
  const [entries, setEntries] = useState(null) // null=未加载
  const [busy, setBusy] = useState(false)
  const [adding, setAdding] = useState(false)
  const [form, setForm] = useState({ name: '', url: '' })
  const [startingIdx, setStartingIdx] = useState(-1) // [CN-FORK] 程序类入口正在启动的卡片下标

  /* [CN-FORK] v0.5.2.94 操作反馈统一走全站 Toast。病灶是原来那行写在板块底部的状态小字：
   * 卡片网格一长，它就被顶到首屏之外，用户看不到「已添加 / 删除失败」以为没生效。 */
  const notify = (text, type = 'success') => addToast(text, type)

  const load = useCallback(async () => {
    try {
      const res = await fetch(`/quicklinks?_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data && Array.isArray(data.entries)) setEntries(data.entries)
    } catch { /* 服务未响应时保留上次清单 */ }
    /* [CN-FORK] 每次刷清单顺带刷新拉起候选表（不 await：慢一点的 payload 列表不该拖住卡片渲染） */
    refreshLaunchIndex()
  }, [])

  useEffect(() => { load() }, [load])

  const save = async () => {
    let url = form.url.trim()
    // 少打字容错：没写协议前缀自动补 http://（https 需显式书写）
    if (url && !/^https?:\/\//i.test(url)) url = `http://${url}`
    if (!form.name.trim() || !url) return
    setBusy(true)
    try {
      const res = await fetch(
        `/quicklinks_add?name=${encodeURIComponent(form.name.trim())}&url=${encodeURIComponent(url)}&_=${Date.now()}`,
        { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      if (data?.ok) {
        setForm({ name: '', url: '' })
        setAdding(false)
        notify(t('entries.saved', '已添加'))
      } else {
        notify(data?.message || t('entries.save_fail', '添加失败'), 'error')
      }
    } catch {
      notify(t('entries.save_fail', '添加失败'), 'error')
    }
    setBusy(false)
    load()
  }

  /* [CN-FORK] v0.5.2.94 删除走全站统一的危险确认弹窗，不再在卡片里叠一层黑底小确认条：
   * 那两条 13px 小按钮遥控器点不到，而且每个视图各自发明一套确认样式。 */
  const askRemove = (entry, index) => {
    showConfirm(
      t('entries.delete', '删除'),
      t('entries.confirm_delete', '删除「{{name}}」？', { name: entry.name }),
      () => remove(index),
      true /* 删除条目不可逆 → 危险档 */
    )
  }

  const remove = async (index) => {
    setBusy(true)
    try {
      const res = await fetch(`/quicklinks_remove?index=${index}&_=${Date.now()}`, { cache: 'no-store' })
      const data = await res.json().catch(() => null)
      notify(data?.ok
        ? t('entries.deleted', '已删除')
        : data?.message || t('entries.delete_fail', '删除失败'),
        data?.ok ? 'success' : 'error')
    } catch {
      notify(t('entries.delete_fail', '删除失败'), 'error')
    }
    setBusy(false)
    load()
  }

  /* [CN-FORK] v0.5.2.83 灰点条目点击接管：先探一次存活，不活就按名称匹配已安装 payload
   * 拉起（PKG Manager 仍走专有的 /pkgmanager_launch，那条路还兼管内嵌版本的安装）。
   * v0.5.2.84 改为新标签打开：tab 由点击手势同步开好（about:blank），拉起成功后才把它
   * 指向目标地址；失败就关掉它，不给用户留一堆空白标签。异步里再 window.open 会被
   * 弹窗拦截，所以必须手势内先占位。tab 为 null（被拦）时不硬塞当前页，程序其实已经
   * 起来了，条目会变成绿点，再点一次即走原生新标签。 */
  const openProgram = async (entry, index, tab) => {
    if (startingIdx !== -1) {
      if (tab) tab.close() /* 上一条还在拉起中：把手势里抢到的空白标签收回，不留孤儿 */
      return
    }
    setStartingIdx(index)
    let go = false
    try {
      if (await probeEntry(entry)) {
        go = true
      } else if (entry.program === 'pkgmanager') {
        /* launch 应答失败也不短路，给端口一小段缓冲再定论 */
        const lr = await launchPkgManager()
        if (lr && !lr.ok) throw new Error(lr.message || t('entries.start_fail', '启动失败'))
        if (!(await waitAlive(entry, 500, 40)))
          throw new Error(t('entries.start_timeout', '已发起拉起，但端口迟迟没有就绪'))
        go = true
      } else {
        if (!launchIndexCache) await refreshLaunchIndex() /* 索引还没到手（首屏就点）：现取一次 */
        const path = findLaunchTarget(launchIndexCache || [], entry.name)
        if (!path) {
          go = true /* 索引失效等边缘情况：不猜程序，直接按普通链接打开 */
        } else if (await isAlreadyRunning(path)) {
          notify(t('entries.already_running',
            '该程序看起来已经在运行，但这个地址从本机连不通（可能只监听了 127.0.0.1）'), 'error')
        } else {
          const res = await fetch(`/loadpayload:${encodeURI(path)}`).catch(() => null)
          if (!res || !res.ok) throw new Error(t('entries.start_fail', '启动失败'))
          if (!(await waitAlive(entry, 700, 45)))
            throw new Error(t('entries.start_timeout', '已发起拉起，但端口迟迟没有就绪'))
          go = true
        }
      }
    } catch (err) {
      notify((err && err.message) || t('entries.start_fail', '启动失败'), 'error')
    }
    setStartingIdx(-1)
    if (go) {
      if (tab) {
        /* assign 而非写 location.href：后者会被 react-hooks 编译规则判成「渲染内修改不可变值」 */
        tab.location.assign(entry.url)
      } else {
        notify(t('entries.tab_blocked',
          '程序已经起来了，但浏览器拦住了新标签，请再点一次该条目打开'), 'error')
      }
    } else if (tab) {
      tab.close()
    }
    if (!go) load()
  }

  const list = entries || []

  return (
    <div className="max-w-5xl mx-auto pb-12">
      {/* [CN-FORK] v0.5.2.94 页头从卡片里挪到页面级并粘顶：主操作（添加入口）常驻可见，
          不必先滚到页首才能点。卡片式板块头则交给 Section 统一形状。 */}
      <PageHead
        bleed={false}
        title={t('entries.title', '常用入口')}
        desc={t('entries.desc', '常用插件与局域网服务的快捷访问清单（如 http://192.168.1.214:8844）。条目保存在主机上，手机、电脑、PS5 打开本页看到的都是同一份；绿点表示当前可连通。')}
        actions={!adding && (
          <button onClick={() => setAdding(true)} className={BTN_PRIMARY}>
            <Plus className="w-4 h-4" />
            {t('entries.add', '添加入口')}
          </button>
        )}
      />

      <div className="mt-6 space-y-8">
      {/* [CN-FORK] 布局：板块一 常用入口（沿用原 quicklinks 全部功能）；板块二 温控管理。
          游戏管理板块已抽出为独立页（侧栏「游戏管理」），不再挂在本页。 */}
      <Section
        id="quicklinks"
        icon={Link2}
        title={t('entries.list_title', '入口清单')}
        actions={(
          <button onClick={load} disabled={busy} title={t('common.refresh', '刷新')} aria-label={t('common.refresh', '刷新')} className="icon-btn">
            <RefreshCw className={cn(busy && "animate-spin")} />
          </button>
        )}
        bodyClass="space-y-4"
      >

      {adding && (
        <div className="p-4 bg-white/5 border border-ps-blue/40 rounded-xl">
          <div className="grid grid-cols-1 md:grid-cols-[200px_1fr_auto] gap-3 items-end">
            <div className="space-y-1.5">
              <label className="text-[12px] text-zinc-400">{t('entries.name', '名称')}</label>
              <input
                className={INPUT_CLS}
                value={form.name}
                maxLength={127}
                placeholder={t('entries.name_ph', '如：PKG Manager')}
                onChange={(e) => setForm({ ...form, name: e.target.value })}
              />
            </div>
            <div className="space-y-1.5">
              <label className="text-[12px] text-zinc-400">{t('entries.url', '地址')}</label>
              <input
                className={INPUT_CLS}
                value={form.url}
                maxLength={511}
                placeholder="http://192.168.1.100:8080"
                onChange={(e) => setForm({ ...form, url: e.target.value })}
                onKeyDown={(e) => { if (e.key === 'Enter') save() }}
              />
            </div>
            <div className="flex items-center pb-0.5">
              <button onClick={save} disabled={busy || !form.name.trim() || !form.url.trim()} className={BTN_PRIMARY}>
                <Check className="w-4 h-4" />
                {t('entries.save', '保存')}
              </button>
              {/* [CN-FORK] v0.5.2.98 取消按钮不再调 setMsg：内联状态行已在 .94 换成 Toast，
                  这个残留引用会让整棵 React 树崩掉（一进水品页就白屏）。 */}
              <button onClick={() => setAdding(false)} className={cn(BTN_GHOST, 'ml-2')}>
                {t('entries.cancel', '取消')}
              </button>
            </div>
          </div>
          <p className="text-[12px] text-zinc-500 mt-3">
            {t('entries.url_hint', '协议前缀可省略，默认按 http:// 处理；https 请完整书写。')}
          </p>
        </div>
      )}

      {entries === null ? (
        <LoadingState label={t('entries.loading', '正在读取入口清单…')} />
      ) : list.length === 0 ? (
        <EmptyState
          icon={Link2}
          title={t('entries.empty_title', '还没有入口')}
          desc={t('entries.empty', '还没有入口，点右上角「添加入口」开始')}
        />
      ) : (
        <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-4">
          {list.map((e, i) => (
            <div key={`${e.url}-${i}`} className="relative group bg-white/5 border border-white/10 rounded-xl overflow-hidden">
              <a
                href={e.url}
                target="_blank"
                rel="noreferrer"
                className="block p-4 space-y-2 row-link"
                onClick={(ev) => {
                  /* [CN-FORK] 灰点（探活不通）且本机有对应程序的条目才接管点击去拉起；绿点、以及
                     本机没有对应程序的外网链接一律保持默认新标签打开。拉起对象按名称匹配，不写死
                     程序名单，以后新增 payload 自动生效。
                     v0.5.2.84：接管时先在手势里抢一个空白标签（拦下默认跳转，因为此刻直接跳过去
                     必然连不通），程序起来后再把它指向目标地址——本页面不被覆盖。 */
                  if (!e.alive && shouldTakeOver(e)) {
                    ev.preventDefault()
                    openProgram(e, i, window.open('about:blank', '_blank'))
                  }
                }}
              >
                {/* [CN-FORK] v0.5.2.94 删除从「右上角 24px 悬浮 ×」改成卡片底部的整行按钮：
                    小块只在鼠标悬停时才看得出可点，遥控器与触屏基本点不中，而且它压着
                    外链图标（实机反馈的「链接与关闭按钮重合」就是这里）。整行了还有第三个
                    好处：焦点能停在行上，删除路径不再需要先「猜」出角落有个按钮。 */}
                <div className="flex items-center">
                  {/* name 上的 min-w-0 是 truncate 在 flex 子里真正生效的前提。 */}
                  <span className={`w-2.5 h-2.5 rounded-full shrink-0 ${e.alive ? 'bg-green-400' : 'bg-zinc-600'}`} title={e.alive ? t('entries.alive', '可连通') : t('entries.dead', '当前不可达')} />
                  <span className="ml-2.5 text-[15px] font-bold text-white truncate min-w-0">{e.name}</span>
                  {e.src === 'auto' && (
                    <span className="ml-1.5 shrink-0 px-1.5 py-0.5 rounded bg-ps-blue/15 text-ps-blue text-[11px] font-bold leading-none">
                      {t('entries.auto_tag', '自动')}
                    </span>
                  )}
                  <ExternalLink className="w-3.5 h-3.5 text-zinc-500 shrink-0 ml-auto group-hover:text-ps-blue transition" />
                </div>
                <p className="text-[12px] text-zinc-500 break-all leading-relaxed">{e.url}</p>
              </a>
              {startingIdx === i ? (
                <div className="absolute inset-0 bg-black/80 rounded-xl flex flex-col items-center justify-center space-y-2.5 px-4">
                  <RefreshCw className="w-5 h-5 text-ps-blue animate-spin" />
                  <p className="text-[13px] text-white text-center">
                    {t('entries.starting_prog', '正在启动 {{name}}…', { name: e.name })}
                  </p>
                </div>
              ) : (
                <button
                  onClick={() => askRemove(e, i)}
                  title={t('entries.delete', '删除')}
                  className="w-full min-h-[var(--ctl-h)] flex items-center border-t border-white/5 px-4 text-[12px] font-bold uppercase tracking-tight text-zinc-500 hover:text-red-400 transition-colors"
                >
                  <X className="w-3.5 h-3.5 mr-1.5" />
                  {t('entries.delete', '删除')}
                </button>
              )}
            </div>
          ))}
        </div>
      )}
      </Section>

      {/* [CN-FORK] 板块二 温控管理：读 /thermal_stats 展示 CPU/GPU/SSD 温度 + 风扇 */}
      <ThermalSection />
      </div>
    </div>
  )
}
