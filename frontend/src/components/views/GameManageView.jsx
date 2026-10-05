import React from 'react'
import { useTranslation } from 'react-i18next'
import PageHead from '../ui/PageHead' /* [CN-FORK] v0.5.2.94 页头统一 + 粘顶 */
import GameSection from './GameSection' /* [CN-FORK] 游戏管理板块 */

/* [CN-FORK] 「游戏管理」独立页：板块自「常用入口」页抽出，侧栏「常用配置」下方入口。
 * v0.5.2.94：标题/说明从板块内部上移到页面级 <PageHead>（粘顶），板块只保留自己的
 * 小标题与刷新，同一屏不再出现两个 24px 级标题。
 * v0.5.2.99：容器全站统一（见 PageHead 顶部约定），本根节点照 <PageHead> 的标准档来写。
 * v0.5.2.112：把 App 级的 addToast / showConfirm 透传给板块 —— 游戏详情里的「删除源」
 * 走的就是全局那个 danger 档确认弹窗，与本项目其余破坏性操作同一条路径，
 * 不自己再造第二套确认弹窗。 */
export default function GameManageView({ addToast, showConfirm }) {
  const { t } = useTranslation()
  return (
    <div className="space-y-6">
      <PageHead
        title={t('game.title', '游戏管理')}
      />
      <GameSection addToast={addToast} showConfirm={showConfirm} />
    </div>
  )
}
