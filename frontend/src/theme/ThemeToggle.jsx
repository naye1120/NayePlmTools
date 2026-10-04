/* ============================================================================
 * [CN-FORK] ThemeToggle.jsx
 * 外观切换三段控件：跟随系统 / 浅色 / 深色。
 * 复用于：侧边栏底部（compact 图标模式）与 设置页（完整模式）。
 * ==========================================================================*/
import React from 'react'
import { Monitor, Moon, Sun } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import Segmented from '../components/ui/Segmented' /* [CN-FORK] v0.5.2.97 互斥三段改走共用分段控件 */
import { useTheme } from './useTheme'

const OPTIONS = [
  { value: 'auto', icon: Monitor, labelKey: 'theme.auto', labelDefault: 'Auto' },
  { value: 'light', icon: Sun, labelKey: 'theme.light', labelDefault: 'Light' },
  { value: 'dark', icon: Moon, labelKey: 'theme.dark', labelDefault: 'Dark' },
]

const ThemeToggle = ({ compact = false }) => {
  const { t } = useTranslation()
  const [pref, setPref] = useTheme()

  /* [CN-FORK] v0.5.2.97 这三段互斥选项此前自带一套轨道 + 一套「实心橙 + 白字 + 光晕」的选中表达，
   * 与管理页排序、温控档位、自启延时三处各不相同；现统一 <Segmented>（选中档全站只有 .sel-chip 一档）。
   * compact（侧栏收窄态）只留图标，整名转给 title 与读屏。 */
  return (
    <Segmented
      className="w-full"
      label={t('settings.theme_title', 'Appearance')}
      value={pref}
      onChange={setPref}
      itemClassName={compact ? 'px-2' : undefined}
      options={OPTIONS.map(({ value, icon, labelKey, labelDefault }) => ({
        value,
        icon,
        title: t(labelKey, labelDefault),
        label: compact ? null : t(labelKey, labelDefault),
      }))}
    />
  )
}

export default ThemeToggle
