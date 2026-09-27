/** @type {import('tailwindcss').Config} */

// [CN-FORK] 全局尺度：正文统一放大到参考量级（base≈原 text-xl），控件之间间距（gap/space/margin）收紧到 0.6 倍。
// 只覆盖 gap/space/margin，不动 padding 与 width/height，故控件内部留白、图标尺寸保持不变。
const compact = {
  0: '0px', px: '1px',
  0.5: '0.075rem', 1: '0.15rem', 1.5: '0.225rem', 2: '0.3rem', 2.5: '0.375rem',
  3: '0.45rem', 3.5: '0.525rem', 4: '0.6rem', 5: '0.75rem', 6: '0.9rem',
  7: '1.05rem', 8: '1.2rem', 9: '1.35rem', 10: '1.5rem', 11: '1.65rem',
  12: '1.8rem', 14: '2.1rem', 16: '2.4rem', 20: '3rem', 24: '3.6rem',
  28: '4.2rem', 32: '4.8rem',
}

export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        'ps-blue': '#0095ff',
        'ps-blue-glow': 'rgba(0, 149, 255, 0.4)',
        'ps-black': '#08080a',
        'ps-surface': '#101014',
        'ps-card': 'rgba(24, 24, 28, 0.8)',
        'ps-border': 'rgba(255, 255, 255, 0.08)',
        // [CN-FORK] 语义文字色：走 CSS 变量随主题翻转（index.css 深色默认 / light-theme.css 浅色覆盖）
        'ps-text': 'var(--color-ps-text)',
      },
      borderRadius: {
        'ps-xl': '1rem',
        'ps-2xl': '1.5rem',
        'ps-3xl': '2rem',
      },
      fontFamily: {
        'ps5': ["Inter", "system-ui", "sans-serif"],
      },
      fontSize: {
        xs: ['0.75rem', { lineHeight: '1.0rem' }],
        sm: ['0.85rem', { lineHeight: '1.15rem' }],
        base: ['0.9rem', { lineHeight: '1.25rem' }],
        lg: ['1.05rem', { lineHeight: '1.4rem' }],
        xl: ['1.1rem', { lineHeight: '1.45rem' }],
        '2xl': ['1.35rem', { lineHeight: '1.7rem' }],
        '3xl': ['1.7rem', { lineHeight: '2.0rem' }],
        '4xl': ['2.05rem', { lineHeight: '2.35rem' }],
        '5xl': ['2.7rem', { lineHeight: '1' }],
        '6xl': ['3.4rem', { lineHeight: '1' }],
        '7xl': ['4.1rem', { lineHeight: '1' }],
        '8xl': ['5.4rem', { lineHeight: '1' }],
        '9xl': ['7.2rem', { lineHeight: '1' }],
      },
      gap: compact,
      space: compact,
      margin: compact,
    },
  },
  plugins: [],
}
