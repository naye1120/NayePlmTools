import React from 'react'
import GameSection from './GameSection' /* [CN-FORK] 游戏管理板块 */

/* [CN-FORK] 「游戏管理」独立页：板块自「常用入口」页抽出，侧栏「常用配置」下方入口。
 * 容器与布局对齐 CommonEntriesView（max-w-5xl 居中 + 同款卡片）。 */

export default function GameManageView() {
  return (
    <div className="max-w-5xl mx-auto space-y-8 pb-12">
      <GameSection />
    </div>
  )
}
