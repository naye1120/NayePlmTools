import React from 'react'
import { Loader2 } from 'lucide-react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.94 载入态统一：全站原有 4 套 spinner 写法
 * （ps5-robust-spinner 全屏、Loader2 w-8、RefreshCw w-5 自转、只有文字没有图形），
 * 收成一个板块内联档；全屏启动遮罩仍保留在 App.jsx（那是打断式的，语义不同）。 */
const LoadingState = ({ label, className }) => (
  <div className={cn("py-12 flex flex-col items-center justify-center", className)}>
    <Loader2 className="w-7 h-7 text-ps-blue animate-spin" />
    {label && <p className="text-zinc-500 text-sm mt-3">{label}</p>}
  </div>
)

export default LoadingState
