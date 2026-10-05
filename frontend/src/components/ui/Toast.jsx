import React, { useEffect } from 'react'
import { CheckCircle2, AlertTriangle, X } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.93 通知条修正
 * 1. 位置：组件自己写死 `fixed top-8 right-8`，而 App 外层已经有一个 fixed 容器在排队——
 *    结果是多条通知全落在同一个坐标上互相压字。现在只留容器定位，条目本身走正常流。
 * 2. 时长：原来一律 3 秒即逝，且没有关闭入口。错误类通知（安装失败、序列中止）在电视端
 *    3 秒根本来不及读完，改成分级驻留：成功 4s、错误 9s，并且随时可以用 OK/点击手动关掉。
 * 3. 文本用 break-words 收住，长错误信息不再把弹窗顶出屏幕。
 *
 * [CN-FORK] v0.5.2.109 用户口径：「提示标签改的好看一点，太大太占地方了，能不能放到
 * 中下边」（指点了激活/登入之后弹出来的这条提示）。所以：
 *   · 位置：从右上角挪到**底部居中**（容器改，见 App.jsx）；
 *   · 体量：内边距 16→10px、圆角 16→12px、图标 24→16px、字号压到 13px，
 *     去掉 `font-bold uppercase`（全大写只有英文受影响，中文夹在中间很吵），
 *     阴影 shadow-2xl → shadow-lg。
 * 语义与配色档位一行未动：成功仍是 emerald、错误仍是 red，驻留时长也照旧。 */
const Toast = ({ message, type = 'success', onClose }) => {
  const { t } = useTranslation()

  useEffect(() => {
    const ms = type === 'error' ? 9000 : 4000
    const timer = setTimeout(onClose, ms)
    return () => clearTimeout(timer)
  }, [onClose, type])

  return (
    <div className={cn(
      'pointer-events-auto flex items-center gap-2 px-3 py-2 rounded-xl border shadow-lg',
      'max-w-[min(92vw,32rem)] text-[13px] leading-snug',
      type === 'success'
        ? 'bg-emerald-950/90 border-emerald-500/50 text-emerald-400'
        : 'bg-red-950/90 border-red-500/50 text-red-400'
    )}>
      {type === 'success'
        ? <CheckCircle2 className="w-4 h-4 shrink-0" />
        : <AlertTriangle className="w-4 h-4 shrink-0" />}
      <span className="font-semibold flex-1 min-w-0 break-words">{message}</span>
      <button
        onClick={onClose}
        className="shrink-0 -mr-1 p-0.5 rounded-md opacity-70 hover:opacity-100"
        title={t("app.buttons.close", "Close")}
        aria-label={t("app.buttons.close", "Close")}
      >
        <X className="w-3.5 h-3.5" />
      </button>
    </div>
  )
}

export default Toast
