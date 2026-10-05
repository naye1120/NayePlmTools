import React from 'react'
import { cn } from '../../utils/helpers'

/**
 * [CN-FORK] v0.5.2.94 统一板块容器
 *
 * 病灶：同一个「板块」角色此前有三套写法并行——`.panel-card`（只有管理 Payload 在用）、
 * 手写的 `bg-white/5 border border-white/10 rounded-2xl p-5 md:p-6`（常用入口/温控/游戏管理/PKG 卡）、
 * 以及 `bg-white/[0.03] ... rounded-ps-3xl`（来源管理）。圆角 0.75rem / 1rem / 2rem 三种，
 * 板块头有的带图标盒有的没有，操作位一会儿在标题行一会儿在板块底部。
 * 本组件把「板块 = .panel-card + .panel-head（图标盒 / 标题 / 计数 / 右侧操作）」定成唯一形状。
 * 底色读 card 档（v0.5.2.100 的结论：板块浮起 = card，板块内的行/输入框 = inset 凹下；
 * v0.5.2.90 那次并到 inset 是半透明 tint 时代的事，实色板下叠不出层次）。
 * [CN-FORK] v0.5.2.103 舒展档：板块内边距 p-4(0.6rem) → p-5(0.75rem)。
 * [CN-FORK] v0.5.2.106 板块标题辨识度上调（用户：「所有这类子模块的标题辨识度都提高一些」）：
 *   标题从原子类 text-sm/font-semibold 换成语义类 .panel-title（0.85rem→1.05rem、600→700、
 *   +0.01em 字距，定义见 index.css）；图标同步从 w-4 h-4(16px) 放大到 1.15rem(≈18.4px) 与标题成比例；
 *   计数徽章从 text-zinc-500 提到 text-zinc-400（深底上原本太灰）。
 */
const Section = ({ id, icon: Icon, title, count, actions, children, bodyClass, className, headless, bodyPad }) => (
  <section id={id} className={cn("panel-card", className)}>
    {!headless && (title || Icon || actions) && (
      <header className="panel-head">
        <div className="flex items-center min-w-0">
          {Icon && (
            <div className="p-2 rounded-lg shrink-0">
              <Icon className="w-[1.15rem] h-[1.15rem] text-ps-blue" />
            </div>
          )}
          {title && <h3 className="panel-title text-white truncate">{title}</h3>}
          {/* 计数徽章：与标题同档，不再各页自造 (n) / 胶囊 / 文字三种写法 */}
          {count !== undefined && count !== null && (
            <span className="ml-3 bg-white/5 px-2.5 py-0.5 rounded-full text-zinc-400 font-medium text-xs shrink-0 tabular-nums">
              {count}
            </span>
          )}
        </div>
        {actions && <div className="flex items-center flex-none ml-3">{actions}</div>}
      </header>
    )}
    <div className={cn(bodyPad === false ? "" : "p-5", bodyClass)}>{children}</div>
  </section>
)

export default Section
