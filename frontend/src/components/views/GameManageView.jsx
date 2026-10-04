import React from 'react'
import { useTranslation } from 'react-i18next'
import PageHead from '../ui/PageHead' /* [CN-FORK] v0.5.2.94 页头统一 + 粘顶 */
import GameSection from './GameSection' /* [CN-FORK] 游戏管理板块 */

/* [CN-FORK] 「游戏管理」独立页：板块自「常用入口」页抽出，侧栏「常用配置」下方入口。
 * 容器与布局对齐 CommonEntriesView（max-w-5xl 居中 + 同款卡片）。
 * v0.5.2.94：标题/说明从板块内部上移到页面级 <PageHead>（粘顶），板块只保留自己的
 * 小标题与刷新，同一屏不再出现两个 24px 级标题。 */

export default function GameManageView() {
  const { t } = useTranslation()
  return (
    <div className="max-w-5xl mx-auto pb-12">
      <PageHead
        bleed={false}
        title={t('game.title', '游戏管理')}
        desc={t('game.desc', '读取已安装（app.db）与已存在（扫描挂载路径）的游戏；导出存档/截图、改名、多标签后续开放。')}
      />
      <div className="mt-6">
        <GameSection />
      </div>
    </div>
  )
}
