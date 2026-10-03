import React from 'react'
import { cn, isPS5 } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.85 侧栏选中态改「藏蓝底 + 左侧动作色竖条」：底色与文字走 index.css 的
 * .nav-item / .is-active（CSS 变量驱动，浅色主题在 light-theme.css 翻变量即可整组跟随），
 * 不再用整块实心主色——主色亮橙只留给竖条和页面上的主操作按钮。 */
const NavButton = ({ active, onClick, icon: Icon, label, mobileLabel, className, sidebar, sidebarExpanded, showSeparator, isDonate }) => {
  return (
    <div className="flex items-center flex-1 md:flex-none min-w-max">
      {showSeparator && <div className="w-px h-6 bg-white/10 md:hidden" />}
      <button
        onClick={onClick}
        className={cn(
          "flex items-center transition-all border group relative outline-none",
          sidebar
            ? cn("nav-item w-full p-4 mb-2 rounded-2xl border-none", active && !isDonate && "is-active", sidebarExpanded ? "justify-start space-x-4" : "justify-center")
            : (isPS5 ? "flex-row space-x-3 px-6 py-3 rounded-2xl" : "flex-col md:flex-row md:space-x-3 px-4 md:px-6 py-2 md:py-3 rounded-2xl border-none flex-1 md:flex-none"),
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
        <span className={cn(
          "uppercase tracking-tighter transition-all duration-300 whitespace-nowrap overflow-hidden",
          sidebar
            ? (sidebarExpanded ? "opacity-100 w-auto font-bold text-[16.5px]" : "opacity-0 w-0") /* [CN-FORK] 仅侧栏标签字号，较正文 text-base 大 2px（不影响全局）*/
            : (isPS5 ? "text-sm font-bold" : "text-[10px] md:text-sm")
        )}>
          <span className={cn((isPS5 || sidebar) ? "inline" : "hidden md:inline")}>{label}</span>
          {!isPS5 && !sidebar && <span className={cn("inline md:hidden", active ? (isDonate ? "text-red-500" : "text-ps-blue") : "font-medium text-zinc-500")}>{mobileLabel}</span>}
        </span>
      </button>
    </div>
  )
}

export default NavButton
