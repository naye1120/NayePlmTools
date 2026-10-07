import React, { useState, useEffect, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { Save, Plug, AlertTriangle } from 'lucide-react'
import Modal from '../ui/Modal'
import Segmented from '../ui/Segmented'
import { Block, Chip, Field, Note } from '../game/parts'
import {
  MODES, MODE_FIELDS, FIELD_LABEL, EMPTY_CFG, toRemoteCfg,
  loadGlobalCfg, normalizeCfg, GLOBAL_KEY, isRemoteConfigured,
} from '../../utils/saveConfig'
import { testRemote } from '../../utils/gameApi'

/* [CN-FORK] v0.5.2.144 设置页「存档配置」弹窗 —— **全局远程位置**的唯一入口
 *
 * 见 SaveManagerModal 文件头：单游戏弹窗只留「自动存档开关 + 两个时机 + 立即拉取/推送 +
 * 本地快照」，**远程位置（协议 / 主机 / 共享 / 账号）整体迁到设置页这里**，全局生效。
 *
 * 为什么只放「远程位置」:远程位置（同一台 NAS / 同一个 FTP）是跨游戏共享的那一份配置，
 * 迁到全局才不重复填;而「自动存档开关 / 时机 / 本地快照间隔」是**每个游戏各自不同**的策略
 * （有的游戏想每次退出都传，有的不想），仍归各自的「存档管理」。一个控件只有一个家,
 * 这正是用户「把配置功能迁移过去、配置一个全局生效、单游戏只留拉取和推送」的字面落实。
 *
 * 存储:主配置键 ARCHIVE_GLOBAL_CONFIG（JSON 字符串），走 /set_config。
 * 密码处理与 per-game 弹窗不同:全局配置没有「后端不回传密码」的限制，这里把完整 cfg
 * 以 JSON 形式存回 /set_config，所以密码会原样保存 / 回显。
 * 测试连接也走全局:testRemote('__global__', ...) —— 后端只按 body 里的 mode/字段连，
 * 不关心 tid 是不是真的游戏。
 *
 * loadGlobalCfg / normalizeCfg / GLOBAL_KEY 抽在 saveConfig.js 里,与单游戏弹窗共用同一份
 * 解析逻辑（单游戏弹窗只**读**这份全局配置、不再自己编辑远程位置）。 */

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
          {t('settings.archive_config_hint', '这里配置的远程位置（主机 / 共享 / 账号等）对所有游戏生效：单游戏弹窗里的「远程位置」只读显示它，保存后该游戏就按它推拉；自动存档的开关与时机、本地快照仍在该游戏的「存档管理」里单独设置。')}
        </Note>

        {/* 远程位置 */}
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

        {/* 自动存档 / 本地快照的开关与时机**每个游戏各不相同**，归各自的「存档管理」，
            这里不再重复一份 —— 一个控件只有一个家。 */}
        <p className="panel-tiny text-zinc-500">
          {t('settings.archive_pergame_hint', '自动存档的开关与时机、本地快照的间隔与保留份数，请在每个游戏的「存档管理」里单独设置。')}
        </p>
      </div>
    </Modal>
  )
}
