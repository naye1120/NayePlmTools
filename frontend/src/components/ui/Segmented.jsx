import React from 'react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.97 全站唯一的分段选择控件（「N 选 1，选中项就是当前值」）
 * 病灶：同一角色此前有四份手写实现——外观三档（ThemeToggle 自带轨道）、
 * 管理页排序三档（三枚裸 .btn 拼 space-x-2）、温控档位三档（.btn + sel-chip）、
 * 自启延时三档（.btn + ml-2）。轨道有无、圆角、选中档、命中高度各自为政，
 * 用户在同一屏里会看到三种「这是一组互斥选项」的表达。
 * 现在只有一处定义：轨道读 inset 档 + 一根发丝线，选中项读契约里唯一的实心选中档 .sel-chip，
 * 每档命中高度统一走 --ctl-h。颜色一个都没改（四处原本也都是这几个档）。
 * role=radiogroup/radio 是故意的：读屏会念「共 3 项，当前第 2 项」，遥控器方向键也能顺着走。 */
const Segmented = ({ options, value, onChange, label, className, itemClassName }) => (
  <div role="radiogroup" aria-label={label} className={cn("segmented-track", className)}>
    {options.map((o) => {
      const active = o.value === value
      const Icon = o.icon
      return (
        <button
          key={String(o.value)}
          type="button"
          role="radio"
          aria-checked={active}
          title={o.title || undefined}
          onClick={() => onChange(o.value)}
          className={cn(
            "segmented-item flex items-center justify-center min-w-0 rounded-lg transition-colors",
            active ? "sel-chip" : "text-zinc-500 hover:text-white",
            itemClassName
          )}
        >
          {Icon && <Icon className="w-4 h-4 shrink-0" />}
          {o.label && <span className="text-xs font-bold uppercase tracking-tight truncate">{o.label}</span>}
        </button>
      )
    })}
  </div>
)

export default Segmented
