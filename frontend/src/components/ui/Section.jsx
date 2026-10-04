import React from 'react'
import { cn } from '../../utils/helpers'

/**
 * [CN-FORK] v0.5.2.94 统一板块容器
 *
 * 病灶：同一个「板块」角色此前有三套写法并行——`.panel-card`（只有管理 Payload 在用）、
 * 手写的 `bg-white/5 border border-white/10 rounded-2xl p-5 md:p-6`（常用入口/温控/游戏管理/PKG 卡）、
 * 以及 `bg-white/[0.03] ... rounded-ps-3xl`（来源管理）。圆角 0.75rem / 1rem / 2rem 三种，
 * 板块头有的带图标盒有的没有，操作位一会儿在标题行一会儿在板块底部。
 * 本组件把「板块 = .panel-card + .panel-head（图标盒 / 标题 / 计数 / 右侧操作）」定成唯一形状；
 * 底色仍读 inset 档（v0.5.2.90 的并档结论），配色一个字节都没动。
 */
const Section = ({ id, icon: Icon, title, count, actions, children, bodyClass, className, headless, bodyPad }) => (
  <section id={id} className={cn("panel-card", className)}>
    {!headless && (title || Icon || actions) && (
      <header className="panel-head">
        <div className="flex items-center min-w-0">
          {Icon && (
            <div className="p-2 rounded-lg shrink-0">
              <Icon className="w-4 h-4 text-ps-blue" />
            </div>
          )}
          {title && <h3 className="text-sm font-semibold text-white truncate">{title}</h3>}
          {/* 计数徽章：与标题同档，不再各页自造 (n) / 胶囊 / 文字三种写法 */}
          {count !== undefined && count !== null && (
            <span className="ml-3 bg-white/5 px-2.5 py-0.5 rounded-full text-zinc-500 font-medium text-xs shrink-0 tabular-nums">
              {count}
            </span>
          )}
        </div>
        {actions && <div className="flex items-center flex-none ml-3">{actions}</div>}
      </header>
    )}
    <div className={cn(bodyPad === false ? "" : "p-4", bodyClass)}>{children}</div>
  </section>
)

export default Section
