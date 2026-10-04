/** @type {import('tailwindcss').Config} */

// [CN-FORK] 全局尺度：正文统一放大到参考量级（base≈原 text-xl），控件之间间距（gap/space/margin）收紧到 0.6 倍。
// [CN-FORK] v0.5.2.85 padding 的 4 档同步收紧到 0.5rem（含 px-4/py-4），与控件间隙的紧凑度对齐。
// [CN-FORK] v0.5.2.86 色板转「墨蓝通透」：夜底压到近黑（#04060B）、背景去灰味，蓝味只留在线条与选中底，
//   动作色仍是亮橙（#FF7A2E），高饱和彩色专门留给图标与状态/警示标签（薄荷绿/暖黄/鲜红）。
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

// [CN-FORK] v0.5.2.86 色板转「墨蓝通透」：夜底接近纯黑、蓝味只留在线条与选中底，彩色集中在语义状态色
// [CN-FORK] 藏蓝灰阶：替换 zinc。深底文字冷白偏蓝（#EEF2FB），次要文字中性冷灰（#7F8AA8），不发暖不发闷灰
const navyInk = {
  50: '#F5F7FC', 100: '#EEF2FB', 200: '#DCE3F2', 300: '#AEB9D2', 400: '#7F8AA8',
  500: '#5F6883', 600: '#414B66', 700: '#2B3550', 800: '#1E2740', 900: '#0E1424', 950: '#04060B',
}

// [CN-FORK] 亮橙（动作主色）：实心 bg + 白字，是页面唯一的「亮点」；700 档 #B8410A 供浅底文字用
const vividOrange = {
  50: '#FFF0E5', 100: '#FFDCC4', 200: '#FFC098', 300: '#FFA870', 400: '#FF9152',
  500: '#FF7A2E', 600: '#F25C05', 700: '#B8410A', 800: '#8A3208', 900: '#5C2206', 950: '#2A1104',
}

// [CN-FORK] 薄荷绿（成功态）：替换 emerald，比青柠更鲜更荧光，深底上极跳
const lime = {
  50: '#E2F7EF', 100: '#C4F0E1', 200: '#8FE8C6', 300: '#5FEFC0', 400: '#40EAB4',
  500: '#2EE6A8', 600: '#1FC893', 700: '#12996B', 800: '#0A7150', 900: '#0B4A38', 950: '#0B2A22',
}

// [CN-FORK] 暖黄（警示态）：替换 yellow
const warmYellow = {
  50: '#FDF2D9', 100: '#FCEBB5', 200: '#FAE08C', 300: '#FFE375', 400: '#FFDD66',
  500: '#FFD84D', 600: '#E0AE27', 700: '#BD7F0F', 800: '#8A5C07', 900: '#5C3F06', 950: '#2A2410',
}

// [CN-FORK] 鲜红（危险态）：替换 red。与动作色橙明显区隔，删除/停止类操作一眼可辨
const coral = {
  50: '#FFF0F0', 100: '#FFDDDD', 200: '#FFB8B8', 300: '#FF9494', 400: '#FF7B7B',
  500: '#FF6B6B', 600: '#F04E4E', 700: '#D92D2D', 800: '#A91F22', 900: '#7A1A1C', 950: '#2A1416',
}

export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        // [CN-FORK] 类名保留、值已换：ps-blue 现在是亮橙（动作色），ps-black/ps-surface 是墨蓝深夜底
        'ps-blue': '#ff7a2e',
        'ps-blue-glow': 'rgba(255, 122, 46, 0.4)',
        'ps-black': '#04060b',
        'ps-surface': '#080c16',
        'ps-card': '#0e1424',
        'ps-border': '#1e2740',
        // [CN-FORK] 语义文字色：走 CSS 变量随主题翻转（index.css 深色默认 / light-theme.css 浅色覆盖）
        'ps-text': 'var(--color-ps-text)',
        // [CN-FORK] 结构墨色：侧栏选中底、页头分隔、次级强调这类「非动作」的结构性用色。
        //   v0.5.2.86 按用户指令把夜底与卡片整体下沉，蓝味收敛到线条与选中底（bg-ink-800 #141D33 / 描边 ink-600 #253A6B）
        ink: {
          50: '#F5F7FC', 100: '#E6ECFA', 200: '#C3D0EC', 300: '#8FA2CE', 400: '#5C74AE',
          500: '#3A5490', 600: '#253A6B', 700: '#1B2B54', 800: '#141D33', 900: '#0E1424', 950: '#04060B',
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
