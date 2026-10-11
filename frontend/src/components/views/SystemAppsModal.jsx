import React, { useCallback, useEffect, useRef, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { AppWindow, AlertTriangle } from 'lucide-react'
import Modal from '../ui/Modal'
import Switch from '../ui/Switch'
import { cn } from '../../utils/helpers'
import { MiniEmpty, Note } from '../game/parts'
import { getSystemApps, setSystemAppHidden, setSystemAppPinned } from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.158 「个性化软件」弹窗 —— 主机内置软件在主屏上的显示 / 隐藏。
 *
 * 一行一个软件，右侧一个**隐藏开关**：开 = 从主屏藏起来，关 = 显示出来。
 * 方向以主机为准（app.db 的 visible 列），本文件不做任何取反 ——
 * 前端一旦自己反一次，后端再反一次就变成「开关写着隐藏、按下去显示」。
 *
 * 状态每次打开都重新拉：真值就在主机那一列里，缓存一份只会多出「面板说藏了、
 * 主屏还画着」这类对不上账的排查。
 *
 * [CN-FORK] v0.5.2.160 后端会回 `controllable:false`（面板改不动的条目，真机实测那一列
 * 对它们不起作用）。`.160` 那版把它们单独列一组并写明原因；`.161` 按用户口径改成
 * **整组不显示**：「其他的不能编辑的选项直接就不用在前端显示了」。
 * 名单仍然留在后端而不是前端抄一份 —— 前端自己判「能不能改」迟早和后端漂。
 *
 * [CN-FORK] v0.5.2.164 每行多了第二个开关：**置顶**。
 * 与「隐藏」刻意分成两个开关，因为它们是两件事、落到两处存储：
 * 隐藏写主机 app.db 的 visible（系统认），置顶落**本面板**的 app_pins.json
 * （面板自己的列表排序），不动 app.db —— 理由与真机取证的缺口见 include/game_prefs.h。
 * 方向同样不取反：开 = 置顶。
 *
 * 用 layout="panel"：这是一张纯列表的工作区，塞进居中卡片一屏只看得到三四行
 * （与存档配置弹窗同一判断，见 ArchiveConfigModal）。 */
export default function SystemAppsModal({ show, onClose, addToast }) {
  const { t } = useTranslation()
  const [apps, setApps] = useState([])
  const [error, setError] = useState('')
  const [loading, setLoading] = useState(false)
  /* 正在写的那一个：拨完到主机回话之间把这一行的开关禁掉，
   * 不然遥控器连按两下会把两条相反的请求都发出去。 */
  const [busyTid, setBusyTid] = useState('')
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false }
  }, [])

  const load = useCallback(() => {
    setLoading(true)
    setError('')
    setApps([])
    return getSystemApps()
      .then((data) => {
        if (!aliveRef.current) return
        setApps((data && data.apps) || [])
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

  const setHidden = async (tid, hidden) => {
    const prev = apps
    /* 乐观更新：电视端一次 POST 往返要几百毫秒，等回话再动开关的话，
     * 用户会以为没按到又按一次。失败再整行退回去。 */
    setApps((list) => list.map((a) => (a.tid === tid ? { ...a, hidden } : a)))
    setBusyTid(tid)
    try {
      await setSystemAppHidden(tid, hidden)
      addToast?.(hidden
        ? t('appsys.hidden_toast', '已隐藏，重启或待机唤醒后主屏生效')
        : t('appsys.shown_toast', '已设为显示，重启或待机唤醒后主屏生效'))
    } catch (e) {
      setApps(prev)
      addToast?.(e.message || t('appsys.save_fail', '设置失败'), 'error')
    } finally {
      if (aliveRef.current) setBusyTid('')
    }
  }

  /* [CN-FORK] v0.5.2.164 置顶：与「隐藏」是**两个不同的开关**（落到不同的存储）——
   * 隐藏写 app.db（系统认），置顶落本面板的 app_pins.json（面板自己的列表排序），
   * 不动 app.db。理由见 include/game_prefs.h。同一条乐观更新 + 失败回退。 */
  const setPinned = async (tid, pinned) => {
    const prev = apps
    setApps((list) => list.map((a) => (a.tid === tid ? { ...a, pinned } : a)))
    setBusyTid(tid)
    try {
      await setSystemAppPinned(tid, pinned)
      addToast?.(pinned
        ? t('appsys.pinned_toast', '已置顶')
        : t('appsys.unpinned_toast', '已取消置顶'))
    } catch (e) {
      setApps(prev)
      addToast?.(e.message || t('appsys.save_fail', '设置失败'), 'error')
    } finally {
      if (aliveRef.current) setBusyTid('')
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
          {t('appsys.hint', '这里只列主机自带的内置软件。左侧「置顶」= 在面板列表里固定到最前；右侧「隐藏」= 把它从主屏藏起来（改完重启主机或待机再唤醒后生效）。面板改不动的那几条不列在这里。')}
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

        {shown.length > 0 && (
          <div className="flex flex-col gap-2">
            {shown.map((a) => (
              /* [CN-FORK] v0.5.2.164 一行两个开关（原来只有一个「隐藏」）。
               * 每个开关前面带一枚小字标签 —— 电视端没有 hover，光看两颗长得一样的
               * 滑块分不出哪个是哪个。方向都不取反：开=置顶 / 开=隐藏。 */
              <div
                key={a.tid}
                className={cn(
                  'flex items-center justify-between gap-3 p-2.5 rounded-lg border border-white/10 bg-white/[0.02]',
                  busyTid === a.tid && 'opacity-60',
                )}
              >
                <div className="min-w-0">
                  <p className="text-white panel-note font-semibold truncate">{a.name || a.tid}</p>
                  <p className="text-zinc-500 panel-tiny mt-0.5 leading-snug">
                    {busyTid === a.tid ? t('appsys.saving', '正在写入主机…') : a.tid}
                  </p>
                </div>
                <div className="flex items-center gap-4 shrink-0">
                  <div className="flex items-center gap-2">
                    <span className="panel-tiny font-semibold text-zinc-400">{t('appsys.pin', '置顶')}</span>
                    <Switch
                      checked={!!a.pinned}
                      disabled={busyTid !== ''}
                      label={`${a.name || a.tid} ${t('appsys.pin', '置顶')}`}
                      onChange={(v) => setPinned(a.tid, v)}
                    />
                  </div>
                  <div className="flex items-center gap-2">
                    <span className="panel-tiny font-semibold text-zinc-400">{t('appsys.hide', '隐藏')}</span>
                    <Switch
                      checked={!!a.hidden}
                      disabled={busyTid !== ''}
                      label={`${a.name || a.tid} ${t('appsys.hide', '隐藏')}`}
                      onChange={(v) => setHidden(a.tid, v)}
                    />
                  </div>
                </div>
              </div>
            ))}
          </div>
        )}
      </div>
    </Modal>
  )
}
