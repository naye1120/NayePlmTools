/* ============================================================================
 * [CN-FORK] ThemeToggle.jsx
 * 外观切换三段控件：跟随系统 / 浅色 / 深色。
 * 复用于：侧边栏底部（compact 图标模式）与 设置页（完整模式）。
 * ==========================================================================*/
import React from 'react'
import { Monitor, Moon, Sun } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../utils/helpers'
import { useTheme } from './useTheme'

const OPTIONS = [
  { value: 'auto', icon: Monitor, labelKey: 'theme.auto', labelDefault: 'Auto' },
  { value: 'light', icon: Sun, labelKey: 'theme.light', labelDefault: 'Light' },
  { value: 'dark', icon: Moon, labelKey: 'theme.dark', labelDefault: 'Dark' },
]

const ThemeToggle = ({ compact = false }) => {
  const { t } = useTranslation()
  const [pref, setPref] = useTheme()

  return (
    <div
      role="radiogroup"
      aria-label={t('settings.theme_title', 'Appearance')}
      className={cn(
        'flex rounded-xl bg-white/5 border border-white/10 p-1 gap-1',
        compact ? 'w-full justify-center' : 'w-full justify-start'
      )}
    >
      {OPTIONS.map(({ value, icon: Icon, labelKey, labelDefault }) => {
        const active = pref === value
        const title = t(labelKey, labelDefault)
        return (
          <button
            key={value}
            type="button"
            role="radio"
            aria-checked={active}
            title={title}
            onClick={() => setPref(value)}
            className={cn(
              'flex items-center justify-center rounded-lg transition-all',
              compact ? 'p-2.5 flex-1' : 'px-4 py-2 flex-1 space-x-2',
              active
                ? 'bg-ps-blue text-white shadow-[0_0_12px_rgba(0,112,209,0.3)]'
                : 'text-zinc-500 hover:text-white hover:bg-white/10'
            )}
          >
            <Icon className='w-4 h-4 shrink-0' />
            {!compact && (
              <span className='font-bold text-sm tracking-tight whitespace-nowrap'>{title}</span>
            )}
          </button>
        )
      })}
    </div>
  )
}

export default ThemeToggle
