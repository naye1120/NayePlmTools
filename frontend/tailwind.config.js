/** @type {import('tailwindcss').Config} */

// [CN-FORK] 全局尺度：正文统一放大到参考量级（base≈原 text-xl），控件之间间距（gap/space/margin）收紧到 0.6 倍。
// [CN-FORK] v0.5.2.85 padding 的 4 档同步收紧到 0.5rem（含 px-4/py-4），与控件间隙的紧凑度对齐。
// [CN-FORK] v0.5.2.86 色板转「墨蓝通透」：夜底压到近黑（#04060B）、背景去灰味，蓝味只留在线条与选中底，
//   动作色仍是亮橙（深色档 #FF9410，v0.5.2.99 由 #FF7A2E 转档：色相 21.8°→33.2°，蓝通道 46→16，
//   用户指实心橙块在深底上发棕），高饱和彩色专门留给图标与状态/警示标签（薄荷绿/暖黄/鲜红）。
// [CN-FORK] v0.5.2.87 「一档一色」：每套主题只留 4 档中性底（画布/侧栏/卡片/内嵌件）+ 1 档选中底，
//   顺带把此前漏掉、一直吃 Tailwind 默认色的 amber/green 两个色族并入同源，全站只剩一套鲜色。
//   具体收口手法见 src/index.css 末尾的「一档一色」收口层（全部走 CSS 变量，浅色档在 light-theme.css 翻变量）。
// [CN-FORK] v0.5.2.100 **整套换成参考页 ps5-remoteplay-pin 的「灰墨 + 蓝主色」**（用户选定「整套照搬（含蓝主色）」）：
//   中性底换成灰墨阶（画布 #111317 / 侧栏与卡片 #1B1F25 / 内嵌件 #0D1117 / 发丝线 #30363D），
//   动作主色由橙换成蓝 #2563EB，状态色换成参考页的绿 #3FB950 / 金 #D29922 / 红 #F87171。
//   本轮明确解除旧硬约束「主色（动作色）不得为灰或蓝」——这是用户逐条圈定的方向，不是自作主张。
//   4+1 档契约、角色语义（800=线 / 900=卡片 / 950=画布）与浅色翻转机制都不动。
// 关键手法：**只换 token 值、不改任何类名**——上游与 fork 各处写的 bg-ps-blue / text-zinc-400 等类名原样保留，
// 换色因此是全站一次性生效且零侵入（上游后续沿用同类名即继续自动跟随）。ps-* 的类名是历史包袱，
// 现指向蓝/灰墨，勿按字面理解成「PS 蓝 / 黑」。
const compact = {
  0: '0px', px: '1px',
  0.5: '0.075rem', 1: '0.15rem', 1.5: '0.225rem', 2: '0.3rem', 2.5: '0.375rem',
  3: '0.45rem', 3.5: '0.525rem', 4: '0.6rem', 5: '0.75rem', 6: '0.9rem',
  7: '1.05rem', 8: '1.2rem', 9: '1.35rem', 10: '1.5rem', 11: '1.65rem',
  12: '1.8rem', 14: '2.1rem', 16: '2.4rem', 20: '3rem', 24: '3.6rem',
  28: '4.2rem', 32: '4.8rem',
}

// [CN-FORK] 灰墨阶：替换 zinc。100~300 是深底上的冷白文字，400~600 是三级灰阶文字，
//   800/900/950 三档被复用为中性底：800=发丝线、900=卡片底、950=页面底，确保「同一个色名只有一个来源」
// [CN-FORK] v0.5.2.100 整档换成参考页的灰墨（原藏蓝味 #EEF2FB/#96A1BD 系）
const navyInk = {
  50: '#F6F8FA', 100: '#E6EDF3', 200: '#D1D9E0', 300: '#C9D1D9', 400: '#A1A9B3',
  500: '#8B949E', 600: '#6E7681', 700: '#484F58', 800: '#30363D', 900: '#1B1F25', 950: '#111317',
}

// [CN-FORK] 蓝（动作主色）：实心 bg + 白字，是页面唯一的「亮点」；700 档供浅底文字用
// [CN-FORK] v0.5.2.100 由亮橙整套换成参考页的动作蓝 #2563EB（标识符仍叫 vividOrange，别按字面理解）
const vividOrange = {
  50: '#EEF4FF', 100: '#DDEAFF', 200: '#BFD7FE', 300: '#8ABCF9', 400: '#5B9BFF',
  500: '#2563EB', 600: '#1F6FEB', 700: '#1450B3', 800: '#103D8A', 900: '#0B2A5C', 950: '#06182F',
}

// [CN-FORK] 绿（成功态）：替换 emerald，取参考页的 #3FB950 一档
const lime = {
  50: '#E7F9EE', 100: '#CFF3DC', 200: '#A6E8BE', 300: '#6FD79A', 400: '#4CC782',
  500: '#3FB950', 600: '#2FA242', 700: '#238636', 800: '#186428', 900: '#0F4519', 950: '#0A2C10',
}

// [CN-FORK] 金（警示态）：替换 yellow，取参考页的 #D29922 一档
const warmYellow = {
  50: '#FCF1D8', 100: '#F8E3AE', 200: '#F3CE79', 300: '#F0B429', 400: '#E3B341',
  500: '#D29922', 600: '#B08319', 700: '#8A6512', 800: '#5C440C', 900: '#3D2C08', 950: '#241A05',
}

// [CN-FORK] 红（危险态）：替换 red。与动作蓝明显区隔，删除/停止类操作一眼可辨
// [CN-FORK] v0.5.2.100 取参考页的 #F87171 / #F85149
const coral = {
  50: '#FFECEC', 100: '#FFD7D7', 200: '#FFB3B3', 300: '#FF9494', 400: '#F87171',
  500: '#F85149', 600: '#DA3633', 700: '#B62324', 800: '#8B1F20', 900: '#5C1516', 950: '#330C0D',
}

export default {
  content: [
    "./index.html",
    "./src/**/*.{js,ts,jsx,tsx}",
  ],
  theme: {
    extend: {
      colors: {
        // [CN-FORK] 类名保留、值已换：ps-blue 现在是动作蓝（v0.5.2.100），ps-black/ps-surface 是灰墨底
        // [CN-FORK] v0.5.2.87 中性底四档定名（类名 = 角色名，别再当第 5 种底色用）：
        //   ps-black=画布 / ps-surface=侧栏 / ps-card=卡片 / ps-border=发丝线；
        //   第 4 档「内嵌件」（图标块、输入框、行、开关槽）用 --color-inset，见 index.css 收口层
        'ps-blue': '#2563eb',
        'ps-blue-glow': 'rgba(37, 99, 235, 0.4)',
        'ps-black': '#111317',
        'ps-surface': '#1b1f25',
        'ps-card': '#1b1f25',
        'ps-border': '#30363d',
        // [CN-FORK] 语义文字色：走 CSS 变量随主题翻转（index.css 深色默认 / light-theme.css 浅色覆盖）
        'ps-text': 'var(--color-ps-text)',
        // [CN-FORK] 语义别名，新组件优先用这几个名字写，别再写 ps-blue 字面
        primary: vividOrange,
        success: lime,
        warning: warmYellow,
        danger: coral,
        // [CN-FORK] 整阶替换：zinc 系灰墨、red 系红、emerald 系绿、yellow 系金（v0.5.2.100 换档）。
        //   v0.5.2.87 补 amber/green 两个漏网色族（此前一直吃 Tailwind 默认色，是「颜色杂」的残留来源）
        zinc: navyInk,
        red: coral,
        emerald: lime,
        green: lime,
        yellow: warmYellow,
        amber: warmYellow,
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
