/* [CN-FORK] v0.5.2.166 「音频文件 → PS5 背景音乐（sce_sys/snd0.at9）」的唯一入口
 *
 * 为什么这件事必须在浏览器里做（用户原话：「背景音乐改动没有生效，应该转换成 ac9 格式」）：
 *
 *   PS5 主屏磁贴只认 **ATRAC9**（`sce_sys/snd0.at9`，RIFF/WAVE 容器里一条 ATRAC9 流）。
 *   旧版把用户选的 mp3/ogg/wav 原文件落到面板自己的目录就报「已应用」—— 主机压根没看过
 *   这个文件，主屏自然一声不响。而**主机上没有任何 ATRAC9 编码器**：
 *     · Sony 的 `at9tool.exe` 是 Windows 私有二进制，PS5 上跑不了；
 *     · ffmpeg 只有 `atrac9dec`（解码），没有编码器；
 *     · 唯一可用的开源实现是纯 Python 的 `blackbearreloaded/ps5-at9-converter`
 *       （GPL-3.0-or-later，与本项目同许可），PS5 上既没有 Python 也没有 numpy。
 *   所以编码只能放在**打开面板的那台设备**上（手机 / 电脑，通常也就是 PS5 自己的浏览器）。
 *   移植版见 `./atrac9/`（零依赖 ESM，2700 行，已与 Python 参考实现逐字节比对通过）。
 *
 * 这一层负责「文件 → 能直接 POST 给主机的 .at9 字节」，与网络无关：
 *   ① `decodeAudioToPcm`  —— 用浏览器自带的解码器把 mp3/ogg/wav 解成 48kHz 附近的 Float32 PCM；
 *   ② `encodeFileToAt9`   —— 裁到前 N 秒 → 丢进 Worker 编码（带进度）→ 回 `.at9` 字节 + 响度。
 *
 * 为什么走 Worker：15 秒立体声在桌面上要 3 秒左右，PS5 的 CPU 上更久。放主线程上跑，
 * 整个面板会在这几秒里冻住（连进度条都画不出来），用户只会以为面板死了。
 *
 * 为什么 `?worker&inline`：产物是**单文件 HTML 塞进 ELF**（vite-plugin-singlefile），
 * 主机上只服务这一个 HTML。普通 Worker 会打成独立 chunk，真机上必然 404 ——
 * inline 版把它编成 base64 内联，用 Blob URL 起线程，单文件下也能跑。
 */

import { encodeAt9, isAt9, PS5_SIZE_LIMIT } from './atrac9/index.js'
import At9Worker from './atrac9/worker.js?worker&inline'

export { isAt9, PS5_SIZE_LIMIT }

/* 编码参数。三个都不是随手取的：
 *
 *   · 192 kbps —— 参考实现支持的最高档（72/96/120/144/168/192）。磁贴音乐是「短循环」，
 *     15 秒 192k 只有约 350 KB，远在 2 MiB 上限内，没有降码率的理由。
 *   · 15 秒 —— 参考实现的默认截取长度，也是 PS5 磁贴音乐的实际形态（聚焦时循环播放）。
 *     按 2 MiB 上限，192k 最长能放 87 秒，但那要编码 6 倍的数据量；
 *     而这段音乐是在**用户点下按钮之后**现场编的，等待时间才是这里的瓶颈。
 *   · 'fast' 搜索模式 —— 每个候选隔一个取一个（输出略大、质量略低）。
 *     这是本模块唯一偏离参考实现默认值的地方，理由同上：这是给「磁贴聚焦音」用的，
 *     听不出那点差别，但编码时间能省掉一半以上。
 */
export const AT9_BITRATE_KBPS = 192
export const AT9_MAX_SECONDS = 15
export const AT9_SEARCH_MODE = 'fast'

/* 用户原始音频的体积上限（与后端 /game_music 的口径一致）。 */
export const MUSIC_SOURCE_LIMIT_MB = 30

/* 能直接喂给编码器的原始音频扩展名。.at9 单独一条快路径（见 isAt9File）。 */
export const MUSIC_EXTS = /\.(mp3|ogg|wav)$/i
export const MUSIC_MIME = /^audio\/(mpeg|mp3|ogg|wav|x-wav)$/

/* 一份「已经是 ATRAC9」的文件：按扩展名或按内容认。
 * 两条都认是刻意的 —— 用户从 ps5-at9-converter 那边拿到的文件可能叫 `snd0.at9`
 * 也可能被浏览器标成 `application/octet-stream`，只看 mime 会把它当成未知格式。 */
export function isAt9File(file) {
  if (!file) return false
  if (/\.at9$/i.test(file.name || '')) return true
  return false
}

export function isAt9Bytes(u8) {
  try { return isAt9(u8) } catch { return false }
}

/* 音频文件 → Float32 PCM（每个声道一条）。
 *
 * `decodeAudioData` 在 Safari 上是**回调式**（返回 undefined），在别的浏览器上返回 Promise；
 * 两套都得接 —— 真机跑的就是老 WebKit。用 done 闩保证只 resolve 一次。
 *
 * 解码用**临时 AudioContext**：`decodeAudioData` 不需要音频输出设备，但需要一个 ctx。
 * 用完立刻 close（PS5 上音频通道是稀缺资源，留着不关会占着硬件）。
 * Safari 上 `close()` 可能在还没 resume 的 ctx 上抛错，所以吞掉。 */
export async function decodeAudioToPcm(arrayBuffer) {
  const Ctx = (typeof window !== 'undefined') && (window.AudioContext || window.webkitAudioContext)
  if (!Ctx) throw new Error('这个浏览器不支持解码音频')
  const ctx = new Ctx()
  try {
    const buf = await new Promise((resolve, reject) => {
      let settled = false
      const ok = (b) => { if (!settled) { settled = true; resolve(b) } }
      const bad = (e) => { if (!settled) { settled = true; reject(e instanceof Error ? e : new Error('音频解码失败')) } }
      let ret
      try { ret = ctx.decodeAudioData(arrayBuffer, ok, bad) } catch (e) { bad(e); return }
      if (ret && typeof ret.then === 'function') ret.then(ok, bad)
    })
    const channelData = []
    for (let c = 0; c < buf.numberOfChannels; c++) {
      channelData.push(new Float32Array(buf.getChannelData(c)))
    }
    return { sampleRate: buf.sampleRate, channels: buf.numberOfChannels, channelData }
  } finally {
    try { ctx.close() } catch { /* 老内核偶发抛错，忽略 */ }
  }
}

let seq = 0

/* 在 Worker 里编码。协议见 `atrac9/worker.js`：
 *   入 { id, sampleRate, channels, channelData, bitrateKbps, maxDurationSec, searchMode }
 *   进度 { id, progress }（0..1）
 *   出 { id, ok:true, bytes, info } | { id, ok:false, error }
 *
 * 每次都新建 / 用完 terminate：编码是低频动作（用户设一次音乐），
 * 常驻一个线程在电视端浏览器上不值得，而且残留的 Worker 会把上一次的 PCM 钉在内存里。 */
function encodeInWorker(payload, onProgress) {
  return new Promise((resolve, reject) => {
    /* 「线程起不来」与「编码本身失败」是两回事，必须分得开：
     *   前者可以退回主线程再试一次（会卡界面但能成）；
     *   后者退回主线程只会用同样的输入再失败一次，而且白卡十几秒才报错。
     * 所以线程侧的失败打一个 workerUnavailable 标记，调用方只对带这个标记的降级。 */
    const unavailable = (why) => {
      const e = new Error(why || '编码线程不可用')
      e.workerUnavailable = true
      return e
    }
    let w
    try {
      w = new At9Worker()
    } catch {
      reject(unavailable('编码线程无法创建'))
      return
    }
    const id = ++seq
    let settled = false
    const finish = (fn, arg) => {
      if (settled) return
      settled = true
      try { w.removeEventListener('message', onMessage) } catch { /* 忽略 */ }
      try { w.terminate() } catch { /* 忽略 */ }
      fn(arg)
    }
    const onMessage = (ev) => {
      const m = ev.data || {}
      if (m.id !== id) return
      if (typeof m.progress === 'number' && m.ok === undefined) {
        try { onProgress?.(m.progress) } catch { /* 回调自己的问题 */ }
        return
      }
      if (m.ok) finish(resolve, { bytes: new Uint8Array(m.bytes), info: m.info || {} })
      else finish(reject, new Error(m.error || '编码失败'))
    }
    w.addEventListener('message', onMessage)
    w.addEventListener('error', () => finish(reject, unavailable('编码线程启动失败')))
    w.postMessage({ id, ...payload })
  })
}

/* 主线程兜底：Worker 起不来时（极少数老内核 / 内容安全策略禁 blob:）直接同步编码。
 * 会卡住界面若干秒 —— 但「卡一下然后成功」远好于「点了没反应」。 */
function encodeOnMainThread(payload, onProgress) {
  const r = encodeAt9({
    sampleRate: payload.sampleRate,
    channels: payload.channels,
    channelData: payload.channelData,
    bitrateKbps: payload.bitrateKbps,
    maxDurationSec: payload.maxDurationSec,
    searchMode: payload.searchMode,
    onProgress: (done, total) => { try { onProgress?.(total ? done / total : 0) } catch { /* 忽略 */ } },
  })
  return { bytes: r.bytes, info: { ...r } }
}

/* 文件 → .at9。
 *
 * 返回值：{ bytes:Uint8Array, info:{ durationSec, lufs, byteCount, ... } }
 *   info.lufs 会被后端写进 `sce_sys/param.json` 的 `pubtools.loudnessSnd0` ——
 *   这是 ATRAC9 响度声明的**唯一**来源，所以必须把编码器算出来的实测值带出去，
 *   不能在前端另填一个「大概的」数字。
 *
 * `onProgress(0..1, phase)` 的 phase 是 'decode' | 'encode'：
 *   解码阶段没有细粒度进度（浏览器的解码器不报），只报一个 0 与 1 的壳，
 *   界面据此显示「正在解码…」而不是一个不动的 0%。
 *
 * ⚠ 先**裁到前 N 秒再送进 Worker**：一首 4 分钟的立体声歌解出来是 80 MB 级的 Float32，
 * 整个结构化克隆过去又慢又占内存，而编码器只用前 15 秒。slice 出来的是新 buffer
 * （subarray 只给视图，克隆时照样会带上整个底层 buffer）。 */
export async function encodeFileToAt9(file, { onProgress, bitrateKbps = AT9_BITRATE_KBPS, maxDurationSec = AT9_MAX_SECONDS, searchMode = AT9_SEARCH_MODE } = {}) {
  onProgress?.(0, 'decode')
  const raw = await file.arrayBuffer()
  const pcm = await decodeAudioToPcm(raw)
  onProgress?.(1, 'decode')

  const cap = Math.max(1, Math.round(maxDurationSec * pcm.sampleRate))
  const channelData = pcm.channelData.map((c) => (c.length > cap ? c.slice(0, cap) : c))

  const payload = {
    sampleRate: pcm.sampleRate,
    channels: channelData.length,
    channelData,
    bitrateKbps,
    maxDurationSec,
    searchMode,
  }

  onProgress?.(0, 'encode')
  let out
  try {
    out = await encodeInWorker(payload, (p) => onProgress?.(p, 'encode'))
  } catch (err) {
    /* 只对「线程起不来」降级；编码本身报错就原样抛出去（退回主线程只会白卡十几秒
     * 再用同样的输入失败一次）。 */
    if (!err || !err.workerUnavailable) throw err
    out = encodeOnMainThread(payload, (p) => onProgress?.(p, 'encode'))
  }
  onProgress?.(1, 'encode')

  const bytes = out.bytes
  if (!bytes || bytes.length === 0) throw new Error('编码结果为空')
  if (bytes.length > PS5_SIZE_LIMIT) {
    throw new Error(`编码结果 ${Math.round(bytes.length / 1024)} KB 超过主机上限 ${Math.round(PS5_SIZE_LIMIT / 1024)} KB`)
  }
  return { bytes, info: out.info || {} }
}

/* 把秒数格式化成播放器上的 mm:ss。放这里而不是组件里，是因为
 * 音乐块的播放器与上传进度两处都要用同一个口径。 */
export function fmtClock(sec) {
  const s = Number(sec)
  if (!Number.isFinite(s) || s < 0) return '0:00'
  const m = Math.floor(s / 60)
  const r = Math.floor(s % 60)
  return `${m}:${r < 10 ? '0' : ''}${r}`
}
