import React from 'react'
import { ArrowLeft } from 'lucide-react'
import { cn } from '../../utils/helpers'

/* [CN-FORK] v0.5.2.94 页头统一 + 粘顶
 * 此前页头不粘顶：列表一长，「这一页是什么、主操作在哪」立刻滚出视野，电视端和手机端
 * 要来回滚找按钮。现在页头粘在滚动容器顶部，底色用实色 rail 档（v0.5.2.88 的约束：
 * 浮层底必须实色，否则滚过的内容会透出来），左右用负外边距吃掉 <main> 的内边距，
 * 让那根发丝分界线铺满整宽。字号层级沿用既有 .page-head，不改配色。
 * back：二级页（来源管理 / 从 USB 导入）的返回钮放页头左侧，遥控器一进一出都在同一档。 */
const PageHead = ({ title, desc, actions, back, backLabel, bleed = true, className }) => (
  <div className={cn("head-bar", !bleed && "head-bar--flush", className)}>
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
          {desc && <p className="page-desc max-w-2xl">{desc}</p>}
        </div>
      </div>
      {actions && <div className="page-actions">{actions}</div>}
    </div>
  </div>
)

export default PageHead
