import React from 'react'
import { ArrowLeft } from 'lucide-react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.94 页头统一；v0.5.2.102 起不再粘顶
 * 这一条本来是为电视/手机「滚远了找不到主操作」而加的 sticky，用户实测后明确否掉：
 * 「所有 head-bar 要跟随页面一起滚动」。于是 .head-bar 只留横条形状（实色 rail 底 +
 * 出血负边距 + 底部分界线），钉不钉住这一档交还给滚动本身。
 * back：二级页（来源管理 / 从 USB 导入）的返回钮放页头左侧，遥控器一进一出都在同一档。
 *
 * ── 页面容器约定（v0.5.2.99 起全站统一，新增页面照此写）──
 * 页面根节点一律 <div className="space-y-6">：不限宽、不居中、不另加底部内边距
 * （main 已经给了 px-6/md:px-16 与 md:pb-12）。页头永远吃默认出血，负外边距正好等于
 * main 的左右内边距，那条分界线才铺到边。
 * 想要更窄的阅读列，请限「板块内部」的内容宽度，不要限页面 —— 页面一旦 max-w 居中，
 * 就和并排的其他菜单页对不齐，页头也得跟着内缩（原来那个 bleed={false} 开关已删）。
 * 唯一的结构性例外是 iframe 页（PKG安装：main 去内边距、整页铺满，没有页头）。
 * 每个页面有且只有一个 <PageHead>，放在根容器最前面；列/栏内部不要再塞第二个页头，
 * 负外边距会伸进相邻列把别人的内容盖住（自动加载页此前就是这样，v0.5.2.99 已上移统一）。 */
const PageHead = ({ title, desc, actions, back, backLabel, className }) => (
  <div className={cn("head-bar", className)}>
    <div className="page-head">
      <div className="flex items-start min-w-0">
        {back && (
          <button
            onClick={back}
            title={backLabel}
            aria-label={backLabel}
            className="icon-btn shrink-0 mr-2 -ml-2"
          >
            <ArrowLeft />
          </button>
        )}
        <div className="min-w-0">
          <h2 className="text-2xl font-bold text-white tracking-tight">{title}</h2>
          {/* [CN-FORK] v0.5.2.98 desc 容器从 <p> 改 <div>：autoload 页往里放了「保存中」指示块
              （div），<p> 嵌 <div> 是非法 HTML，老内核会自己拆标签导致排版错位。
              v0.5.2.99：纯文字说明在窄屏限两行（.page-desc--clamp），否则长解释在 450px 宽下
              排到 8 行，把粘顶页头撑成屏幕的三分之一；desc 传组件时不加这个类。 */}
          {desc && (
            <div className={cn("page-desc max-w-2xl", typeof desc === "string" && "page-desc--clamp")}>
              {desc}
            </div>
          )}
        </div>
      </div>
      {actions && <div className="page-actions">{actions}</div>}
    </div>
  </div>
)

export default PageHead
