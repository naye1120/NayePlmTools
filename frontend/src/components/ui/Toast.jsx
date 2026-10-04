import React, { useEffect } from 'react'
import { CheckCircle2, AlertTriangle, X } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.93 通知条修正
 * 1. 位置：组件自己写死 `fixed top-8 right-8`，而 App 外层已经有一个 fixed 容器在排队——
 *    结果是多条通知全落在同一个坐标上互相压字。现在只留容器定位，条目本身走正常流。
 * 2. 时长：原来一律 3 秒即逝，且没有关闭入口。错误类通知（安装失败、序列中止）在电视端
 *    3 秒根本来不及读完，改成分级驻留：成功 4s、错误 9s，并且随时可以用 OK/点击手动关掉。
 * 3. 文本用 break-words 收住，长错误信息不再把弹窗顶出屏幕。 */
const Toast = ({ message, type = 'success', onClose }) => {
  const { t } = useTranslation()

  useEffect(() => {
    const ms = type === 'error' ? 9000 : 4000
    const timer = setTimeout(onClose, ms)
    return () => clearTimeout(timer)
  }, [onClose, type])

  return (
    <div className={cn(
      "pointer-events-auto flex items-center p-4 rounded-2xl border shadow-2xl max-w-[min(92vw,28rem)]",
      type === 'success' ? "bg-emerald-950/90 border-emerald-500/50 text-emerald-400" : "bg-red-950/90 border-red-500/50 text-red-400"
    )}>
      {type === 'success' ? <CheckCircle2 className="w-6 h-6 shrink-0 mr-3" /> : <AlertTriangle className="w-6 h-6 shrink-0 mr-3" />}
      <span className="font-bold uppercase tracking-tight flex-1 min-w-0 break-words">{message}</span>
      <button
        onClick={onClose}
        className="icon-btn shrink-0 ml-2"
        title={t("app.buttons.close", "Close")}
        aria-label={t("app.buttons.close", "Close")}
      >
        <X />
      </button>
    </div>
  )
}

export default Toast
