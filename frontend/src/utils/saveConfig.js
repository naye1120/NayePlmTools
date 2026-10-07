/* [CN-FORK] v0.5.2.143 存档配置常量与转换函数
 *
 * 原本只活在 SaveManagerModal 里；设置页新增「存档配置」入口后，
 * 全局默认配置弹窗也要用同一套协议、字段定义、编辑态/后端态转换。
 * 抽到这里，避免两份一样的 MODES / MODE_FIELDS / EMPTY_CFG 互相跑偏。
 */

/* SMB 已解禁（走 libsmb2）；三种协议都可选。 */
export const MODES = [
  { value: 'smb', label: 'SMB' },
  { value: 'ftp', label: 'FTP' },
  { value: 'http', label: 'HTTP' },
]

/* 每种协议各自的字段定义。加协议只改这张表。 */
export const MODE_FIELDS = {
  smb: [
    { key: 'host', ph: '192.168.1.10' },
    { key: 'port', ph: '445', numeric: true },
    { key: 'user', ph: 'ps5' },
    { key: 'password', secret: true },
    { key: 'share', ph: 'ps5save' },
    { key: 'sub', ph: 'saves' },
  ],
  ftp: [
    { key: 'host', ph: '192.168.1.10' },
    { key: 'port', ph: '2121', numeric: true },
    { key: 'path', ph: '/ps5save/PPSA01234' },
    { key: 'user', ph: 'anonymous' },
    { key: 'password', secret: true },
  ],
  http: [
    { key: 'url', ph: 'http://192.168.1.10:8099/save' },
    { key: 'token', secret: true },
  ],
}

/* 字段标签的中性中文兜底；真正翻译走 t(`game.sf_${f.key}`)。 */
export const FIELD_LABEL = {
  host: '主机地址',
  share: '共享名',
  sub: '子路径',
  user: '用户名',
  password: '密码',
  port: '端口',
  path: '远程路径',
  url: '上传地址',
  token: '访问令牌',
}

export const EMPTY_CFG = {
  enabled: false,
  mode: 'ftp',
  on_launch: true,
  on_exit: true,
  smb: { host: '', port: '', share: '', sub: '', user: '', password: '' },
  ftp: { host: '', port: '', path: '', user: '', password: '' },
  http: { url: '', token: '' },
  snap: { enabled: false, interval: 1800, keep: 5 },
}

/* 全局存档配置键名（设置页「存档配置」弹窗写入主配置 ARCHIVE_GLOBAL_CONFIG）。
 * 单游戏弹窗读它作为远程位置的唯一真相源。 */
export const GLOBAL_KEY = 'ARCHIVE_GLOBAL_CONFIG'

/* 把主配置里的 ARCHIVE_GLOBAL_CONFIG 解析成完整 cfg（与 EMPTY_CFG 合并，各协议段各自展开）。
 * 与 ArchiveConfigModal 里那份逻辑一致，抽到这里让两个弹窗共用、不再各写各的。 */
export function loadGlobalCfg(config) {
  let stored = null
  try {
    const raw = config && config[GLOBAL_KEY]
    if (raw) stored = JSON.parse(raw)
  } catch {
    stored = null
  }
  return {
    ...EMPTY_CFG,
    ...(stored || {}),
    smb: { ...EMPTY_CFG.smb, ...(stored && stored.smb) || {} },
    ftp: { ...EMPTY_CFG.ftp, ...(stored && stored.ftp) || {} },
    http: { ...EMPTY_CFG.http, ...(stored && stored.http) || {} },
    snap: { ...EMPTY_CFG.snap, ...(stored && stored.snap) || {} },
  }
}

/* 全局配置保存前的数字规范化（snap.interval / keep 兜底）。 */
export function normalizeCfg(c) {
  return {
    ...c,
    snap: {
      ...c.snap,
      interval: Number(c.snap.interval) || EMPTY_CFG.snap.interval,
      keep: Number(c.snap.keep) || EMPTY_CFG.snap.keep,
    },
  }
}

/* 一份 cfg 的远程段是否真的填了目标（用于判断「全局是否已配置」）。
 * 只查当前 mode 的主目标字段，空着就当没配。 */
export function isRemoteConfigured(cfg) {
  const m = cfg.mode
  if (m === 'smb') return !!(cfg.smb && (cfg.smb.host || '').trim())
  if (m === 'ftp') return !!(cfg.ftp && (cfg.ftp.host || '').trim())
  if (m === 'http') return !!(cfg.http && (cfg.http.url || '').trim())
  return false
}

/* 取出一份 cfg 的远程段（{mode, smb, ftp, http}），缺字段用 EMPTY_CFG 补齐。 */
export function remoteOf(cfg) {
  return {
    mode: cfg.mode || 'ftp',
    smb: { ...EMPTY_CFG.smb, ...(cfg.smb || {}) },
    ftp: { ...EMPTY_CFG.ftp, ...(cfg.ftp || {}) },
    http: { ...EMPTY_CFG.http, ...(cfg.http || {}) },
  }
}

/* 单个游戏保存时该用哪份远程配置：
 *   优先全局（全局已填目标）—— 这就是「全局生效」的字面意义，用户在设置里填一次，
 *   所有游戏保存后都用它；游戏之前各自配过、且全局还没填时，回退到这个游戏自己已存的远程段，
 *   不丢旧配置。后端没回传密码，所以 per-game 回退段的密码是空串 —— toRemoteCfg 会把它当
 *   「不修改」，后端保留自己存的密码。 */
export function resolveRemoteForGame(globalCfg, perGameAuto) {
  if (isRemoteConfigured(globalCfg)) return remoteOf(globalCfg)
  return remoteOf(toLocalCfg(perGameAuto))
}

/* 给远程位置做一行给人看的摘要（只读展示用）。 */
export function remoteSummary(remote) {
  const m = remote.mode
  if (m === 'smb') {
    const s = remote.smb
    return `SMB · ${s.host || '?'}${s.share ? '/' + s.share : ''}${s.sub ? '/' + s.sub : ''}`
  }
  if (m === 'ftp') {
    const f = remote.ftp
    return `FTP · ${f.host || '?'}${f.port ? ':' + f.port : ''}${f.path || ''}`
  }
  if (m === 'http') {
    return `HTTP · ${remote.http.url || '?'}`
  }
  return ''
}

/* 后端 → 本地编辑态。密码一律清空（后端不发明文），是否已存由 has_password 标记。 */
export function toLocalCfg(auto) {
  if (!auto) return { ...EMPTY_CFG }
  return {
    enabled: !!auto.enabled,
    mode: auto.mode || 'ftp',
    on_launch: auto.on_launch !== false,
    on_exit: auto.on_exit !== false,
    smb: { ...EMPTY_CFG.smb, ...(auto.smb || {}), password: '' },
    ftp: { ...EMPTY_CFG.ftp, ...(auto.ftp || {}), password: '' },
    http: { ...EMPTY_CFG.http, ...(auto.http || {}), token: '' },
    snap: { ...EMPTY_CFG.snap, ...(auto.snap || {}) },
  }
}

/* 本地编辑态 → 后端。空字符串的密码/token 一律剔除（留空 = 不修改）。 */
export function toRemoteCfg(cfg) {
  const out = {
    enabled: cfg.enabled,
    mode: cfg.mode,
    on_launch: cfg.on_launch,
    on_exit: cfg.on_exit,
    snap: {
      enabled: !!cfg.snap.enabled,
      interval: Number(cfg.snap.interval) || EMPTY_CFG.snap.interval,
      keep: Number(cfg.snap.keep) || EMPTY_CFG.snap.keep,
    },
  }
  Object.keys(MODE_FIELDS).forEach((m) => {
    out[m] = { ...cfg[m] }
    MODE_FIELDS[m].forEach((f) => {
      if (f.secret && !out[m][f.key]) delete out[m][f.key]
    })
  })
  return out
}
