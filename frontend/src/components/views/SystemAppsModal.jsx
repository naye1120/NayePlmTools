import React, { useCallback, useEffect, useRef, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { AppWindow, AlertTriangle } from 'lucide-react'
import Modal from '../ui/Modal'
import Switch from '../ui/Switch'
import { cn } from '../../utils/helpers'
import { MiniEmpty, Note } from '../game/parts'
import {
  getSystemApps, setSystemAppHidden, setSystemAppPinned,
  appIconUrl,
} from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.158 「个性化软件」弹窗 —— 主机内置软件在主屏上的显示 / 隐藏 / 置顶。
 *
 * 一行一个软件：左侧是主机自带的图标（只读预览），右侧两颗开关：
 *   · 隐藏：开 = 从主屏藏起来，关 = 显示出来（写 app.db 的 visible 列）；
 *   · 置顶：把它写进主机主屏的固定列表（MyHome 组），排在最前。
 * 方向以主机为准（app.db 的 visible 列），本文件不做任何取反 ——
 * 前端一旦自己反一次，后端再反一次就变成「开关写着隐藏、按下去显示」。
 *
 * 状态每次打开都重新拉：真值就在主机那一列里，缓存一份只会多出「面板说藏了、
 * 主屏还画着」这类对不上账的排查。
 *
 * [CN-FORK] v0.5.2.161 后端会回 `controllable:false`（面板改不动的条目）—— 整组不显示。
 *
 * [CN-FORK] v0.6.0.179 图标 / 背景的换图上传曾在此页提供；现按用户口径移除
 * 「图标和背景的修改功能」，只保留主机自带图标的只读预览与「置顶 / 隐藏」两开关。
 *
 * 乐观更新只用在两颗开关上：拨完到主机回话之间把开关先动起来，失败整行退回去。
 *
 * 用 layout="panel"：这是一张纯列表的工作区，塞进居中卡片一屏只看得到三四行
 * （与存档配置弹窗同一判断，见 ArchiveConfigModal）。 */

export default function SystemAppsModal({ show, onClose, addToast }) {
  const { t } = useTranslation()
  const [apps, setApps] = useState([])
  const [count, setCount] = useState(0)
  const [accounts, setAccounts] = useState(0)
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)
  /* 正在写的那一个：拨完到主机回话之间把这一行的开关禁掉，
   * 不然遥控器连按两下会把两条相反的请求都发出去。 */
  const [busyTid, setBusyTid] = useState('')
  /* 图标预览加载失败的 tid：主机上没有这一条、或者老机型压根没给图标。
   * 按 tid 记账而不是一个布尔：一屏 19 行，一行失败不能把全列表的图都换成占位。 */
  const [iconFailed, setIconFailed] = useState({})
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false }
  }, [])

  const load = useCallback(() => {
    setLoading(true)
    setError('')
    setApps([])
    setIconFailed({})
    return getSystemApps()
      .then((data) => {
        if (!aliveRef.current) return
        setApps((data && data.apps) || [])
        setCount((data && data.count) || 0)
        setAccounts((data && data.accounts) || 0)
        setError((data && data.error) || '')
      })
      .catch((e) => {
        if (aliveRef.current) setError(e.message || t('appsys.load_fail', '读取失败'))
      })
      .finally(() => { if (aliveRef.current) setLoading(false) })
  }, [t])

  useEffect(() => {
    if (!show) return
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开时拉一次主机的内置软件
    load()
  }, [show, load])

  if (!show) return null

  /* 后端没给 controllable 字段（老面板 / mock 漏改）时按「可控」处理：
   * 少一组比少一个开关更糟 —— 那样用户会以为这几条凭空消失了。
   * `.161`：改不动的那几条**根本不列出来**（用户口径），所以这里只做过滤，不再分组。 */
  const shown = apps.filter((a) => a.controllable !== false)

  /* 后端的 count 是**不过滤**的总数（含面板改不动的那几条），列表里只画 shown；
   * 两个数字不一致时以列表为准，免得出现「说 19 条、只列出 12 条」这种对不上账的提示。 */
  const shownCount = shown.length || count

  /* 拨开关之前统一进这个状态：busyTid 非空时全列表的控件都禁掉。
   * 真机上后端写的是同一张 app.db，两条相反的请求并发到主机上，落盘先后是随机的 ——
   * 遥控器连按两下就能撞上。 */
  const beginBusy = (tid) => { setBusyTid(tid) }
  const endBusy = () => {
    if (!aliveRef.current) return
    setBusyTid('')
  }

  /* 这一行此刻在写主机时的占位小字。 */
  const busyText = (tid) => (busyTid === tid ? t('appsys.saving', '正在写入主机…') : '')

  const setHidden = async (tid, hidden) => {
    const prev = apps
    /* 乐观更新：电视端一次 POST 往返要几百毫秒，等回话再动开关的话，
     * 用户会以为没按到又按一次。失败再整行退回去。 */
    setApps((list) => list.map((a) => (a.tid === tid ? { ...a, hidden } : a)))
    beginBusy(tid)
    try {
      await setSystemAppHidden(tid, hidden)
      addToast?.(hidden
        ? t('appsys.hidden_toast', '已从主屏隐藏，重启或待机唤醒后生效')
        : t('appsys.shown_toast', '已设为显示，重启或待机唤醒后生效'))
    } catch (e) {
      setApps(prev)
      addToast?.(e.message || t('appsys.save_fail', '设置失败'), 'error')
    } finally {
      endBusy()
    }
  }

  /* [CN-FORK] v0.6.0.179 置顶 = 写进**主机主屏的固定列表**（app.db 的 MyHome 组），
   * 不再是本面板自己的 app_pins.json（那是 `.164` 的旧语义，主机看不见）。
   * 与「隐藏」仍是两个开关、两处存储（隐藏写 visible 列），只是现在置顶也落到主机上了。
   * 同一条乐观更新 + 失败回退；toast 里带上后端回的 idx（置顶后的真实位置）。 */
  const setPinned = async (tid, pinned) => {
    const prev = apps
    setApps((list) => list.map((a) => (a.tid === tid ? { ...a, pinned } : a)))
    beginBusy(tid)
    try {
      const r = await setSystemAppPinned(tid, pinned)
      /* accounts === 0 = 幂等命中（本来就在最前 / 本来就不在组里），**不是失败**：
       * 说「已置顶」不算错，但说清「本来就在最前」更像人话 —— 同族提示见 v0.5.2.142
       * 「远程还没有这个游戏的存档」那条（首次使用必然遇到，不该是一条红报错）。 */
      const idx = (r && r.idx) || 0
      if (pinned) {
        addToast?.(r && r.accounts === 0
          ? t('appsys.pinned_same', '本来就在主屏固定列表最前')
          : t('appsys.pinned_toast', '已写进主机主屏固定列表（第 {{idx}} 位），重启或待机唤醒后主屏才重排', { idx: idx || 1 }))
      } else {
        addToast?.(r && r.accounts === 0
          ? t('appsys.unpinned_same', '本来就不在主屏固定列表里')
          : t('appsys.unpinned_toast', '已从主机主屏固定列表移除，重启或待机唤醒后生效'))
      }
    } catch (e) {
      setApps(prev)
      addToast?.(e.message || t('appsys.save_fail', '设置失败'), 'error')
    } finally {
      endBusy()
    }
  }

  return (
    <Modal
      show={show}
      title={t('appsys.title', '个性化软件')}
      onClose={onClose}
      layout="panel"
    >
      <div className="panel-body leading-relaxed font-normal flex flex-col gap-4">
        <Note tone="info">
          {t('appsys.hint', '这里只列主机自带的内置软件。「置顶」= 把它写进主机主屏的固定列表（排在最前），「隐藏」= 把它从主屏藏起来。改完主屏不会立刻重排，重启主机或待机唤醒后才生效。面板改不动的那几条不列在这里。')}
        </Note>

        {/* [CN-FORK] v0.6.0.179 这一条是「关机开机后被重置」那个修复的用户可见面：
            面板每次开机都会把这两项重放到主机上。用户不必知道实现，但要能预期
            「主机自己把 app.db 改写回去时，我的选择会不会丢」——答案是不会。 */}
        <Note tone="ok">
          {t('appsys.boot_note', '「置顶 / 隐藏」的状态面板每次开机都会重新对齐到主机 —— 主机自己改写 app.db 里那几行（SMP 重扫、新增账号）也不会把它们冲掉。')}
        </Note>

        {error && (
          <Note tone="error" icon={AlertTriangle}>
            {error}
          </Note>
        )}

        {loading && (
          <p className="panel-note text-zinc-500">{t('appsys.loading', '正在读取主机的内置软件…')}</p>
        )}

        {!loading && !shown.length && !error && (
          <MiniEmpty icon={AppWindow}>
            {t('appsys.empty', '主机上没有找到可管理的内置软件')}
          </MiniEmpty>
        )}

        {!loading && shown.length > 0 && (
          <>
            {/* accounts 是「写入时一次写平几个账号」——写出来用户才知道为什么改一条
                会在每个账号上都生效（内置软件的显隐是**按账号**存的）。 */}
            <p className="panel-tiny text-zinc-500">
              {t('appsys.count_hint', '共 {{n}} 条内置软件，主机上有 {{accounts}} 个账号（改一次会同时写到每个账号）。', {
                n: shownCount, accounts: accounts || 1,
              })}
            </p>
            {/* 卡片宫格（用户口径的版式）：默认每行三列（窄屏逐级降为两列 / 单列）。
                每张卡 = 最左「图标」独占一列（垂直居中），右列自上而下是「名称 / TID」、
                一道发丝分隔线、「置顶 / 隐藏」左右分列的两颗开关。 */}
            <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-3">
              {shown.map((a) => {
                const rowBusy = busyTid === a.tid
                /* 只读预览：主机自带的内置软件图标（换图上传功能已按用户口径移除）。 */
                const iconSrc = appIconUrl(a.tid)
                return (
                  <div
                    key={a.tid}
                    className={cn(
                      'rounded-xl border border-white/10 bg-white/[0.02] p-3 flex items-stretch gap-3',
                      rowBusy && 'opacity-60',
                    )}
                  >
                    {/* 图标：最左边独占一列、垂直居中。尺寸写死，图 404 退回中性占位
                        且与真图占同一固定方框，卡片高度不随图片成败变化。 */}
                    <div className="relative w-14 h-14 shrink-0 self-center rounded-xl overflow-hidden bg-white/5 border border-white/10 flex items-center justify-center">
                      {iconFailed[a.tid] ? (
                        <AppWindow className="w-7 h-7 text-zinc-600" aria-hidden="true" />
                      ) : (
                        <img
                          src={iconSrc}
                          alt=""
                          loading="lazy"
                          className="w-full h-full object-cover"
                          onError={() => setIconFailed((m) => ({ ...m, [a.tid]: true }))}
                        />
                      )}
                    </div>

                    {/* 右列：名称 / TID 在上，一道发丝分隔线，置顶 / 隐藏两颗开关在下。 */}
                    <div className="flex-1 min-w-0 flex flex-col gap-3">
                      <div className="min-w-0">
                        <p className="text-white panel-note font-semibold truncate">{a.name || a.tid}</p>
                        <p className="text-zinc-500 panel-tiny mt-0.5 leading-snug font-mono tracking-wide truncate">
                          {rowBusy ? busyText(a.tid) : a.tid}
                        </p>
                      </div>

                      {/* 分隔线：把「是哪一条」与「要改什么」分成上下两块。 */}
                      <div className="h-px bg-white/10" aria-hidden="true" />

                      {/* 置顶在左、隐藏在右，两端对齐。每个开关前带小字标签 ——
                          电视端没有 hover，光看两颗长得一样的滑块分不出哪个是哪个。
                          方向都不取反：开=置顶 / 开=隐藏。 */}
                      <div className="flex items-center justify-between">
                        <div className="flex items-center gap-2">
                          <span className="panel-tiny font-semibold text-zinc-400">{t('appsys.pin', '置顶')}</span>
                          <Switch
                            checked={!!a.pinned}
                            disabled={busyTid !== ''}
                            label={`${a.name || a.tid} ${t('appsys.pin_label', '置顶（写进主机主屏固定列表，排在最前）')}`}
                            onChange={(v) => setPinned(a.tid, v)}
                          />
                        </div>
                        <div className="flex items-center gap-2">
                          <span className="panel-tiny font-semibold text-zinc-400">{t('appsys.hide', '隐藏')}</span>
                          <Switch
                            checked={!!a.hidden}
                            disabled={busyTid !== ''}
                            label={`${a.name || a.tid} ${t('appsys.hide_label', '隐藏（从主屏藏起来）')}`}
                            onChange={(v) => setHidden(a.tid, v)}
                          />
                        </div>
                      </div>
                    </div>
                  </div>
                )
              })}
            </div>
          </>
        )}
      </div>
    </Modal>
  )
}
