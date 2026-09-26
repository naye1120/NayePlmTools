/* ============================================================================
 * [CN-FORK] useTheme.js
 * 主题偏好管理：深色 / 浅色 / 跟随系统。
 *
 * - 偏好存于 localStorage('pldmgrTheme')，'auto' 表示跟随 prefers-color-scheme
 * - 实际生效值以 <html data-theme="dark|light"> 暴露给 light-theme.css
 * - 同时负责把 <html lang> 同步为当前 i18n 语言，供 cn-typography.css 判定中文排版
 * - 模块导入时立即应用一次，避免首屏闪烁 (FOUC)
 * ==========================================================================*/
import { useCallback, useEffect, useState } from 'react'

const STORAGE_KEY = 'pldmgrTheme'

function readPref() {
  try {
    return localStorage.getItem(STORAGE_KEY) || 'auto'
  } catch {
    return 'auto'
  }
}

function systemPrefersLight() {
  try {
    return window.matchMedia('(prefers-color-scheme: light)').matches
  } catch {
    // PS5 老内核浏览器不支持 matchMedia：默认深色
    return false
  }
}

export function resolveTheme(pref) {
  if (pref === 'light' || pref === 'dark') return pref
  return systemPrefersLight() ? 'light' : 'dark'
}

export function applyTheme(pref) {
  const effective = resolveTheme(pref)
  const el = document.documentElement
  el.setAttribute('data-theme', effective)
  el.style.colorScheme = effective
}

// 导入即应用，消除首屏闪烁
applyTheme(readPref())

export function useTheme() {
  const [pref, setPrefState] = useState(readPref)

  const setPref = useCallback((next) => {
    setPrefState(next)
    try {
      if (next === 'auto') localStorage.removeItem(STORAGE_KEY)
      else localStorage.setItem(STORAGE_KEY, next)
    } catch { /* 隐私模式下仅内存生效 */ }
    applyTheme(next)
  }, [])

  // 'auto' 时监听系统主题切换
  useEffect(() => {
    if (pref !== 'auto') return undefined
    let mq
    const handler = () => applyTheme('auto')
    try {
      mq = window.matchMedia('(prefers-color-scheme: light)')
      if (mq.addEventListener) mq.addEventListener('change', handler)
      else if (mq.addListener) mq.addListener(handler)
    } catch { return undefined }
    return () => {
      if (mq.addEventListener) mq.removeEventListener('change', handler)
      else if (mq.removeListener) mq.removeListener(handler)
    }
  }, [pref])

  return [pref, setPref]
}

/** 把 i18n 当前语言同步到 <html lang>，供中文排版层生效 */
export function syncHtmlLang(language) {
  if (language) document.documentElement.setAttribute('lang', language)
}
