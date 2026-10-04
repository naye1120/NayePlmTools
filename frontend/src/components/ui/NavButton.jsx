import React from 'react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.85 侧栏选中态改「藏蓝底 + 左侧动作色竖条」：底色与文字走 index.css 的
 * .nav-item / .is-active（CSS 变量驱动，浅色主题在 light-theme.css 翻变量即可整组跟随），
 * 不再用整块实心主色——主色亮橙只留给竖条和页面上的主操作按钮。 */
const NavButton = ({ active, onClick, icon: Icon, label, mobileLabel, className, sidebar, sidebarExpanded, showSeparator, isDonate }) => (
  /* [CN-FORK] v0.5.2.96 手机底部栏改成等宽格子（App.jsx 里给列数）：
   * 七个入口挤在横向滚动条里，等于把一半页面藏在拇指够不着的地方；格子均分后全部一屏可见。
   * 外层撑满整条栏的高度（h-full），点击区就是「图标 + 标签 + 上下留白」的整块，不再是一小圈。 */
  <div className="flex items-center min-w-0 w-full h-full">
    {showSeparator && <div className="w-px h-6 bg-white/10" />}
    <button
      onClick={onClick}
      aria-current={active ? 'page' : undefined}
      className={cn(
        "flex items-center transition-all border group relative outline-none",
        sidebar
          ? cn("nav-item w-full p-4 mb-2 rounded-2xl border-none", active && !isDonate && "is-active", sidebarExpanded ? "justify-start space-x-4" : "justify-center")
          : "flex-col justify-center w-full h-full min-w-0 px-1 rounded-xl",
        active
          ? (sidebar
            ? (isDonate ? "bg-red-600 text-white" : "")
            : (isDonate ? "text-red-500" : "text-ps-blue font-black"))
          : (isDonate ? "text-red-500/60" : (sidebar ? "" : "bg-transparent text-zinc-400 hover:text-white")),
        className
      )}
    >
      <Icon className={cn(
        "w-6 h-6 shrink-0 group-hover:scale-110 transition-transform",
        active ? (sidebar ? "text-current" : (isDonate ? "text-red-500" : "text-ps-blue")) : (isDonate ? "text-red-500" : "")
      )} />
      {sidebar ? (
        <span className={cn(
          "uppercase tracking-tighter transition-all duration-300 whitespace-nowrap overflow-hidden",
          sidebarExpanded ? "opacity-100 w-auto font-bold text-[16.5px]" : "opacity-0 w-0" /* 仅侧栏标签字号，较正文 text-base 大 2px（不影响全局）*/
        )}>
          {label}
        </span>
      ) : (
        <span className="max-w-full min-w-0 truncate text-[10px] font-bold uppercase tracking-tighter mt-0.5">
          {/* 窄屏只显示短标签，完整页面名留给读屏；aria-current 已给侧栏，底部栏同样标出当前页 */}
          <span className="sr-only">{label}</span>
          {mobileLabel || label}
        </span>
      )}
    </button>
  </div>
)

export default NavButton
