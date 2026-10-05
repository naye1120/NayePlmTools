import React from 'react'
import { useTranslation } from 'react-i18next'
import { Lightbulb, Gamepad2, HardDriveDownload, Tags, Activity, ChevronRight } from 'lucide-react'
import Modal from '../ui/Modal'
import { Note } from './parts'

/* [CN-FORK] v0.5.2.112 第四个功能位（待定）
 *
 * 用户口径：「第四个暂时没想好。」
 *
 * 所以这个位置**不能**做成一个灰按钮或者一个假装能用的按钮 —— 那两种都会让用户以为
 * 是自己没找到用法。这里做成一格明确的「待定」：点开就说清楚「这个位置还空着」，
 * 并列出几个**基于现有代码能实现**的候选方向，让用户挑一个（或者提别的）。
 *
 * 候选不是凭空想的，每条都标注了它需要的后端能力，避免用户选了一个这轮做不了的东西：
 *   · 存档快照历史 —— 复用存档列表 + 多版本目录，后端改动最小
 *   · 游戏时长统计 —— app.db 里有 last_access_time，加上自建的历史表就能出图
 *   · 一键导入 / 导出 —— 复用已有的 zip 打包能力（与多媒体打包同一条路径）
 *   · 启动参数 / 环境 —— 需要 appmeta 或 fakelib 相关接口（ShadowMountPlus 有 fakelib 路由可参考）
 */

const IDEAS = [
  {
    icon: HardDriveDownload,
    title: '存档快照历史',
    desc: '给存档做多版本快照，可以回滚到任意一个时间点。复用存档列表与打包路径，后端改动最小。',
    need: '需要：存档多版本目录',
  },
  {
    icon: Activity,
    title: '游戏时长统计',
    desc: '记录每个游戏玩了多久、最近在玩什么，出一张按周/月的时间线。',
    need: '需要：后台累计时长（app.db 已有最近启动时间可起步）',
  },
  {
    icon: Tags,
    title: '批量整理',
    desc: '一次给多个游戏改名 / 换背景 / 打标签，不用一个个点开。',
    need: '需要：批量改名与背景接口',
  },
  {
    icon: Gamepad2,
    title: '启动参数与兼容设置',
    desc: '按游戏设置 fakelib / 挂载模式 / 兼容开关，下次启动自动生效。',
    need: '需要：appmeta 或 fakelib 接口',
  },
]

export default function ComingSoonModal({ show, game, onClose }) {
  const { t } = useTranslation()
  if (!show) return null

  return (
    <Modal
      show={show}
      title={t('game.pending_title', '第四个功能位')}
      onClose={onClose}
      className="max-w-2xl"
    >
      <div className="panel-body leading-relaxed font-normal space-y-4">
        <p className="text-zinc-500 panel-note">{game ? (game.name || game.tid) : ''}</p>

        <Note tone="info" icon={Lightbulb}>
          {t('game.pending_desc', '这个位置还没定下来做什么。下面几个方向都是基于现有能力能实现的，挑一个（或者你有别的想法直接说）。')}
        </Note>

        <ul className="space-y-2">
          {/* 用 `const Icon = it.icon` 而不是 `({ icon: Icon, ... }) =>`：本仓库 eslint 没装
              eslint-plugin-react，只在 JSX 里用到的解构参数会被 no-unused-vars 误判（见 GameSection.jsx）。 */}
          {IDEAS.map((it) => {
            const Icon = it.icon
            return (
              <li key={it.title} className="flex items-start gap-3 p-3 rounded-xl border border-white/10 bg-white/[0.02]">
                <div className="w-9 h-9 rounded-lg bg-white/5 flex items-center justify-center shrink-0">
                  <Icon className="w-4 h-4 text-ps-blue" />
                </div>
                <div className="min-w-0">
                  <p className="text-white panel-body font-semibold flex items-center gap-1">
                    {it.title}
                    <ChevronRight className="w-3.5 h-3.5 text-zinc-600" />
                  </p>
                  <p className="text-zinc-400 panel-note mt-0.5 leading-snug">{it.desc}</p>
                  <p className="text-zinc-600 panel-tiny mt-1">{it.need}</p>
                </div>
              </li>
            )
          })}
        </ul>
      </div>
    </Modal>
  )
}
