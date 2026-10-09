import React from 'react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.85 侧栏选中态改「藏蓝底 + 左侧动作色竖条」：底色与文字走 index.css 的
 * .nav-item / .is-active（CSS 变量驱动，浅色主题在 light-theme.css 翻变量即可整组跟随），
 * 不再用整块实心主色——主色亮橙只留给竖条和页面上的主操作按钮。 */
const NavButton = ({ active, onClick, icon: Icon, label, mobileLabel, className, sidebar, sidebarExpanded, isDonate }) => (
  /* [CN-FORK] v0.5.2.99 手机底部栏：等宽格子保留（七项全可见是对的），但把「每格一个圆角框」去掉。
   * 公共类串里带着上游的 border，旧写法在移动分支补了 border-none 抵消，.96 重写时把这半句丢了，
   * 于是 7 个入口各画一圈 1px 亮边框，看起来像一排卡片而不是一条导航栏。
   * 分隔线同理：横向滚动条时代它用来切分挤在一起的条目，等分格子里每格左缘一条线只会把宽度吃掉。 */
  <div className="flex items-center min-w-0 w-full h-full">
    <button
      onClick={onClick}
      aria-current={active ? 'page' : undefined}
      className={cn(
        "flex items-center transition-all border group relative outline-none",
        sidebar
          ? cn("nav-item w-full p-4 mb-2 rounded-2xl border-none", active && !isDonate && "is-active", sidebarExpanded ? "justify-start space-x-4" : "justify-center")
          : "flex-col justify-center w-full h-full min-w-0 px-1 border-none",
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
          sidebarExpanded ? "opacity-100 w-auto font-bold text-[1.03125rem]" : "opacity-0 w-0" /* 仅侧栏标签字号（16.5px @ 16px 根字号）。
            [CN-FORK] v0.5.2.156 由 text-[16.5px] 改成 rem：写死 px 不跟着 html[data-device="ps5"]
            的根字号缩，电视端整体缩小后只有这一处不缩，看着比周围大一圈。16.5/16 = 1.03125rem，
            桌面档的取值一字未变。 */
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
