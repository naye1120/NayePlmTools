/* [CN-FORK] v0.5.2.112 游戏模块的展示格式化
 *
 * 抽出来的原因：这一轮新增的四个弹窗（详情/存档/多媒体/改名）都要显示体积、时间、时长，
 * 而 GameSection 里原本那份 formatSize 是模块内私有的。就地复制四份必然出现「同一个 68.5 GB
 * 在磁贴上写 68.5 GB、在详情里写 68.50 GB」这种不一致，所以收成一处。
 * 磁贴原来的格式化行为一个字没改（阈值、进位、有效位全部照抄），只是换了个位置。
 */

/* 体积：后端一律给字节。0 / 缺失 → 空串（调用方据此决定要不要显示这一格）。
 * 按 1024 递进，≥100 或 B/KB 取整、其余保留小数。 */
export function formatSize(bytes) {
  const n = Number(bytes) || 0
  if (n <= 0) return ''
  const units = ['B', 'KB', 'MB', 'GB', 'TB']
  let v = n
  let i = 0
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024
    i += 1
  }
  const digits = v >= 100 || i <= 1 ? 0 : v >= 10 ? 1 : 2
  return `${v.toFixed(digits)} ${units[i]}`
}

/* 体积（详情页版）：0 也显示成 "0 B" 而不是空串。
 * 磁贴上「体积未知」应该什么都不显示（省地方），详情页里「未知」必须显式写出来 ——
 * 一格里是空的，用户分不清是「0」还是「没读到」。 */
export function formatSizeAlways(bytes) {
  return formatSize(bytes) || '0 B'
}

/* 时长：秒 → 0:32 / 1:05:20。用于视频。 */
export function formatDuration(sec) {
  const s = Math.max(0, Math.round(Number(sec) || 0))
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  const ss = s % 60
  const pad = (x) => String(x).padStart(2, '0')
  return h > 0 ? `${h}:${pad(m)}:${pad(ss)}` : `${m}:${pad(ss)}`
}

/* 时间戳 → 原样透传。
 * 后端（与 ShadowMountPlus 的 games/info 一致）返回的已经是**本地时区的可读串**
 * （`2026-10-04 22:10`），前端不再做一次 Date 解析 —— 主机的时区与浏览器的时区
 * 不一定相同，来回转换只会把时间改错。读不到就返回空串，由调用方显示「—」。 */
export function formatStamp(v) {
  const s = String(v == null ? '' : v).trim()
  return s || ''
}

/* 传输速率：字节/秒 → "12.3 MB/s"。 */
export function formatRate(bytesPerSec) {
  const v = Number(bytesPerSec) || 0
  if (v <= 0) return '—'
  const units = ['B/s', 'KB/s', 'MB/s', 'GB/s']
  let x = v
  let i = 0
  while (x >= 1024 && i < units.length - 1) { x /= 1024; i += 1 }
  return `${x.toFixed(x >= 100 || i === 0 ? 0 : 1)} ${units[i]}`
}

/* 已用时长：秒 → "12 秒" / "1 分 20 秒"。进度面板上用。 */
export function formatElapsed(sec) {
  const s = Math.max(0, Math.round(Number(sec) || 0))
  if (s < 60) return `${s} 秒`
  const m = Math.floor(s / 60)
  const r = s % 60
  return r ? `${m} 分 ${r} 秒` : `${m} 分`
}
