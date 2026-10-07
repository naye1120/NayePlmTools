import React, { useState, useEffect, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { Save, Plug, AlertTriangle, CloudDownload, History } from 'lucide-react'
import Modal from '../ui/Modal'
import Segmented from '../ui/Segmented'
import Switch from '../ui/Switch'
import { Block, Chip, Field, Note, TimingRow } from '../game/parts'
import {
  MODES, MODE_FIELDS, FIELD_LABEL, EMPTY_CFG, toRemoteCfg,
  loadGlobalCfg, normalizeCfg, GLOBAL_KEY, isRemoteConfigured,
} from '../../utils/saveConfig'
import { testRemote } from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.144 设置页「存档配置」弹窗 —— **存档相关的唯一配置入口**
 *
 * 三块，全部全局生效（`.144` 收远程位置，`.145` 再把自动存档与本地快照也收过来）：
 *   ① 远程位置      —— 协议（SMB / FTP / HTTP）+ 主机 / 共享 / 账号
 *   ② 自动存档      —— 总开关 + 两个时机（启动游戏时拉取 / 退出游戏时回传）
 *   ③ 本地快照      —— 总开关 + 间隔（分钟）+ 保留份数
 *
 * 单游戏弹窗（SaveManagerModal）里这三块都变成**只读展示 + 动作按钮**
 * （立即拉取 / 立即推送 / 立即备份 + 该游戏的存档与快照列表）—— 用户口径是
 * 「把配置功能迁移过去，配置一个全局生效；单个游戏包开启拉取和推送按钮」。
 * 一个控件只有一个家：配置在这儿，动作在那儿。
 *
 * ──「全局生效」是怎么落地的（前后端同一条优先级，缺一不可）───────────────
 *   全局这份配置只要**写过一次**（ARCHIVE_GLOBAL_CONFIG 非空），它就对所有游戏生效；
 *   没写过（老装机）才回落到各游戏自己存的那份。判据是「写过没有」而不是「填了几个字段」，
 *   见 saveConfig.js 的 hasGlobalArchive()。
 *   前端：单游戏弹窗用它渲染只读状态，保存时也按它拼请求（resolveAutoForGame）。
 *   后端：game_auto.c 的 ga_load_cfg() 与 game_saves.c 的 gs_snap_cfg() 读的是同一个文件
 *        （ARCHIVE_GLOBAL_PATH），所以**用户不进任何一个游戏的弹窗、引擎也按全局跑** ——
 *        否则「全局生效」只是界面上的一句话。
 *
 * 存储：主配置键 ARCHIVE_GLOBAL_CONFIG（JSON 字符串），走 /set_config；
 * 后端把它落到单独一份文件（不塞进 key=value 行式的 nayeplmtools_config.txt）。
 * 密码处理与 per-game 弹窗不同：这里把完整 cfg 原样存回，所以密码会保存 / 回显
 * （后端不发明文的限制只针对 per-game 那条回传路径）。
 * 测试连接也走全局：testRemote('__global__', ...) —— 后端只按 body 里的 mode/字段连，
 * 不关心 tid 是不是真的游戏。 */

export default function ArchiveConfigModal({ show, onClose, config, onSaveConfig, addToast }) {
  const { t } = useTranslation()
  const [cfg, setCfg] = useState(EMPTY_CFG)
  const [saving, setSaving] = useState(false)
  const [testing, setTesting] = useState(false)
  const [testResult, setTestResult] = useState(null)
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false }
  }, [])

  /* 打开 / 配置变化时从全局配置键还原。 */
  useEffect(() => {
    if (!show) return
    const next = loadGlobalCfg(config)
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开/配置变化时从全局配置键还原一次
    setCfg(next)
    setTestResult(null)
  }, [show, config])

  if (!show) return null

  const mode = cfg.mode
  const fields = MODE_FIELDS[mode] || []
  const modeCfg = cfg[mode] || {}
  const modeBad = !MODES.some((m) => m.value === mode)
  const configured = isRemoteConfigured(cfg)
  /* 间隔用分钟输入（用户口径），存的是秒 —— 与后端 gs_snap_clamp_interval 同一单位。 */
  const snapMin = Math.max(1, Math.round((Number(cfg.snap.interval) || EMPTY_CFG.snap.interval) / 60))

  const setModeField = (key, val) => setCfg((c) => ({ ...c, [mode]: { ...c[mode], [key]: val } }))

  const onSaveCfg = async () => {
    if (modeBad) {
      addToast?.(t('game.mode_unsupported_toast', '传输方式认不出来，请改用 SMB / FTP / HTTP'), 'error')
      return
    }
    setSaving(true)
    setTestResult(null)
    try {
      const payload = { [GLOBAL_KEY]: JSON.stringify(normalizeCfg(cfg)) }
      const ok = await onSaveConfig(payload)
      if (ok) {
        addToast?.(t('settings.archive_saved', '全局存档配置已保存'))
      } else {
        addToast?.(t('game.auto_save_fail', '保存失败'), 'error')
      }
    } catch (e) {
      addToast?.(e.message || t('game.auto_save_fail', '保存失败'), 'error')
    }
    setSaving(false)
  }

  const onTest = async () => {
    if (modeBad) {
      addToast?.(t('game.mode_unsupported_toast', '传输方式认不出来，请改用 SMB / FTP / HTTP'), 'error')
      return
    }
    setTesting(true)
    setTestResult(null)
    try {
      const r = await testRemote('__global__', toRemoteCfg(cfg))
      if (aliveRef.current) setTestResult({ ok: true, latency: r && r.latency_ms })
    } catch (e) {
      if (aliveRef.current) setTestResult({ ok: false, message: e.message })
    }
    setTesting(false)
  }

  return (
    <Modal
      show={show}
      title={t('settings.archive_config_title', '存档配置')}
      onClose={onClose}
      layout="panel"
    >
      <div className="panel-body leading-relaxed font-normal flex flex-col gap-5">
        <Note tone="info">
          {t('settings.archive_config_hint', '这里配置的三件事对所有游戏生效：远程位置、自动存档的开关与时机、本地快照的间隔与保留份数。单个游戏的「存档管理」里只显示这些设置并执行（立即拉取 / 立即推送 / 立即备份），不重复配置。')}
        </Note>

        {/* ── ① 远程位置 ── */}
        <Block
          title={t('game.remote_location', '远程位置')}
          icon={Save}
          actions={
            <Chip tone={configured ? 'ok' : 'muted'}>
              {configured ? t('game.on', '已启用') : t('game.off', '未启用')}
            </Chip>
          }
        >
          <Segmented
            options={MODES}
            value={mode}
            onChange={(v) => { setCfg((c) => ({ ...c, mode: v })); setTestResult(null) }}
            label={t('game.remote_kind', '远程位置类型')}
            className="w-full sm:w-auto"
          />
          {modeBad && (
            <Note tone="error" icon={AlertTriangle} className="mt-2">
              {t('game.mode_unsupported', '这个传输方式（{{mode}}）当前版本认不出来，请在 SMB / FTP / HTTP 里选一个。').replace('{{mode}}', mode)}
            </Note>
          )}

          <div className="grid grid-cols-1 sm:grid-cols-2 gap-3 mt-4">
            {fields.map((f) => (
              <Field
                key={f.key}
                label={t(`game.sf_${f.key}`, FIELD_LABEL[f.key] || f.key)}
                className={f.key === 'path' || f.key === 'url' || f.key === 'sub' ? 'sm:col-span-2' : undefined}
              >
                <input
                  className="field"
                  type={f.secret ? 'password' : 'text'}
                  inputMode={f.numeric ? 'numeric' : undefined}
                  autoComplete="off"
                  disabled={modeBad}
                  placeholder={f.ph}
                  value={modeCfg[f.key] || ''}
                  onChange={(e) => setModeField(f.key, e.target.value)}
                />
              </Field>
            ))}
          </div>

          <div className="flex flex-wrap items-center gap-2 mt-4">
            <button type="button" className="btn btn-primary" disabled={saving || modeBad} onClick={onSaveCfg}>
              {saving ? t('game.saving', '保存中…') : t('game.save_settings', '保存设置')}
            </button>
            <button type="button" className="btn btn-outline" disabled={testing || modeBad} onClick={onTest}>
              <Plug className="w-4 h-4" />
              <span>{testing ? t('game.testing', '测试中…') : t('game.test_conn', '测试连接')}</span>
            </button>
          </div>

          {testResult && (
            <Note
              tone={testResult.ok ? 'ok' : 'error'}
              className="mt-3"
            >
              {testResult.ok
                ? t('game.test_ok', '连接成功{{lat}}', { lat: testResult.latency ? `（${testResult.latency} ms）` : '' })
                : t('game.test_fail', '连接失败：{{why}}', { why: testResult.message })}
            </Note>
          )}
        </Block>

        {/* ── ② 自动存档（.145 从单游戏弹窗迁上来）── */}
        <Block
          title={t('game.auto_title', '自动存档')}
          icon={CloudDownload}
          actions={
            <Chip tone={cfg.enabled ? 'ok' : 'muted'}>
              {cfg.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}
            </Chip>
          }
        >
          <div className="flex items-center justify-between gap-4">
            <div className="min-w-0">
              <p className="text-white panel-body font-semibold">{t('game.auto_enable', '启用自动存档')}</p>
              <p className="text-zinc-500 panel-tiny mt-0.5">
                {t('settings.auto_enable_hint_global', '开关打开后，主机按下面的时机自动与远程位置同步每个游戏的存档。对所有游戏生效。')}
              </p>
            </div>
            <Switch
              checked={cfg.enabled}
              onChange={(v) => setCfg((c) => ({ ...c, enabled: v }))}
              label={t('game.auto_enable', '启用自动存档')}
            />
          </div>

          {/* 两个时机是独立开关，可以只开一个；总开关关掉时这一档整块灰掉 */}
          <div className={cfg.enabled ? 'mt-4' : 'mt-4 opacity-50 pointer-events-none'}>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
              <TimingRow
                label={t('game.auto_on_launch', '启动游戏时拉取')}
                hint={t('game.auto_on_launch_hint', '把远程的存档下载到主机，覆盖本地')}
                checked={cfg.on_launch}
                onChange={(v) => setCfg((c) => ({ ...c, on_launch: v }))}
              />
              <TimingRow
                label={t('game.auto_on_exit', '退出游戏时回传')}
                hint={t('game.auto_on_exit_hint', '把主机的存档上传到远程位置')}
                checked={cfg.on_exit}
                onChange={(v) => setCfg((c) => ({ ...c, on_exit: v }))}
              />
            </div>
          </div>
        </Block>

        {/* ── ③ 本地快照（.145 从单游戏弹窗迁上来）── */}
        <Block
          title={t('game.snap_title', '本地快照')}
          icon={History}
          actions={
            <Chip tone={cfg.snap.enabled ? 'ok' : 'muted'}>
              {cfg.snap.enabled ? t('game.on', '已启用') : t('game.off', '未启用')}
            </Chip>
          }
        >
          <div className="flex items-center justify-between gap-4">
            <div className="min-w-0">
              <p className="text-white panel-body font-semibold">{t('game.snap_enable', '启用本地快照')}</p>
              <p className="text-zinc-500 panel-tiny mt-0.5">
                {t('settings.snap_enable_hint_global', '不依赖网络：主机按下面的间隔把存档整份复制到本地 /data 下，超保留数自动删最旧。远程同步连不上时，这一条是最后的兜底。对所有游戏生效。')}
              </p>
            </div>
            <Switch
              checked={cfg.snap.enabled}
              onChange={(v) => setCfg((c) => ({ ...c, snap: { ...c.snap, enabled: v } }))}
              label={t('game.snap_enable', '启用本地快照')}
            />
          </div>

          <div className={cfg.snap.enabled ? 'mt-4' : 'mt-4 opacity-50 pointer-events-none'}>
            <div className="grid grid-cols-1 sm:grid-cols-2 gap-3">
              <Field
                label={t('game.snap_interval', '间隔（分钟）')}
                hint={t('game.snap_interval_hint', '最短 1 分钟，最长 1440（一天）。改完记得点保存。')}
              >
                <input
                  className="field"
                  type="number"
                  min="1"
                  max="1440"
                  inputMode="numeric"
                  value={snapMin}
                  disabled={!cfg.snap.enabled}
                  onChange={(e) => {
                    const v = Math.min(1440, Math.max(1, Number(e.target.value) || 1))
                    setCfg((c) => ({ ...c, snap: { ...c.snap, interval: v * 60 } }))
                  }}
                />
              </Field>
              <Field
                label={t('game.snap_keep', '保留份数')}
                hint={t('game.snap_keep_hint', '每份存档最多留这么多份，超了删最旧（1~20）。')}
              >
                <input
                  className="field"
                  type="number"
                  min="1"
                  max="20"
                  inputMode="numeric"
                  value={cfg.snap.keep}
                  disabled={!cfg.snap.enabled}
                  onChange={(e) => {
                    const v = Math.min(20, Math.max(1, Number(e.target.value) || 1))
                    setCfg((c) => ({ ...c, snap: { ...c.snap, keep: v } }))
                  }}
                />
              </Field>
            </div>

            <div className="flex flex-wrap items-center gap-2 mt-4">
              <button type="button" className="btn btn-primary" disabled={saving} onClick={onSaveCfg}>
                {saving ? t('game.saving', '保存中…') : t('game.save_settings', '保存设置')}
              </button>
            </div>
          </div>
        </Block>
      </div>
    </Modal>
  )
}
