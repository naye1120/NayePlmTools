import React, { useCallback, useEffect, useRef, useState } from 'react'
import { useTranslation } from 'react-i18next'
import { AppWindow, AlertTriangle } from 'lucide-react'
import Modal from '../ui/Modal'
import { MiniEmpty, Note, TimingRow } from '../game/parts'
import { getSystemApps, setSystemAppHidden } from '../../utils/gameApi'

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

  return (
    <Modal
      show={show}
      title={t('appsys.title', '个性化软件')}
      onClose={onClose}
      layout="panel"
    >
      <div className="panel-body leading-relaxed font-normal flex flex-col gap-4">
        <Note tone="info">
          {t('appsys.hint', '这里只列主机自带的内置软件。开关打开 = 把它从主屏藏起来，关掉 = 显示出来。改完主屏不会立刻重绘，重启主机或待机再唤醒后生效。面板改不动的那几条不列在这里。')}
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
              <TimingRow
                key={a.tid}
                label={a.name || a.tid}
                hint={busyTid === a.tid ? t('appsys.saving', '正在写入主机…') : a.tid}
                checked={!!a.hidden}
                disabled={busyTid !== ''}
                onChange={(v) => setHidden(a.tid, v)}
              />
            ))}
          </div>
        )}
      </div>
    </Modal>
  )
}
