import React, { useEffect, useRef } from 'react'
import { X } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.93 弹窗骨架重写（只动交互与盒模型，配色仍走 .glass-panel / rail 档）
 * 修掉的四件事：
 * 1. 原来 footer 用 `flex gap-4`，PS5 老内核不支持 flex gap，两个按钮是贴死的；改 .stack-x（margin 实现）。
 * 2. 原来只要传了 onClose 就不画任何关闭入口，也不认 Esc/返回键——电视端打开弹窗就只能二选一。
 * 3. 原来面板 overflow-hidden，内容一长就被切掉且遥控器滚不出去。
 * 4. 原来打开后焦点还在弹窗背后，遥控器第一次按方向键没有起点；现在打开即把焦点送进面板。
 * dismissible=false 给「不可打断」的进行中提示用（安装/上传时）。 */
const Modal = ({ show, title, children, onClose, footer, dismissible = true, className }) => {
  const { t } = useTranslation()
  const panelRef = useRef(null)

  useEffect(() => {
    if (!show) return undefined
    if (panelRef.current) {
      try { panelRef.current.focus() } catch (e) { /* 老内核偶发抛错，忽略 */ }
    }
    if (!dismissible) return undefined
    const onKey = (e) => { if (e.key === 'Escape' && onClose) onClose() }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [show, dismissible, onClose])

  if (!show) return null

  return (
    <div className="fixed inset-0 z-[1000] flex items-center justify-center p-4 md:p-8">
      <div className="absolute inset-0 bg-black/80" onClick={dismissible ? onClose : undefined} />
      <div
        ref={panelRef}
        tabIndex={-1}
        role="dialog"
        aria-modal="true"
        aria-label={typeof title === 'string' ? title : undefined}
        className={cn(
          "relative glass-panel w-full max-w-xl p-6 md:p-10 rounded-ps-3xl border-white/10 max-h-full overflow-y-auto",
          className
        )}
      >
        <div className="flex items-start justify-between mb-6 md:mb-8">
          <h3 className="text-2xl md:text-3xl font-black text-white uppercase italic tracking-tighter min-w-0 mr-4">{title}</h3>
          {dismissible && (
            <button
              onClick={onClose}
              className="icon-btn shrink-0 hover:bg-red-500/10 hover:text-red-500"
              title={t("app.buttons.close", "Close")}
              aria-label={t("app.buttons.close", "Close")}
            >
              <X />
            </button>
          )}
        </div>
        <div className="text-zinc-300 mb-8 md:mb-10 text-lg md:text-xl font-medium leading-relaxed">
          {children}
        </div>
        {footer && <div className="stack-x">{footer}</div>}
      </div>
    </div>
  )
}

export default Modal
