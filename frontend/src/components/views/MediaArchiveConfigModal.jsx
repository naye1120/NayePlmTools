import React, { useState, useEffect, useRef } from 'react'
import { useTranslation } from 'react-i18next'
import { Save, Plug, AlertTriangle, CloudDownload, Upload } from 'lucide-react'
import Modal from '../ui/Modal'
import Segmented from '../ui/Segmented'
import Switch from '../ui/Switch'
import { Block, Chip, Field, Note } from '../game/parts'
import { MODE_FIELDS, FIELD_LABEL } from '../../utils/saveConfig'
import { testRemote } from '../../utils/gameApi'

/* [CN-FORK] v0.6.0.177 设置页「多媒体存档配置」弹窗 —— 多媒体的远程位置 + 自动备份开关
 *
 * 与「存档配置」（ArchiveConfigModal）**并列但互不相干**的两份配置，这是用户口径：
 * 多媒体要往自己的共享 / 目录推，不能跟存档挤一个位置。两份的落点因此也是两个文件：
 *   存档 → ARCHIVE_GLOBAL_PATH（ARCHIVE_GLOBAL_CONFIG）
 *   媒体 → MEDIA_ARCHIVE_PATH（MEDIA_ARCHIVE_CONFIG）
 * 只有**存储形状**是同一套（{mode, smb:{…}, ftp:{…}}），这样「测试连接」能直接复用
 * 后端的 /game_save_test（它按协议段读），字段定义也直接用 MODE_FIELDS，
 * 两个弹窗不会各自长出一套字段名。
 *
 * ── 三处与存档弹窗**故意不一样**的地方 ────────────────────────────────────
 *  ① 协议段只开 SMB + FTP。后端 sr_mode_supported 三个都认（http 也行），
 *    但 HTTP 那一档在存档弹窗里至今标着「后续开发」——没有验证过的流程就不放出来，
 *    让用户点了才知道不行，比少一个选项更伤（同 FTP 早年被禁的原因）。
 *  ② 没有「启动 / 退出」两个时机，只有一个开关：媒体是主机自己落盘的，
 *    面板拿不到「新截图落盘」的事件（铁律② 也不许轮询），唯一的触发点是
 *    **打开多媒体管理页拉列表之后**（见 media_remote.c 的 mr_auto_kick）。
 *  ③ 没有快照那一档：媒体本来就在主机上，不需要第二份本地副本。
 *
 * 自动备份是**增量**的：账本记在 MEDIA_BACKUP_STATE_DIR/<tid>.json，
 * 已经传过且大小没变的直接跳过 —— 开关说明里必须写清这一点，
 * 否则用户会以为打开它就要每次把几个 GB 的录屏重传一遍而不敢开。 */

export const MEDIA_ARCHIVE_KEY = 'MEDIA_ARCHIVE_CONFIG'

const MEDIA_MODES = [
  { value: 'smb', label: 'SMB' },
  { value: 'ftp', label: 'FTP' },
]

const MEDIA_EMPTY = {
  mode: 'smb',
  auto: false,
  smb: { host: '', port: '', share: '', sub: '', user: '', password: '' },
  ftp: { host: '', port: '', path: '', user: '', password: '' },
}

/* 主配置键 → 编辑态。与 loadGlobalCfg 同一条合并规则：三段各自补齐，
 * 免得后端回一份「只有 mode」的残缺配置就把输入框清空了。 */
function loadMediaCfg(config) {
  let stored = null
  try {
    const raw = config && config[MEDIA_ARCHIVE_KEY]
    if (raw) stored = JSON.parse(raw)
  } catch {
    stored = null
  }
  return {
    ...MEDIA_EMPTY,
    ...(stored || {}),
    smb: { ...MEDIA_EMPTY.smb, ...((stored && stored.smb) || {}) },
    ftp: { ...MEDIA_EMPTY.ftp, ...((stored && stored.ftp) || {}) },
  }
}

export default function MediaArchiveConfigModal({ show, onClose, config, onSaveConfig, addToast }) {
  const { t } = useTranslation()
  const [cfg, setCfg] = useState(MEDIA_EMPTY)
  const [saving, setSaving] = useState(false)
  const [testing, setTesting] = useState(false)
  const [testResult, setTestResult] = useState(null)
  const aliveRef = useRef(true)

  useEffect(() => {
    aliveRef.current = true
    return () => { aliveRef.current = false }
  }, [])

  useEffect(() => {
    if (!show) return
    const next = loadMediaCfg(config)
    // eslint-disable-next-line react-hooks/set-state-in-effect -- 打开/配置变化时从全局配置键还原一次
    setCfg(next)
    setTestResult(null)
  }, [show, config])

  if (!show) return null

  const mode = cfg.mode
  const fields = MODE_FIELDS[mode] || []
  const modeCfg = cfg[mode] || {}
  const modeBad = !MEDIA_MODES.some((m) => m.value === mode)
  const configured = mode === 'smb'
    ? !!(cfg.smb.host || '').trim() && !!(cfg.smb.share || '').trim()
    : !!(cfg.ftp.host || '').trim()

  const setModeField = (key, val) => setCfg((c) => ({ ...c, [mode]: { ...c[mode], [key]: val } }))

  /* 只存协议段 + 自动开关，形状与存档那份一致（见文件头）。 */
  const onSaveCfg = async () => {
    if (modeBad) {
      addToast?.(t('game.mode_unsupported_toast', '传输方式认不出来，当前仅支持 SMB'), 'error')
      return
    }
    setSaving(true)
    setTestResult(null)
    try {
      const payload = {
        [MEDIA_ARCHIVE_KEY]: JSON.stringify({
          mode: cfg.mode,
          auto: !!cfg.auto,
          smb: cfg.smb,
          ftp: cfg.ftp,
        }),
      }
      const ok = await onSaveConfig(payload)
      if (ok) {
        addToast?.(t('settings.media_saved', '多媒体存档配置已保存'), 'success')
      } else {
        addToast?.(t('game.auto_save_fail', '保存失败'), 'error')
      }
    } catch (e) {
      addToast?.(e.message || t('game.auto_save_fail', '保存失败'), 'error')
    }
    setSaving(false)
  }

  /* 测试连接走后端 /game_save_test：它按 mode 取 smb/ftp/http 段做 TCP + 协议握手。
   * 这里顺手把两个段都带上（空的那段后端读不到目标就当没配），用户切了协议再测也不用
   * 先保存一次。 */
  const onTest = async () => {
    if (modeBad) {
      addToast?.(t('game.mode_unsupported_toast', '传输方式认不出来，当前仅支持 SMB'), 'error')
      return
    }
    setTesting(true)
    setTestResult(null)
    try {
      const r = await testRemote('__media__', {
        mode: cfg.mode,
        smb: cfg.smb,
        ftp: cfg.ftp,
        http: { url: '', token: '' },
      })
      if (aliveRef.current) setTestResult({ ok: true, latency: r && r.latency_ms })
    } catch (e) {
      if (aliveRef.current) setTestResult({ ok: false, message: e.message })
    }
    setTesting(false)
  }

  return (
    <Modal
      show={show}
      title={t('settings.media_archive_title', '多媒体存档配置')}
      onClose={onClose}
      layout="panel"
    >
      <div className="panel-body leading-relaxed font-normal flex flex-col gap-5">
        <Note tone="info">
          {t('settings.media_archive_hint',
            '这里配置的多媒体远程位置与存档那份互不相干，可以指向另一个共享 / 目录。远端按 <TID>/media/<原路径> 存放，主机上的目录层级原样保留。')}
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
            options={MEDIA_MODES}
            value={mode}
            onChange={(v) => { setCfg((c) => ({ ...c, mode: v })); setTestResult(null) }}
            label={t('game.remote_kind', '远程位置类型')}
            className="w-full sm:w-auto"
          />
          {modeBad && (
            <Note tone="error" icon={AlertTriangle} className="mt-2">
              {t('settings.media_mode_unsupported', '这个传输方式（{{mode}}）当前版本认不出来，只支持 SMB 与 FTP。').replace('{{mode}}', mode)}
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
            <Note tone={testResult.ok ? 'ok' : 'error'} className="mt-3">
              {testResult.ok
                ? t('game.test_ok', '连接成功{{lat}}', { lat: testResult.latency ? `（${testResult.latency} ms）` : '' })
                : t('game.test_fail', '连接失败：{{why}}', { why: testResult.message })}
            </Note>
          )}
        </Block>

        {/* ── ② 自动备份（唯一的触发点写在说明里）── */}
        <Block
          title={t('settings.media_auto_title', '自动备份')}
          icon={CloudDownload}
          actions={
            <Chip tone={cfg.auto ? 'ok' : 'muted'}>
              {cfg.auto ? t('game.on', '已启用') : t('game.off', '未启用')}
            </Chip>
          }
        >
          <div className="flex items-center justify-between gap-4">
            <div className="min-w-0">
              <p className="text-white panel-body font-semibold">{t('settings.media_auto_enable', '新增媒体自动上传')}</p>
              <p className="text-zinc-500 panel-tiny mt-0.5">
                {t('settings.media_auto_hint',
                  '打开后，每次打开「多媒体管理」时自动把新增的截图 / 录屏增量传上去 —— 已经传过且没变的直接跳过，不会重复传。手动「远程同步」不受这个开关影响。')}
              </p>
            </div>
            <Switch
              checked={cfg.auto}
              onChange={(v) => setCfg((c) => ({ ...c, auto: v }))}
              label={t('settings.media_auto_enable', '新增媒体自动上传')}
            />
          </div>

          <div className="flex flex-wrap items-center gap-2 mt-4">
            <button type="button" className="btn btn-primary" disabled={saving || modeBad} onClick={onSaveCfg}>
              {saving ? t('game.saving', '保存中…') : t('game.save_settings', '保存设置')}
            </button>
          </div>
        </Block>

        {/* ── ③ 手动同步入口说明（按钮在多媒体管理页）── */}
        <Block title={t('settings.media_manual_title', '手动同步')} icon={Upload}>
          <Note tone="info">
            {t('settings.media_manual_hint',
              '在游戏的「多媒体管理」弹窗里点「远程同步」即可立刻推送：选了文件就只推选中的，没选就推全部（已传过的自动跳过）。')}
          </Note>
        </Block>
      </div>
    </Modal>
  )
}
