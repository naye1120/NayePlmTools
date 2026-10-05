import React, { useEffect, useRef, useState } from 'react'
import { X } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.93 弹窗骨架重写（只动交互与盒模型，配色仍走 .glass-panel / rail 档）
 * 修掉的四件事：
 * 1. 原来 footer 用 `flex gap-4`，PS5 老内核不支持 flex gap，两个按钮是贴死的；改 .stack-x（margin 实现）。
 * 2. 原来只要传了 onClose 就不画任何关闭入口，也不认 Esc/返回键——电视端打开弹窗就只能二选一。
 * 3. 原来面板 overflow-hidden，内容一长就被切掉且遥控器滚不出去。
 * 4. 原来打开后焦点还在弹窗背后，遥控器第一次按方向键没有起点；现在打开即把焦点送进面板。
 * dismissible=false 给「不可打断」的进行中提示用（安装/上传时）。 */
/* [CN-FORK] v0.5.2.112 新增 containerClass：只作用在**最外层遮罩**上，用来调层级。
 * 起因是一个真实事故：App 级的 showConfirm 和页面内的弹窗（游戏详情等）都是 z-[1000]，
 * 页面内弹窗在 DOM 里更靠后，于是**确认框被整个盖住、点不到也看不见** ——
 * 用户点「删除源」会以为按钮坏了。层级约定见 App.jsx：页面内弹窗 1000 / 预览浮层 1100 /
 * App 级浮层 1900 / 通知条 2000 / 全屏加载与日志 9999。 */
/* [CN-FORK] v0.5.2.112 打开栈：同一时刻可能叠着两层弹窗（游戏详情 + 删除确认框、
 * 媒体预览 + 删除确认框）。两个 Modal 都往 window 上挂 Escape 监听，
 * 于是**一次 Esc 会把两层一起关掉** —— 用户只是想取消确认，结果详情页也没了。
 * 用一个模块级数组记下打开顺序，只让最上层那颗处理 Escape。
 * 后挂载 = 后压栈，与「App 级浮层 z 更高」的约定方向一致。 */
const openStack = []

/* [CN-FORK] v0.5.2.112 面板形态（layout="panel"）：**从侧栏右边铺到屏幕右边**。
 *
 * 用户口径：「存档管理页面打开后铺满右边整个区域。多媒体管理也是铺满右边。修改名称也是铺满。」
 * 起因很实在：这三块内容都是**列表/表单为主**的「工作区」，塞进一个 max-w-xl 的居中卡片里，
 * 一屏只看得到两三行；而它们右边本来就是一大片空地（PC 上正文列宽上限 1800px，电视端更宽）。
 *
 * 做法：不动交互（打开栈、Esc、焦点、遮罩点击全部沿用），只换盒模型 ——
 *   · 外层从 `inset-0 居中 + p-8` 改成 `inset-y-0 right-0 + left: var(--sidebar-w)`，
 *     所以它天然避开侧栏、跟着侧栏展开/收起走（--sidebar-w 见 index.css）；
 *   · 这个形态**不画遮罩**：面板已经盖满右侧，遮罩只会把还露在外面的侧栏也压黑，
 *     反而让人不敢点侧栏（侧栏此时本来就该是可点的）；
 *   · 面板内部改成 flex 列：标题栏粘顶、内容区自己滚（`flex-1 min-h-0 overflow-y-auto`），
 *     footer 钉底。原来是「整个面板一起滚」，内容一长标题和按钮就都滚出去了。
 *
 * 为什么不换成路由页：这三个入口都是**针对某一款游戏**的临时工作区（进来干完就关），
 * 做成路由会把「当前是哪个游戏」也塞进 URL，返回键与遥控器的语义都得跟着改。 */
const Modal = ({ show, title, children, onClose, footer, dismissible = true, className, containerClass, layout = 'dialog' }) => {
  const { t } = useTranslation()
  const panelRef = useRef(null)
  /* 这一层实例在打开栈里的身份。用 useState 的惰性初始值而不是 ref：
   * 「渲染期给 ref 赋初值」是 React 明确不建议的写法（react-hooks/refs 会报），
   * 而这里要的本来就是一个「每个组件实例一个、永不变」的稳定对象。 */
  const [token] = useState(() => ({}))

  /* 打开/关闭时维护全局栈：只与 show/token 绑定，不因 onClose 变化而抖动。
   * 病因：父组件重渲染 → onClose 引用变 → effect cleanup 把 token 从栈里抽掉 →
   * effect body 再把它压回来。由于 React 先跑全部 cleanup 再跑全部 body，
   * 后打开的确认框先压栈、先打开的详情框后压栈， Escape 时「最上层」判反，
   * 结果 Esc 关掉的是底下那层详情框，确认框还在。 */
  useEffect(() => {
    if (!show) return undefined
    openStack.push(token)
    return () => {
      const i = openStack.indexOf(token)
      if (i >= 0) openStack.splice(i, 1)
    }
  }, [show, token])

  /* [CN-FORK] v0.5.2.112 焦点进出：打开时把焦点送进面板，关闭时还给「打开它的那颗按钮」。
   *
   * 这两件事**必须单独一个 effect、且只跟 show 走**。踩过的坑：一开始把它们和下面那个
   * Escape 监听写在同一条里，依赖数组里带着 onClose —— 而调用方传进来的 onClose 往往是
   * 内联函数（GameDetailModal 的 `close`），父组件每重渲染一次就换一个引用，于是
   * 「cleanup 把焦点还给 opener → effect 再聚焦面板」这组动作在弹窗**还开着的时候**反复执行；
   * 只要有一次 cleanup 之后 effect 没跟上（比如 show 的判断恰好落在中间），焦点就留在了外面。
   * 实测症状：电视端遥控器打开详情后光圈还停在磁贴上，方向键一动就把弹窗绕过去了。
   *
   * 所以：opener 用 ref 记（不参与依赖），还原只发生在 show 由真变假的那一次 cleanup 上。
   * 还焦点这件事对遥控器是刚需 —— 不还的话，下一次按方向键会从「全屏最左上」重新落焦，
   * 用户直接从游戏页被甩到侧栏。 */
  const openerRef = useRef(null)
  useEffect(() => {
    if (!show) return undefined
    openerRef.current = document.activeElement
    if (panelRef.current) {
      try { panelRef.current.focus() } catch { /* 老内核偶发抛错，忽略 */ }
    }
    return () => {
      const prev = openerRef.current
      openerRef.current = null
      if (prev && prev !== document.body && prev !== document.documentElement && document.contains(prev)) {
        try { prev.focus() } catch { /* 忽略 */ }
      }
    }
  }, [show])

  useEffect(() => {
    if (!show) return undefined
    if (!dismissible) return undefined
    const onKey = (e) => {
      if (e.key !== 'Escape') return
      if (openStack[openStack.length - 1] !== token) return   // 上面还压着一层，Esc 归它
      e.stopPropagation()                                     // 别让底下那层也收到同一次 Esc
      if (onClose) onClose()
    }
    window.addEventListener('keydown', onKey)
    return () => {
      window.removeEventListener('keydown', onKey)
    }
  }, [show, dismissible, onClose, token])

  if (!show) return null

  const isPanel = layout === 'panel'

  return (
    <div
      className={cn(
        "fixed z-[1000] flex",
        isPanel
          ? "panel-sheet inset-y-0 right-0 flex-col"
          : "inset-0 items-center justify-center p-4 md:p-8",
        containerClass
      )}
      /* 面板形态靠 --sidebar-w 贴住侧栏右边（见 index.css）。用内联 style 而不是 Tailwind 任意值
       * 的写法 `left-[var(--sidebar-w)]`：后者要 JIT 扫到才会生成，而这一个类在构建期是动态拼的，
       * 漏生成就是「面板压在侧栏上」这种一眼可见的错。 */
      style={isPanel ? { left: 'var(--sidebar-w)' } : undefined}
    >
      {/* 遮罩只给居中弹窗用：面板已经盖满右侧，再压一层黑只会把露在外面的侧栏也闷掉 */}
      {!isPanel && <div className="absolute inset-0 bg-black/80" onClick={dismissible ? onClose : undefined} />}
      <div
        ref={panelRef}
        tabIndex={-1}
        role="dialog"
        aria-modal="true"
        aria-label={typeof title === 'string' ? title : undefined}
        className={cn(
          "relative glass-panel w-full border-white/10 flex flex-col min-h-0",
          isPanel
            /* [CN-FORK] v0.5.2.112 面板**四角全平**（用户：「刚才铺满的几个页面圆角全部取消」）。
             * 上一版留了 `rounded-l-ps-3xl`，本意是让面板像一张贴着右边缘的卡片，但面板左边缘
             * 正好压在侧栏分界线上，那道圆角会被读成「侧栏右下角缺了一块」而不是「一张卡片」。
             * 既然要铺满，就铺成一块干净的直角面板。 */
            ? "h-full max-w-none rounded-none border-l overflow-hidden"
            : "max-w-xl p-6 md:p-10 rounded-ps-3xl max-h-full overflow-y-auto",
          className
        )}
      >
        <div className={cn(
          "flex items-start justify-between gap-3",
          isPanel ? "shrink-0 px-6 md:px-8 py-4 md:py-5 border-b border-white/10" : "mb-6 md:mb-8"
        )}>
          <h3 className="text-2xl md:text-3xl font-black text-white uppercase italic tracking-tighter min-w-0 mr-4">{title}</h3>
          {dismissible && (
            <button
              onClick={onClose}
              className="icon-btn shrink-0 hover:bg-red-500/10 hover:text-red-500"
              title={t("app.buttons.close", "Close")}
              aria-label={t("app.buttons.close", "Close")}
            >
              <X />
            </button>
          )}
        </div>
        <div className={cn(
          "min-h-0",
          isPanel
            ? "flex-1 overflow-y-auto custom-scrollbar px-6 md:px-8 py-5 md:py-6"
            : "text-zinc-300 mb-8 md:mb-10 text-lg md:text-xl font-medium leading-relaxed"
        )}>
          {children}
        </div>
        {footer && (
          <div className={cn("stack-x", isPanel && "shrink-0 px-6 md:px-8 py-4 border-t border-white/10")}>{footer}</div>
        )}
      </div>
    </div>
  )
}

export default Modal
