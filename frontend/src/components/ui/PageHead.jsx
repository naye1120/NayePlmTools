import React from 'react'
import { ArrowLeft } from 'lucide-react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.94 页头统一 + 粘顶
 * 此前页头不粘顶：列表一长，「这一页是什么、主操作在哪」立刻滚出视野，电视端和手机端
 * 要来回滚找按钮。现在页头粘在滚动容器顶部，底色用实色 rail 档（v0.5.2.88 的约束：
 * 浮层底必须实色，否则滚过的内容会透出来），左右用负外边距吃掉 <main> 的内边距，
 * 让那根发丝分界线铺满整宽。字号层级沿用既有 .page-head，不改配色。
 * back：二级页（来源管理 / 从 USB 导入）的返回钮放页头左侧，遥控器一进一出都在同一档。
 *
 * ── 页面容器约定（v0.5.2.99 起全站统一，新增页面照此写）──
 * 页面根节点一律 <div className="space-y-6">：不限宽、不居中、不另加底部内边距
 * （main 已经给了 px-6/md:px-16 与 md:pb-12）。页头永远吃默认出血，负外边距正好等于
 * main 的左右内边距，粘顶条的分界线才铺到边。
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
