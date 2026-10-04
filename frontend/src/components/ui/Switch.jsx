import React from 'react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.94 全站唯一的开关控件
 * 病灶：设置页五个开关各自抄一份 `w-14 h-7 md:w-20 md:h-10 … translate-x-7 md:translate-x-10`
 * 的滑块 markup（含一份窄屏/宽屏双档尺寸），活动进程页又是另一套 h-6 w-11，两处已经长得不一样。
 * 现在统一成这一个：尺寸全部由尺度令牌给（--switch-w / --switch-h / --knob-d），
 * PS5 档在 index.css 里整体抬大一档，电视上隔着三米也看得出开没开。
 * 颜色仍走既有契约：开启=实心橙（--color-ps-blue 即选中档），关闭=内嵌件底，旋钮恒为白。
 * 旋钮靠 ml-auto 推到轨道右端，不用算 translate 距离，也就不跟着尺寸改要同步两处。 */
const Switch = ({ checked, onChange, label, disabled }) => (
  <button
    type="button"
    role="switch"
    aria-checked={!!checked}
    aria-label={label}
    disabled={disabled}
    onClick={() => onChange(!checked)}
    className={cn(
      "switch-track flex items-center rounded-full shrink-0 transition-colors p-[0.1875rem]",
      checked ? "bg-ps-blue" : "bg-white/10"
    )}
  >
    <span
      aria-hidden="true"
      className={cn(
        "switch-knob rounded-full bg-white transition-all duration-200",
        checked ? "ml-auto" : "ml-0"
      )}
    />
  </button>
)

export default Switch
