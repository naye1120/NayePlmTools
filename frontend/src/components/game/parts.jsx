import React from 'react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.112 游戏模块四个弹窗共用的小零件
 *
 * 抽出来的理由很直接：详情 / 存档 / 多媒体 / 改名四个弹窗里，「深色芯片徽标」「键值对一格」
 * 「带标题的子板块」「表单一行」这四样东西各出现 3~8 次。就地复制会让同一个徽标在四个弹窗里
 * 有三种圆角和两种透明度 —— v0.5.2.103 刚把全站的徽标收敛成「深色芯片 + 同色字」，这里必须接住。
 *
 * 配色全部读站内既有的语义档（--color-*、ps-blue），一个硬编码颜色都不新增，
 * 所以浅色主题由 light-theme.css 自动接管，这四个弹窗不用单独适配。
 */

/* ── 芯片徽标：与 GameSection 磁贴上的平台/格式徽标同一档 ──
 * 底一律压暗（盖在任意封面上都可读、两种主题都不用翻），色相只由字与那根发丝线传达。
 * [CN-FORK] v0.5.2.113 收 className：改名页要把「待应用」这颗芯片压在预览图角上，
 * 就地再写一颗同色芯片就会绕开这里唯一的定义（.103 刚把全站徽标收敛成一种样子）。 */
const CHIP_TONE = {
  info: '#58a6ff',
  ok: '#3fb950',
  warn: '#e3b341',
  danger: '#f85149',
  muted: '#8b949e',
}
export function Chip({ tone = 'muted', children, title, className }) {
  const c = CHIP_TONE[tone] || CHIP_TONE.muted
  return (
    <span
      title={title}
      className={cn(
        'chip-xs inline-flex items-center px-1.5 rounded-md panel-micro font-semibold leading-none tracking-wide border shrink-0',
        className
      )}
      style={{ color: c, borderColor: `${c}59`, backgroundColor: 'rgba(8, 10, 14, 0.72)' }}
    >
      {children}
    </span>
  )
}

/* ── 弹窗内的子板块 ──
 * 刻意**不用** ui/Section：Section 是 .panel-card（页面级板块），套在已经是 glass-panel 的弹窗里
 * 会叠出两层卡片边。这里用「inset 底 + 一根发丝线」的凹下档，与弹窗底拉开一层就够。 */
export function Block({ title, icon: Icon, actions, children, className }) {
  return (
    <section className={cn('rounded-xl border border-white/10 bg-white/[0.03] overflow-hidden', className)}>
      {(title || actions) && (
        <header className="flex items-center justify-between gap-3 px-4 py-2.5 border-b border-white/10">
          <div className="flex items-center gap-2 min-w-0">
            {Icon && <Icon className="w-4 h-4 text-ps-blue shrink-0" />}
            {title && <h4 className="panel-title text-white truncate">{title}</h4>}
          </div>
          {actions && <div className="flex items-center gap-1.5 shrink-0">{actions}</div>}
        </header>
      )}
      <div className="p-4">{children}</div>
    </section>
  )
}

/* ── 表单一行：标签在上、控件在下 ──
 * 不用「标签在左」的横向排布，因为电视端控件会被 --ctl-h 抬到 51px，横向排布在窄屏上
 * 标签会把控件挤到不足 8 个字符宽。 */
export function Field({ label, hint, children, className }) {
  return (
    <label className={cn('block min-w-0', className)}>
      <span className="block panel-tiny font-semibold text-zinc-400 mb-1">{label}</span>
      {children}
      {hint && <span className="block panel-tiny text-zinc-500 mt-1 leading-snug">{hint}</span>}
    </label>
  )
}

/* ── 明细网格的一行（详情弹窗与存档/媒体信息格共用）──
 * 值为空显示「—」：空着会让人分不清「没有」还是「没读到」。
 * onOpen 给了就渲染成一个可点的链接式按钮（详情里「存档 3 个 ›」那种）。 */
export function GridRow({ label, value, mono, onOpen, openLabel, children, cols = 'grid-cols-[86px_minmax(0,1fr)]' }) {
  const text = value === undefined || value === null || value === '' ? '' : String(value)
  return (
    <div className={cn('grid gap-x-3 items-start', cols)}>
      <div className="text-zinc-500 panel-note leading-6">{label}</div>
      <div className={cn('min-w-0 break-all panel-note leading-6', mono && 'font-mono tracking-wide')}>
        {onOpen ? (
          <button
            type="button"
            onClick={onOpen}
            title={openLabel}
            /* 可见文字只有「2 个 ›」，读屏念出来完全不知道点了会去哪；title 只算兜底提示，
             * 真正决定可访问名的是内容。所以显式补一个 aria-label。 */
            aria-label={openLabel}
            className="inline-flex items-center gap-1 text-ps-blue hover:text-white transition-colors"
          >
            {text || '—'}
            <span aria-hidden="true" className="panel-micro">›</span>
          </button>
        ) : (children || (text || '—'))}
      </div>
    </div>
  )
}

/* ── 统计格（进度面板 / 列表行尾用）── */
export function Stat({ label, value, className }) {
  return (
    <div className={cn('min-w-0', className)}>
      <div className="text-zinc-500 panel-tiny">{label}</div>
      <div className="text-white tabular-nums truncate panel-note">{value}</div>
    </div>
  )
}

/* ── 进度条：删除 / 打包 / 上传共用 ──
 * indeterminate（百分比未知）时给一条来回扫的条，而不是停在 0% —— 统计阶段（measuring）
 * 后端本来就给不出百分比，显示 0% 会让用户以为卡死了。 */
export function ProgressBar({ percent, indeterminate, className }) {
  return (
    <div className={cn('h-1.5 rounded-full bg-white/10 overflow-hidden', className)}>
      {indeterminate ? (
        <div className="h-full w-1/3 bg-ps-blue animate-pulse" />
      ) : (
        <div
          className="h-full bg-ps-blue transition-[width] duration-300"
          style={{ width: `${Math.max(0, Math.min(100, percent || 0))}%` }}
        />
      )}
    </div>
  )
}

/* ── 弹窗顶部的说明条（错误 / 提示 / 进行中），三档语义色 ── */
const NOTE_TONE = {
  error: 'border-red-500/30 bg-red-500/10 text-red-400',
  warn: 'border-amber-500/30 bg-amber-500/10 text-amber-400',
  info: 'border-white/10 bg-white/[0.03] text-zinc-400',
  ok: 'border-emerald-500/30 bg-emerald-500/10 text-emerald-400',
}
export function Note({ tone = 'info', icon: Icon, children, className }) {
  return (
    <div className={cn('flex items-start gap-2 p-3 rounded-xl border panel-note leading-snug', NOTE_TONE[tone] || NOTE_TONE.info, className)}>
      {Icon && <Icon className="w-4 h-4 shrink-0 mt-0.5" />}
      <div className="min-w-0 break-words">{children}</div>
    </div>
  )
}

/* ── 空态（比 ui/EmptyState 紧凑，弹窗里空间有限）── */
export function MiniEmpty({ icon: Icon, children }) {
  return (
    <div className="flex flex-col items-center justify-center text-center py-8 border border-dashed border-white/10 rounded-xl">
      {Icon && <Icon className="w-7 h-7 text-white/15" />}
      <p className="panel-note text-zinc-500 mt-2">{children}</p>
    </div>
  )
}
