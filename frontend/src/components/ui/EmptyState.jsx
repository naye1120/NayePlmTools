import React from 'react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.94 空态统一
 * 此前全站有 9 处以上各页自写的空态（虚线框 / 纯文字 / italic 大写 / 带感叹号角标的），
 * 字号、图标明暗、留白全不一样，同一个「这里没有东西」在相邻两个板块里长得像三种东西。
 * 现收成一个：虚线框 + 淡图标 + 一句标题 + 可选说明 + 可选就地操作。 */
const EmptyState = ({ icon: Icon, iconClassName, title, desc, action, dashed = true, className }) => (
  <div className={cn(
    "px-4 py-10 flex flex-col items-center justify-center text-center",
    dashed && "border-2 border-dashed border-white/10 rounded-xl",
    className
  )}>
    {Icon && <Icon className={cn("w-9 h-9 text-white/15", iconClassName)} />}
    <p className={cn("text-sm font-semibold text-white", Icon && "mt-3")}>{title}</p>
    {desc && <p className="text-xs text-zinc-500 max-w-md leading-relaxed mt-1.5">{desc}</p>}
    {action && <div className="mt-4">{action}</div>}
  </div>
)

export default EmptyState
