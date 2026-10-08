/* [CN-FORK] v0.5.2.143 存档配置常量与转换函数
 *
 * 原本只活在 SaveManagerModal 里；设置页新增「存档配置」入口后，
 * 全局默认配置弹窗也要用同一套协议、字段定义、编辑态/后端态转换。
 * 抽到这里，避免两份一样的 MODES / MODE_FIELDS / EMPTY_CFG 互相跑偏。
 */

/* SMB 已解禁（走 libsmb2）；FTP / HTTP 暂时禁用，后续开发。 */
export const MODES = [
  { value: 'smb', label: 'SMB' },
  { value: 'ftp', label: 'FTP', disabled: true, title: '后续开发，暂不可用' },
  { value: 'http', label: 'HTTP', disabled: true, title: '后续开发，暂不可用' },
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
  mode: 'smb',
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
    mode: cfg.mode || 'smb',
    smb: { ...EMPTY_CFG.smb, ...(cfg.smb || {}) },
    ftp: { ...EMPTY_CFG.ftp, ...(cfg.ftp || {}) },
    http: { ...EMPTY_CFG.http, ...(cfg.http || {}) },
  }
}

/* [CN-FORK] v0.5.2.145 「全局是不是在生效」的唯一判据。
 *
 * 判据是**全局那份配置写过没有**（主配置里 ARCHIVE_GLOBAL_CONFIG 是个非空串），
 * 而不是「全局里填了几个字段」。理由：后者会引出一堆没法解释的分支 ——
 * 用户在设置页把主机清空保存（本意是「别同步了」），按内容判就会悄悄回落到
 * 这个游戏很久以前存的旧主机上，继续往一个他以为已经删掉的地方推。
 * 「写过就算数」只有一条规则，用户改了什么就看到什么。
 *
 * 后端 game_auto.c / game_saves.c 用的是同一判据（ARCHIVE_GLOBAL_PATH 这个文件在不在），
 * 所以前后端不会各说各话。 */
export function hasGlobalArchive(config) {
  const raw = config && config[GLOBAL_KEY]
  return typeof raw === 'string' && raw.trim().length > 0
}

/* 这个游戏**实际生效**的自动存档 + 本地快照参数。
 *   全局写过 → 全局；没写过（老装机）→ 这个游戏自己存的那份；都没有 → 内置默认。
 * `from` 给单游戏弹窗决定标「（来自全局设置）」还是「（本游戏已配置）」。 */
export function resolveAutoForGame(config, perGameAuto) {
  const useGlobal = hasGlobalArchive(config)
  const src = useGlobal ? loadGlobalCfg(config) : toLocalCfg(perGameAuto)
  return {
    enabled: !!src.enabled,
    on_launch: src.on_launch !== false,
    on_exit: src.on_exit !== false,
    snap: { ...EMPTY_CFG.snap, ...(src.snap || {}) },
    from: useGlobal ? 'global' : (perGameAuto ? 'game' : 'none'),
  }
}

/* 单个游戏**实际生效**的远程位置（{mode, smb, ftp, http}），与上面同一优先级。
 * 后端没回传密码，所以回退段的密码是空串 —— toRemoteCfg 会把它当「不修改」。 */
export function resolveRemoteForGame(config, perGameAuto) {
  const useGlobal = hasGlobalArchive(config)
  if (useGlobal) return remoteOf(loadGlobalCfg(config))
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
    mode: auto.mode || 'smb',
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
