/** @type {import('tailwindcss').Config} */

// [CN-FORK] 全局尺度：正文统一放大到参考量级（base≈原 text-xl），控件之间间距（gap/space/margin）收紧到 0.6 倍。
// [CN-FORK] v0.5.2.85 padding 的 4 档同步收紧到 0.5rem（含 px-4/py-4），与控件间隙的紧凑度对齐。
// [CN-FORK] v0.5.2.85 色板转「藏蓝结构 + 亮橙动作」撞色：主色（动作色）由 PS 蓝改亮橙，
//   中性 zinc 换藏蓝灰阶（承担正文/次要文字这类结构墨色），语义色改青柠/暖黄/珊瑚。
// 关键手法：**只换 token 值、不改任何类名**——上游与 fork 各处写的 bg-ps-blue / text-zinc-400 等类名原样保留，
// 换色因此是全站一次性生效且零侵入（上游后续沿用同类名即继续自动跟随）。ps-* 的类名是历史包袱，
// 现指向橙/藏蓝，勿按字面理解成「PS 蓝 / 黑」。
const compact = {
  0: '0px', px: '1px',
  0.5: '0.075rem', 1: '0.15rem', 1.5: '0.225rem', 2: '0.3rem', 2.5: '0.375rem',
  3: '0.45rem', 3.5: '0.525rem', 4: '0.6rem', 5: '0.75rem', 6: '0.9rem',
  7: '1.05rem', 8: '1.2rem', 9: '1.35rem', 10: '1.5rem', 11: '1.65rem',
  12: '1.8rem', 14: '2.1rem', 16: '2.4rem', 20: '3rem', 24: '3.6rem',
  28: '4.2rem', 32: '4.8rem',
}

// [CN-FORK] 藏蓝灰阶：替换 zinc。深色底上的次要文字带极轻蓝调（#949DB5），不发灰不发暖
const navyInk = {
  50: '#F4F6FB', 100: '#E8ECFF', 200: '#CFD7EE', 300: '#B6BED6', 400: '#949DB5',
  500: '#767F9A', 600: '#5A6288', 700: '#434B6B', 800: '#2C3350', 900: '#1B2033', 950: '#0C0F1A',
}

// [CN-FORK] 亮橙（动作主色）：实心 bg + 白字，是页面唯一的「亮点」；700 档 #B84D00 供浅底文字用
const vividOrange = {
  50: '#FFEADB', 100: '#FFD6B8', 200: '#FFBB85', 300: '#FF9D55', 400: '#FF802B',
  500: '#FF6600', 600: '#E05900', 700: '#B84D00', 800: '#8C3704', 900: '#5C2505', 950: '#331402',
}

// [CN-FORK] 青柠（成功态）：替换 emerald，比原绿更亮更跳
const lime = {
  50: '#F0F8DD', 100: '#E2F2C0', 200: '#CBE894', 300: '#B3DD66', 400: '#AAD944',
  500: '#99CC33', 600: '#7BAB26', 700: '#5D8A1F', 800: '#476A18', 900: '#33500F', 950: '#1C2D08',
}

// [CN-FORK] 暖黄（警示态）：替换 yellow
const warmYellow = {
  50: '#FFF9CC', 100: '#FFF3A6', 200: '#FFE973', 300: '#FFDD3D', 400: '#FFD633',
  500: '#FFCC00', 600: '#D9AD00', 700: '#B8860B', 800: '#8A6508', 900: '#5C4405', 950: '#332603',
}

// [CN-FORK] 珊瑚（危险态）：替换 red，与主色橙同暖色温但明显偏红，删除类操作仍可辨
const coral = {
  50: '#FFEBEB', 100: '#FFD6D6', 200: '#FFAAAA', 300: '#FF8080', 400: '#FF6B6B',
  500: '#FF5555', 600: '#E03C3C', 700: '#B62E2E', 800: '#8A2424', 900: '#5C1A1A', 950: '#2E0D0D',
}

export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        // [CN-FORK] 类名保留、值已换：ps-blue 现在是亮橙（动作色），ps-black/ps-surface 是藏蓝深夜底
        'ps-blue': '#ff6600',
        'ps-blue-glow': 'rgba(255, 102, 0, 0.4)',
        'ps-black': '#0c0f1a',
        'ps-surface': '#11162a',
        'ps-card': '#1b2033',
        'ps-border': '#2c3350',
        // [CN-FORK] 语义文字色：走 CSS 变量随主题翻转（index.css 深色默认 / light-theme.css 浅色覆盖）
        'ps-text': 'var(--color-ps-text)',
        // [CN-FORK] 结构墨色（藏蓝）：侧栏选中底、页头分隔、次级强调这类「非动作」的结构性用色。
        //   按用户指令「深色的时候颜色再深一些」，深色档由样张的 #4A5FA5 压到 #33477F，避免夜底刺眼
        ink: {
          50: '#F4F6FB', 100: '#DDE4F4', 200: '#B9C6E8', 300: '#8FA0D4', 400: '#6B80C0',
          500: '#4F68A8', 600: '#33477F', 700: '#283968', 800: '#1D2644', 900: '#141B33', 950: '#0C0F1A',
        },
        // [CN-FORK] 语义别名，新组件优先用这几个名字写，别再写 ps-blue 字面
        primary: vividOrange,
        success: lime,
        warning: warmYellow,
        danger: coral,
        // [CN-FORK] 整阶替换：zinc 系藏蓝灰、red 系珊瑚、emerald 青柠、yellow 暖黄
        zinc: navyInk,
        red: coral,
        emerald: lime,
        yellow: warmYellow,
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
      // [CN-FORK] v0.5.2.85 用户指令：padding 的 4 档 = 0.5rem（p-4 / px-4 / py-4 一并跟随）
      padding: { 4: '0.5rem' },
    },
  },
  plugins: [],
}
