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
/* [CN-FORK] v0.5.2.142 加 `o.disabled` / `o.title`：远程位置类型里的 SMB 要**看得见但点不动**
 * （SDK 那份 libcurl 没编 smb，选了也传不出去）。直接从选项表里删掉是更省事，但老用户
 * 存的就是 mode:"smb" —— 选项没了会变成「一个都没选中」，用户根本不知道发生了什么。
 * 留着一个禁用的档 + 一句原因，才是能自解释的。 */
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
          disabled={!!o.disabled}
          aria-disabled={o.disabled ? 'true' : undefined}
          title={o.title || undefined}
          onClick={() => { if (!o.disabled) onChange(o.value) }}
          className={cn(
            "segmented-item flex items-center justify-center min-w-0 rounded-lg transition-colors",
            o.disabled
              ? "opacity-40 cursor-not-allowed text-zinc-600"
              : (active ? "sel-chip" : "text-zinc-500 hover:text-white"),
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
