import React, { useState } from 'react'
import { Terminal, ChevronRight, Globe, Languages, Palette, CloudDownload, Power, Save, AppWindow, Upload } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../../utils/helpers'
/* [CN-FORK] v0.6.0 合入：导入取两侧并集 —— LANGUAGE_NAMES（上游的语言选择行在用）
 * 与 fork 的 ThemeToggle/PageHead/Section/Switch/Segmented/ArchiveConfigModal/SystemAppsModal
 * （外观切换、页头板块、互斥档位、两个弹窗）互不重叠，都留。 */
import { LANGUAGE_NAMES } from '../../utils/localeSupport'
// [CN-FORK] 外观（深/浅色）切换控件
import ThemeToggle from '../../theme/ThemeToggle'
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'
import Switch from '../ui/Switch'
import Segmented from '../ui/Segmented' /* [CN-FORK] v0.5.2.97 互斥档位统一 */
// [CN-FORK] v0.5.2.143 设置页「存档配置」弹窗
import ArchiveConfigModal from './ArchiveConfigModal'
// [CN-FORK] v0.6.0.177 设置页「多媒体存档配置」弹窗（独立一份 SMB/FTP + 自动备份开关，
// 与上面的存档配置并列、互不相干 —— 见 MediaArchiveConfigModal 文件头）
import MediaArchiveConfigModal from './MediaArchiveConfigModal'
// [CN-FORK] v0.5.2.158 设置页「个性化软件」弹窗（主机内置软件的显示 / 隐藏）
import SystemAppsModal from './SystemAppsModal'
// [CN-FORK] v0.5.2.154 「更新日志」与「支持作者」两块整体搬到「关于」页（AboutView）。
// 随之删掉的：CHANGELOG 数组、changelog 的 <Modal>、donate 的 <Section>，
// 以及只给它们用的 ScrollText / Heart / Modal 三个 import。

const FOLLOW_BROWSER_LANGUAGE = '__auto__'

const isFollowingBrowserLanguage = () => {
  try {
    return !localStorage.getItem('i18nextLng')
  } catch {
    return true
  }
}

/* [CN-FORK] v0.5.2.94 设置行只剩一种形状：图标 + 标题/说明 + 右侧控件。
 * 此前每个开关都就地抄一遍 w-14 h-7 / translate-x-7 的滑块 markup（五个副本），
 * 现在换成共用的 <Switch>；说明文字不再靠 hidden md:!block 写两份。 */
const SettingRow = ({ title, description, children, icon: Icon, stacked }) => (
  <div className={cn(
    "p-4 md:p-5 bg-white/[0.03] rounded-xl border border-white/10 hover:border-ps-blue/30 transition-colors h-full",
    stacked ? "flex flex-col" : "flex items-center"
  )}>
    <div className="flex items-center min-w-0 flex-1">
      {Icon && (
        <div className="p-3 shrink-0 -ml-3">
          <Icon className="w-5 h-5 text-ps-blue" />
        </div>
      )}
      <div className="min-w-0 ml-3">
        <p className="font-bold text-white text-base tracking-tight leading-tight">{title}</p>
        {description && <p className="text-sm text-zinc-500 leading-relaxed mt-1">{description}</p>}
      </div>
    </div>
    {children && <div className={cn("shrink-0", stacked ? "w-full mt-4" : "ml-4")}>{children}</div>}
  </div>
)

/* [CN-FORK] v0.5.2.94 「点整行去别处」的几份副本（管理来源 / 存档配置 / 日志 / 个性化软件）
 * 收成一个：整行就是按钮，命中区从图标那条 24px 变成整行，遥控器移到行上直接确认即可。 */
const NavRow = ({ icon: _Icon, title, description, onClick, danger }) => (
  <button
    onClick={onClick}
    className="row-link group w-full text-left flex items-center p-4 md:p-5 bg-white/[0.03] rounded-xl border border-white/10 hover:border-ps-blue/30 transition-colors"
  >
    <div className="p-3 shrink-0 -ml-3">
      <_Icon className={cn("w-5 h-5", danger ? "text-zinc-500 group-hover:text-red-500" : "text-ps-blue")} />
    </div>
    <div className="min-w-0 flex-1 ml-3">
      <p className="font-bold text-white text-base tracking-tight leading-tight">{title}</p>
      {description && <p className="text-sm text-zinc-500 leading-relaxed mt-1">{description}</p>}
    </div>
    <ChevronRight className={cn("w-6 h-6 shrink-0 ml-3 text-zinc-700 group-hover:translate-x-1 transition-transform",
      danger ? "group-hover:text-red-500" : "group-hover:text-ps-blue")} />
  </button>
)

/* [CN-FORK] v0.6.0 合入：签名取并集 —— addToast 是 fork 侧在用的（toast 通知），
 * onClearCache 是上游「仓库缓存」行在用的，两个都要。 */
const SettingsView = ({ config, onSaveConfig, addToast, setShowLogs, onNavigate, onClearCache }) => {
  const { t, i18n } = useTranslation()
  const [showArchiveConfig, setShowArchiveConfig] = useState(false)
  /* [CN-FORK] v0.6.0.177 多媒体存档配置弹窗（存档配置右边那一格） */
  const [showMediaArchive, setShowMediaArchive] = useState(false)
  /* [CN-FORK] v0.5.2.158 「个性化软件」弹窗 */
  const [showSystemApps, setShowSystemApps] = useState(false)
  const autoOpen = config.AUTO_BROWSER_OPEN !== false
  const autoInstall = config.AUTO_INSTALL_APP !== false
  const autoloadDelay = config.AUTOLOAD_DELAY || 5
  /* 上游 v0.6.2 #123：默认关（后端 config.c 的 defaults 里也是 0）。 */
  const autoloadReturn = config.AUTOLOAD_RETURN_TO_DASHBOARD === true
  /* [CN-FORK] v0.5.2.67 默认开启多来源（后端缺键时按开处理，与 config_read 默认值一致） */
  const multiSources = config.MULTI_SOURCES_ENABLED !== false
  /* [CN-FORK] v0.5.2.67 默认云端仓库总开关：本版默认关（后端缺键时按关处理，与 config_read 默认值一致） */
  const defaultRepo = config.DEFAULT_REPO_ENABLED === true
  const [followBrowserLanguage, setFollowBrowserLanguage] = useState(isFollowingBrowserLanguage)

  const [clearingCache, setClearingCache] = useState(false)
  const [cacheMessage, setCacheMessage] = useState('')
  const handleClearCache = async () => {
    if (clearingCache) return
    setClearingCache(true)
    setCacheMessage('')
    try {
      await onClearCache()
      setCacheMessage(t('settings.cache_cleared', 'Repository cache cleared.'))
    } catch {
      setCacheMessage(t('settings.cache_failed', 'Could not clear repository cache.'))
    } finally { setClearingCache(false) }
  }

  const getLanguageDisplayName = lang => LANGUAGE_NAMES[lang] || lang;

  const currentLang = i18n.resolvedLanguage || i18n.language || 'en';
  const selectedLanguage = followBrowserLanguage ? FOLLOW_BROWSER_LANGUAGE : currentLang;

  const handleLanguageChange = async (event) => {
    const language = event.target.value;
    const followBrowser = language === FOLLOW_BROWSER_LANGUAGE;
    setFollowBrowserLanguage(followBrowser);
    try {
      if (followBrowser) {
        localStorage.removeItem('i18nextLng');
      } else {
        localStorage.setItem('i18nextLng', language);
      }
    } catch {
      // Continue with an in-memory language change if storage is unavailable.
    }
    await i18n.changeLanguage(followBrowser ? undefined : language);
  };

  return (
    /* [CN-FORK] v0.5.2.99 容器对齐「管理 Payload」：去掉 max-w-5xl 居中与页内 pb-12，
     * 正文吃满 main 的内容列；页头因此恢复出血（bleed 默认 true），粘顶条的分界线铺到边。 */
    <div className="space-y-6">
      <PageHead
        title={t("settings.title", "Settings")}
      />

      {/* Startup + Appearance + Behaviour */}
      <Section
        id="startup"
        icon={Power}
        title={t("settings.startup_title", "Startup & Interface")}
        bodyClass="grid grid-cols-1 xl:grid-cols-2 gap-4"
      >
        <SettingRow
          title={t("settings.language_title", "Language")}
          description={t("settings.language_desc", "Change the display language of the application.")}
          icon={Languages}
          stacked
        >
          {/* [CN-FORK] v0.5.2.94 语言选择器从「假按钮 + 一层 opacity-0 的原生 select」改成真正的
              select：假按钮没有下拉箭头的聚焦态、遥控器看不出自己站在什么控件上，选项又多到不该铺成芯片。 */}
          <select
            value={selectedLanguage}
            onChange={handleLanguageChange}
            className="field w-full font-bold"
            aria-label={t("settings.language_title", "Language")}
          >
            <option value={FOLLOW_BROWSER_LANGUAGE} className="bg-ps-surface">
              {t("settings.language_system_default", "System Default")}
            </option>
            {Object.keys(i18n.store.data).map(lang => (
              <option key={lang} value={lang} className="bg-ps-surface">{getLanguageDisplayName(lang)}</option>
            ))}
          </select>
          <p className="text-sm text-zinc-500 leading-relaxed mt-2">
            {t("settings.language_disclaimer", "Translations are community-driven and may contain errors.")}
          </p>
        </SettingRow>

        {/* [CN-FORK] 外观：深色 / 浅色 / 跟随系统 */}
        <SettingRow
          title={t("settings.theme_title", "Appearance")}
          description={t("settings.theme_desc", "Choose the dark or light interface.")}
          icon={Palette}
          stacked
        >
          <ThemeToggle />
        </SettingRow>

        <SettingRow title={t("settings.auto_open_title", "Auto-open Browser")} description={t("settings.auto_open_desc", "Automatically launch the browser when Payload Manager payload is executed.")}>
          <Switch checked={autoOpen} onChange={(v) => onSaveConfig({ AUTO_BROWSER_OPEN: v })} label={t("settings.auto_open_title", "Auto-open Browser")} />
        </SettingRow>

        <SettingRow title={t("settings.auto_install_title", "Auto-install App Launcher")} description={t("settings.auto_install_desc", "Automatically install the Payload Manager app to the PS5 home screen.")}>
          <Switch checked={autoInstall} onChange={(v) => onSaveConfig({ AUTO_INSTALL_APP: v })} label={t("settings.auto_install_title", "Auto-install App Launcher")} />
        </SettingRow>

        <SettingRow title={t("settings.kill_disc_title", "Kill Disc Player")} description={t("settings.kill_disc_desc", "Automatically terminate the Disc Player application on startup (for BD-JB users).")}>
          <Switch
            checked={config.KILL_DISC_PLAYER_ON_STARTUP !== false}
            onChange={(v) => onSaveConfig({ KILL_DISC_PLAYER_ON_STARTUP: v })}
            label={t("settings.kill_disc_title", "Kill Disc Player")}
          />
        </SettingRow>

        <SettingRow title={t("settings.scan_usb_title", "Scan USB Payloads")} description={t("settings.scan_usb_desc", "Enable scanning for .elf and .bin files in the root directory of USB drives (/mnt/usb0-7).")}>
          <Switch checked={!!config.SCAN_USB_PAYLOADS} onChange={(v) => onSaveConfig({ SCAN_USB_PAYLOADS: v })} label={t("settings.scan_usb_title", "Scan USB Payloads")} />
        </SettingRow>

        {/* [CN-FORK] v0.5.1.64 「读取硬件信息」开关连同主页仪表盘、后端 stats.c 一并删除：
            温度/风扇/内存/显存/存储/FPS/当前游戏 不再探测，设置项也就没有存在意义 */}

        <SettingRow
          title={t("settings.autoload_delay_title", "Autoload Delay")}
          description={t("settings.autoload_delay_desc", "Wait time before the autoload sequence begins.")}
          stacked
        >
          <div className="flex items-center justify-between">
            {/* [CN-FORK] v0.5.2.97 延时档位归到共用 <Segmented>（与管理页排序、温控档位、外观三档同一形状） */}
            <Segmented
              label={t("settings.autoload_delay_title", "Autoload Delay")}
              value={autoloadDelay}
              onChange={(v) => onSaveConfig({ AUTOLOAD_DELAY: v })}
              options={[3, 5, 10].map((s) => ({ value: s, label: `${s}s` }))}
            />
            <span className="text-ps-blue font-bold text-lg tabular-nums ml-4">{autoloadDelay}s</span>
          </div>
        </SettingRow>

        {/* 上游 v0.6.2 #123「自动加载成功后自动返回」。上游的原意是回 dashboard，
            fork 没有 dashboard 视图（「管理 Payload」一页统管），所以 handleFinish
            在 App.jsx 里改指向 storage；文案也据此改写，不再提「仪表盘」。 */}
        <SettingRow
          title={t("settings.autoload_return_title", "全部加载成功后返回")}
          description={t("settings.autoload_return_desc", "全部 payload 加载成功后约 3 秒自动返回「管理 Payload」；有失败或被跳过时停在结果页，方便查看原因。")}
        >
          <Switch
            checked={autoloadReturn}
            onChange={(v) => onSaveConfig({ AUTOLOAD_RETURN_TO_DASHBOARD: v })}
            label={t("settings.autoload_return_title", "全部加载成功后返回")}
          />
        </SettingRow>

        {/* [CN-FORK] v0.5.2.158 「个性化软件」：整行点开一个弹窗，里面逐款给开关。
            放这一节是因为它改的就是「主屏长什么样」，与语言/外观同一类；
            状态不在本面板的 config 里 —— 真值在主机应用数据库那一列，所以走独立端点。 */}
        <NavRow
          icon={AppWindow}
          title={t("appsys.title", "个性化软件")}
          description={t("appsys.row_desc", "隐藏或显示主机自带的内置软件。")}
          onClick={() => setShowSystemApps(true)}
        />
      </Section>

      {/* Multi-Source */}
      <Section
        id="sources"
        icon={Globe}
        title={t("settings.sources_title", "Payload Sources")}
        bodyClass="space-y-4"
      >
        <div className="grid grid-cols-1 xl:grid-cols-2 gap-4">
          <SettingRow title={t("settings.multi_sources_title", "Multiple Payload Sources")} description={t("settings.multi_sources_desc", "Enable third-party payload repositories. Payloads from multiple sources are grouped by catalog in the Manage tab.")}>
            <Switch checked={multiSources} onChange={(v) => onSaveConfig({ MULTI_SOURCES_ENABLED: v })} label={t("settings.multi_sources_title", "Multiple Payload Sources")} />
          </SettingRow>

          {/* [CN-FORK] v0.5.1.65 默认云端仓库总开关：内置官方源在国内经常拉不动，
           * 关掉后开机不再拉 payloads.json，管理页也不出这一组目录 */}
          <SettingRow title={t("settings.default_repo_title", "Default Cloud Repository")} description={t("settings.default_repo_desc", "The built-in official payload catalog. Turn it off to stop every request to it (including the startup refresh); your own sources keep working.")} icon={CloudDownload}>
            <Switch checked={defaultRepo} onChange={(v) => onSaveConfig({ DEFAULT_REPO_ENABLED: v })} label={t("settings.default_repo_title", "Default Cloud Repository")} />
          </SettingRow>

          {/* [CN-FORK] v0.6.0 合入：上游的「仓库缓存」行（清掉已下载的目录与图标后重新拉取，
              已安装的 payload 与设置不动）。它依赖公共区里的 handleClearCache/clearingCache/
              cacheMessage 与 App 传下来的 onClearCache。上游那份「管理来源」大按钮与本节
              末尾 fork 的 <NavRow> 管理来源重复 → 不保留。vertical 在本文件的 SettingRow 上
              叫 stacked（入参名），照本文件口径改。 */}
          <SettingRow
            title={t('settings.cache_title', 'Repository Cache')}
            description={t('settings.cache_desc', 'Clear downloaded catalogs and icons, then fetch fresh copies. Installed payloads and your settings are preserved.')}
            icon={Globe}
            stacked
          >
            <button onClick={handleClearCache} disabled={clearingCache}
              className="px-5 py-3 rounded-xl bg-white/10 hover:bg-ps-blue/20 disabled:opacity-50 font-bold">
              {clearingCache ? t('settings.cache_clearing', 'Clearing…') : t('settings.cache_clear', 'Clear Cache')}
            </button>
            {cacheMessage && <p role="status" className="mt-3 text-sm text-zinc-400">{cacheMessage}</p>}
          </SettingRow>
        </div>

        {!defaultRepo && !multiSources && (
          <p className="text-xs text-amber-400/80 leading-relaxed px-1">
            {t("settings.default_repo_off_hint", "Default repository off and multi-source mode disabled: the Manage tab will only list local payloads. Turn on Multiple Payload Sources to use your own repositories.")}
          </p>
        )}

        {multiSources && (
          <NavRow
            icon={Globe}
            title={t("settings.manage_sources_title", "Manage Sources")}
            description={t("settings.manage_sources_desc", "Add, remove, or reorder your payload repositories.")}
            onClick={() => onNavigate('sources')}
          />
        )}
      </Section>

      {/* [CN-FORK] v0.5.2.143 存档配置：设置页新增入口，统一管理自动存档 / 本地快照默认参数。 */}
      <Section
        id="archive"
        icon={Save}
        title={t("settings.archive_title", "存档配置")}
        bodyClass="grid grid-cols-1 xl:grid-cols-2 gap-4"
      >
        <NavRow
          icon={Save}
          title={t("settings.archive_title", "存档配置")}
          description={t("settings.archive_desc", "配置自动存档与本地快照的默认参数。")}
          onClick={() => setShowArchiveConfig(true)}
        />

        {/* [CN-FORK] v0.6.0.177 多媒体存档配置：与上面那颗**并列的第二格**（本节 body
            是 xl:grid-cols-2，两颗正好铺满一行）。刻意不合并进存档配置弹窗 ——
            用户口径是多媒体有自己的 SMB/FTP 位置，两个开关长在一张表里迟早被一起改坏。 */}
        <NavRow
          icon={Upload}
          title={t("settings.media_archive_title", "多媒体存档配置")}
          description={t("settings.media_archive_desc", "配置截图 / 录屏的远程位置与自动备份。")}
          onClick={() => setShowMediaArchive(true)}
        />
      </Section>

      {/* Diagnostics */}
      <Section
        id="diagnostics"
        icon={Terminal}
        title={t("settings.diagnostics_title", "Diagnostics")}
        bodyClass="grid grid-cols-1 xl:grid-cols-2 gap-4"
      >
        <NavRow
          icon={Terminal}
          title={t("settings.log_viewer_title", "Open Log Viewer")}
          description={t("settings.log_viewer_desc", "Access real-time debug output from the Payload Manager daemon.")}
          onClick={() => setShowLogs(true)}
        />
        {/* [CN-FORK] v0.5.2.154 「更新日志」这一行搬到「关于」页去了（用户口径：
            「把更新日志和支持作者的内容全部放到关于里面」），这里不再留入口。 */}
      </Section>

      {/* [CN-FORK] v0.5.2.154 存档配置弹窗 */}
      <ArchiveConfigModal
        show={showArchiveConfig}
        onClose={() => setShowArchiveConfig(false)}
        config={config}
        onSaveConfig={onSaveConfig}
        addToast={addToast}
      />

      {/* [CN-FORK] v0.6.0.177 多媒体存档配置弹窗 */}
      <MediaArchiveConfigModal
        show={showMediaArchive}
        onClose={() => setShowMediaArchive(false)}
        config={config}
        onSaveConfig={onSaveConfig}
        addToast={addToast}
      />

      {/* [CN-FORK] v0.5.2.158 个性化软件弹窗 */}
      <SystemAppsModal
        show={showSystemApps}
        onClose={() => setShowSystemApps(false)}
        addToast={addToast}
      />
    </div>
  )
}

export default SettingsView
