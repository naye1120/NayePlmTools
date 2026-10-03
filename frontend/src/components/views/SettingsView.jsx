import React, { useState } from 'react'
import { Terminal, ChevronRight, Globe, Languages, Palette, Heart, ScrollText, X, Package, CloudDownload } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../../utils/helpers'
// [CN-FORK] 外观（深/浅色）切换控件
import ThemeToggle from '../../theme/ThemeToggle'
// [CN-FORK] PKG Manager 管理卡片（上传更新/重启），侧栏菜单已改为直接跳转其界面
import PkgManagerCard from './PkgManagerCard'

const FOLLOW_BROWSER_LANGUAGE = '__auto__'

const isFollowingBrowserLanguage = () => {
  try {
    return !localStorage.getItem('i18nextLng')
  } catch {
    return true
  }
}

const SettingRow = ({ title, description, children, icon: Icon, vertical }) => (
  <div className={cn(
    "p-4 md:p-5 bg-white/[0.03] rounded-2xl border border-white/10 hover:border-ps-blue/30 transition-all group h-full",
    vertical ? "flex flex-col space-y-4" : "grid grid-cols-[1fr_auto] gap-x-4 gap-y-3 md:flex md:items-center md:justify-between md:gap-6"
  )}>
    <div className="flex items-start md:items-center space-x-4 md:space-x-6 min-w-0 col-span-1">
      {Icon && (
        <div className="p-3 md:p-4 bg-white/5 rounded-2xl group-hover:bg-ps-blue/10 transition-colors shrink-0">
          <Icon className="w-5 h-5 md:w-6 md:h-6 text-zinc-500 group-hover:text-ps-blue transition-colors" />
        </div>
      )}
      <div className="space-y-1 min-w-0">
        <p className="font-bold text-white uppercase text-base md:text-lg tracking-tight leading-tight">{title}</p>
        <p className={cn("text-sm text-zinc-500 max-w-md leading-relaxed", vertical ? "block mt-2" : "hidden md:!block")}>{description}</p>
      </div>
    </div>
    <div className={cn("shrink-0", vertical ? "w-full pt-2" : "col-start-2 row-start-1 md:ml-8 self-center md:self-auto")}>
      {children}
    </div>
    {!vertical && (
      <p className="md:hidden col-span-2 text-xs text-zinc-500 leading-relaxed">
        {description}
      </p>
    )}
  </div>
)

const SettingsView = ({ config, onSaveConfig, setShowLogs, onNavigate }) => {
  const { t, i18n } = useTranslation()
  const [showChangelog, setShowChangelog] = useState(false)
  const autoOpen = config.AUTO_BROWSER_OPEN !== false
  const autoInstall = config.AUTO_INSTALL_APP !== false
  const autoloadDelay = config.AUTOLOAD_DELAY || 5
  /* [CN-FORK] v0.5.2.67 默认开启多来源（后端缺键时按开处理，与 config_read 默认值一致） */
  const multiSources = config.MULTI_SOURCES_ENABLED !== false
  /* [CN-FORK] v0.5.2.67 默认云端仓库总开关：本版默认关（后端缺键时按关处理，与 config_read 默认值一致） */
  const defaultRepo = config.DEFAULT_REPO_ENABLED === true
  const [followBrowserLanguage, setFollowBrowserLanguage] = useState(isFollowingBrowserLanguage)

  const getLanguageDisplayName = (lang) => {
    let displayName = lang;
    try {
      const baseLang = lang.split('-')[0];
      const lookupLang = lang.startsWith('zh') ? lang : baseLang;
      displayName = new Intl.DisplayNames([lang], { type: 'language' }).of(lookupLang);
      displayName = displayName.charAt(0).toUpperCase() + displayName.slice(1);
    } catch {
      return lang;
    }
    return displayName;
  };

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
    <div className="max-w-5xl mx-auto space-y-10 pb-12">
      <div className="space-y-4">
        <h2 className="text-2xl font-bold text-white tracking-tight">
          {t("settings.title", "Settings")}
        </h2>
      </div>

      {/* Startup Settings */}
      <section className="space-y-5">
        <div className="grid grid-cols-1 xl:grid-cols-2 gap-6">
          <SettingRow
            title={t("settings.language_title", "Language")}
            description={t("settings.language_desc", "Change the display language of the application.")}
            icon={Languages}
            vertical
          >
            <div className="flex flex-col items-start space-y-2 w-full">
              <div className="relative w-full bg-black/50 border border-white/10 text-white rounded-xl px-4 py-3 font-bold tracking-tight hover:bg-white/5 transition-all overflow-hidden flex items-center justify-between">
                <span>
                  {followBrowserLanguage
                    ? t("settings.language_system_default", "System Default")
                    : getLanguageDisplayName(currentLang)}
                </span>
                <ChevronRight className="w-5 h-5 text-zinc-500 rotate-90" />
                <select
                  value={selectedLanguage}
                  onChange={handleLanguageChange}
                  className="absolute inset-0 w-full h-full opacity-0 cursor-pointer"
                >
                  <option value={FOLLOW_BROWSER_LANGUAGE} className="bg-[#121214]">
                    {t("settings.language_system_default", "System Default")}
                  </option>
                  {Object.keys(i18n.store.data).map(lang => (
                    <option key={lang} value={lang} className="bg-[#121214]">{getLanguageDisplayName(lang)}</option>
                  ))}
                </select>
              </div>
              <p className="text-sm text-zinc-500 leading-relaxed text-left w-full mt-1">
                {t("settings.language_disclaimer", "Translations are community-driven and may contain errors.")}
              </p>
            </div>
          </SettingRow>

          {/* [CN-FORK] 外观：深色 / 浅色 / 跟随系统 */}
          <SettingRow
            title={t("settings.theme_title", "Appearance")}
            description={t("settings.theme_desc", "Choose the dark or light interface.")}
            icon={Palette}
            vertical
          >
            <ThemeToggle />
          </SettingRow>

          <SettingRow
            title={t("settings.auto_open_title", "Auto-open Browser")}
            description={t("settings.auto_open_desc", "Automatically launch the browser when Payload Manager payload is executed.")}
          >
            <button
              onClick={() => onSaveConfig({ AUTO_BROWSER_OPEN: !autoOpen })}
              className={cn(
                "w-14 h-7 md:w-20 md:h-10 rounded-full transition-all relative p-1 md:p-1.5",
                autoOpen ? "bg-ps-blue" : "bg-white/10"
              )}
            >
              <div className={cn(
                "w-5 h-5 md:w-7 md:h-7 bg-white rounded-full transition-all",
                autoOpen ? "translate-x-7 md:translate-x-10" : "translate-x-0"
              )} />
            </button>
          </SettingRow>

          <SettingRow
            title={t("settings.auto_install_title", "Auto-install App Launcher")}
            description={t("settings.auto_install_desc", "Automatically install the Payload Manager app to the PS5 home screen.")}
          >
            <button
              onClick={() => onSaveConfig({ AUTO_INSTALL_APP: !autoInstall })}
              className={cn(
                "w-14 h-7 md:w-20 md:h-10 rounded-full transition-all relative p-1 md:p-1.5",
                autoInstall ? "bg-ps-blue" : "bg-white/10"
              )}
            >
              <div className={cn(
                "w-5 h-5 md:w-7 md:h-7 bg-white rounded-full transition-all",
                autoInstall ? "translate-x-7 md:translate-x-10" : "translate-x-0"
              )} />
            </button>
          </SettingRow>

          <SettingRow
            title={t("settings.kill_disc_title", "Kill Disc Player")}
            description={t("settings.kill_disc_desc", "Automatically terminate the Disc Player application on startup (for BD-JB users).")}
          >
            <button
              onClick={() => onSaveConfig({ KILL_DISC_PLAYER_ON_STARTUP: !config.KILL_DISC_PLAYER_ON_STARTUP })}
              className={cn(
                "w-14 h-7 md:w-20 md:h-10 rounded-full transition-all relative p-1 md:p-1.5",
                config.KILL_DISC_PLAYER_ON_STARTUP !== false ? "bg-ps-blue" : "bg-white/10"
              )}
            >
              <div className={cn(
                "w-5 h-5 md:w-7 md:h-7 bg-white rounded-full transition-all",
                config.KILL_DISC_PLAYER_ON_STARTUP !== false ? "translate-x-7 md:translate-x-10" : "translate-x-0"
              )} />
            </button>
          </SettingRow>

          <SettingRow
            title={t("settings.scan_usb_title", "Scan USB Payloads")}
            description={t("settings.scan_usb_desc", "Enable scanning for .elf and .bin files in the root directory of USB drives (/mnt/usb0-7).")}
          >
            <button
              onClick={() => onSaveConfig({ SCAN_USB_PAYLOADS: !config.SCAN_USB_PAYLOADS })}
              className={cn(
                "w-14 h-7 md:w-20 md:h-10 rounded-full transition-all relative p-1 md:p-1.5",
                config.SCAN_USB_PAYLOADS ? "bg-ps-blue" : "bg-white/10"
              )}
            >
              <div className={cn(
                "w-5 h-5 md:w-7 md:h-7 bg-white rounded-full transition-all",
                config.SCAN_USB_PAYLOADS ? "translate-x-7 md:translate-x-10" : "translate-x-0"
              )} />
            </button>
          </SettingRow>

          {/* [CN-FORK] v0.5.1.64 「读取硬件信息」开关连同主页仪表盘、后端 stats.c 一并删除：
              温度/风扇/内存/显存/存储/FPS/当前游戏 不再探测，设置项也就没有存在意义 */}

          <div className="flex flex-col justify-between p-5 bg-white/[0.03] rounded-2xl border border-white/10 space-y-4 h-full">
            <div className="flex justify-between items-center">
              <div className="space-y-1">
                <p className="font-bold text-white uppercase text-lg tracking-tight">{t("settings.autoload_delay_title", "Autoload Delay")}</p>
                <p className="text-sm text-zinc-500">{t("settings.autoload_delay_desc", "Wait time before the autoload sequence begins.")}</p>
              </div>
              <span className="text-ps-blue font-black text-2xl italic tracking-tighter">{autoloadDelay}s</span>
            </div>

            <div className="grid grid-cols-3 gap-4">
              {[3, 5, 10].map(s => (
                <button
                  key={s}
                  onClick={() => onSaveConfig({ AUTOLOAD_DELAY: s })}
                  className={cn(
                    "py-3 rounded-xl font-black text-base transition-all border uppercase italic",
                    autoloadDelay === s
                      ? "bg-ps-blue border-ps-blue text-white scale-[1.02]"
                      : "bg-white/5 border-white/10 text-zinc-500 hover:bg-white/10 hover:text-white"
                  )}
                >
                  {s}s
                </button>
              ))}
            </div>
          </div>
        </div>
      </section>

      {/* Multi-Source */}
      <section className="space-y-5">
        <h3 className="label-caps !text-ps-blue !opacity-100 flex items-center space-x-2 text-base tracking-[0.15em]">
          <Globe className="w-6 h-6" />
          <span>{t("settings.sources_title", "Payload Sources")}</span>
        </h3>

        <div className="grid grid-cols-1 xl:grid-cols-2 gap-6">
          <SettingRow
            title={t("settings.multi_sources_title", "Multiple Payload Sources")}
            description={t("settings.multi_sources_desc", "Enable third-party payload repositories. Payloads from multiple sources are grouped by catalog in the Manage tab.")}
            icon={Globe}
          >
            <button
              onClick={() => onSaveConfig({ MULTI_SOURCES_ENABLED: !multiSources })}
              className={cn(
                "w-14 h-7 md:w-20 md:h-10 rounded-full transition-all relative p-1 md:p-1.5",
                multiSources ? "bg-ps-blue" : "bg-white/10"
              )}
            >
              <div className={cn(
                "w-5 h-5 md:w-7 md:h-7 bg-white rounded-full transition-all",
                multiSources ? "translate-x-7 md:translate-x-10" : "translate-x-0"
              )} />
            </button>
          </SettingRow>

          {/* [CN-FORK] v0.5.1.65 默认云端仓库总开关：内置官方源在国内经常拉不动，
           * 关掉后开机不再拉 payloads.json，管理页也不出这一组目录 */}
          <SettingRow
            title={t("settings.default_repo_title", "Default Cloud Repository")}
            description={t("settings.default_repo_desc", "The built-in official payload catalog. Turn it off to stop every request to it (including the startup refresh); your own sources keep working.")}
            icon={CloudDownload}
          >
            <button
              onClick={() => onSaveConfig({ DEFAULT_REPO_ENABLED: !defaultRepo })}
              className={cn(
                "w-14 h-7 md:w-20 md:h-10 rounded-full transition-all relative p-1 md:p-1.5",
                defaultRepo ? "bg-ps-blue" : "bg-white/10"
              )}
            >
              <div className={cn(
                "w-5 h-5 md:w-7 md:h-7 bg-white rounded-full transition-all",
                defaultRepo ? "translate-x-7 md:translate-x-10" : "translate-x-0"
              )} />
            </button>
          </SettingRow>

          {!defaultRepo && !multiSources && (
            <p className="xl:col-span-2 text-xs text-amber-400/80 leading-relaxed px-1">
              {t("settings.default_repo_off_hint", "Default repository off and multi-source mode disabled: the Manage tab will only list local payloads. Turn on Multiple Payload Sources to use your own repositories.")}
            </p>
          )}

          {multiSources && (
            <button
              onClick={() => onNavigate('sources')}
              className="group w-full text-left grid grid-cols-[1fr_auto] md:flex md:items-center md:justify-between p-4 md:p-5 bg-white/[0.03] rounded-2xl border border-white/10 hover:border-ps-blue/50 hover:bg-ps-blue/5 transition-all gap-x-4 gap-y-3 md:gap-6"
            >
              <div className="flex items-start md:items-center space-x-4 md:space-x-6 min-w-0 col-span-1">
                <div className="p-3 md:p-4 bg-white/5 rounded-2xl group-hover:bg-ps-blue/10 transition-colors shrink-0">
                  <Globe className="w-5 h-5 md:w-6 md:h-6 text-zinc-500 group-hover:text-ps-blue transition-colors" />
                </div>
                <div className="space-y-1 min-w-0">
                  <p className="font-bold text-white uppercase text-base md:text-lg tracking-tight leading-tight">{t("settings.manage_sources_title", "Manage Sources")}</p>
                  <p className="hidden md:!block text-sm text-zinc-500 max-w-md leading-relaxed">{t("settings.manage_sources_desc", "Add, remove, or reorder your payload repositories.")}</p>
                </div>
              </div>
              <div className="shrink-0 col-start-2 row-start-1 md:ml-8 self-center md:self-auto">
                <ChevronRight className="w-6 h-6 md:w-8 md:h-8 text-zinc-700 group-hover:text-ps-blue group-hover:translate-x-2 transition-all" />
              </div>
              <p className="md:hidden col-span-2 text-xs text-zinc-500 leading-relaxed">
                {t("settings.manage_sources_desc", "Add, remove, or reorder your payload repositories.")}
              </p>
            </button>
          )}
        </div>
      </section>

      {/* Diagnostics */}
      <section className="space-y-5">
        <h3 className="label-caps !text-ps-blue !opacity-100 flex items-center space-x-2 text-base tracking-[0.15em]">
          <Terminal className="w-6 h-6" />
          <span>{t("settings.diagnostics_title", "Diagnostics")}</span>
        </h3>

        <div className="grid grid-cols-1 xl:grid-cols-2 gap-6">
        <button
          onClick={() => setShowLogs(true)}
          className="group w-full text-left grid grid-cols-[1fr_auto] md:flex md:items-center md:justify-between p-4 md:p-5 bg-white/[0.03] rounded-2xl border border-white/10 hover:border-ps-blue/50 hover:bg-ps-blue/5 transition-all gap-x-4 gap-y-3 md:gap-6"
        >
          <div className="flex items-start md:items-center space-x-4 md:space-x-6 min-w-0 col-span-1">
            <div className="p-3 md:p-4 bg-white/5 rounded-2xl group-hover:bg-ps-blue/10 transition-colors shrink-0">
              <Terminal className="w-5 h-5 md:w-6 md:h-6 text-zinc-500 group-hover:text-ps-blue transition-colors" />
            </div>
            <div className="space-y-1 min-w-0">
              <p className="font-bold text-white uppercase text-base md:text-lg tracking-tight leading-tight">{t("settings.log_viewer_title", "Open Log Viewer")}</p>
              <p className="hidden md:!block text-sm text-zinc-500 max-w-md leading-relaxed">{t("settings.log_viewer_desc", "Access real-time debug output from the Payload Manager daemon.")}</p>
            </div>
          </div>
          <div className="shrink-0 col-start-2 row-start-1 md:ml-8 self-center md:self-auto">
            <ChevronRight className="w-6 h-6 md:w-8 md:h-8 text-zinc-700 group-hover:text-ps-blue group-hover:translate-x-2 transition-all" />
          </div>
          <p className="md:hidden col-span-2 text-xs text-zinc-500 leading-relaxed">
            {t("settings.log_viewer_desc", "Access real-time debug output from the Payload Manager daemon.")}
          </p>
        </button>

        {/* [CN-FORK] 更新日志按钮 */}
        <button
          onClick={() => setShowChangelog(true)}
          className="group w-full text-left grid grid-cols-[1fr_auto] md:flex md:items-center md:justify-between p-4 md:p-5 bg-white/[0.03] rounded-2xl border border-white/10 hover:border-ps-blue/50 hover:bg-ps-blue/5 transition-all gap-x-4 gap-y-3 md:gap-6"
        >
          <div className="flex items-start md:items-center space-x-4 md:space-x-6 min-w-0 col-span-1">
            <div className="p-3 md:p-4 bg-white/5 rounded-2xl group-hover:bg-ps-blue/10 transition-colors shrink-0">
              <ScrollText className="w-5 h-5 md:w-6 md:h-6 text-zinc-500 group-hover:text-ps-blue transition-colors" />
            </div>
            <div className="space-y-1 min-w-0">
              <p className="font-bold text-white uppercase text-base md:text-lg tracking-tight leading-tight">{t("settings.changelog_title", "Changelog")}</p>
              <p className="hidden md:!block text-sm text-zinc-500 max-w-md leading-relaxed">{t("settings.changelog_desc", "View version update history for this fork.")}</p>
            </div>
          </div>
          <div className="shrink-0 col-start-2 row-start-1 md:ml-8 self-center md:self-auto">
            <ChevronRight className="w-6 h-6 md:w-8 md:h-8 text-zinc-700 group-hover:text-ps-blue group-hover:translate-x-2 transition-all" />
          </div>
          <p className="md:hidden col-span-2 text-xs text-zinc-500 leading-relaxed">
            {t("settings.changelog_desc", "View version update history for this fork.")}
          </p>
        </button>
        </div>
      </section>

      {/* [CN-FORK] PKG Manager 管理：侧栏「PKG安装」菜单直接跳转 :8844，这里保留上传更新与重启 */}
      <section className="space-y-5">
        <h3 className="label-caps !text-ps-blue !opacity-100 flex items-center space-x-2 text-base tracking-[0.15em]">
          <Package className="w-6 h-6" />
          <span>{t("settings.pkg_title", "PKG Manager")}</span>
        </h3>
        <PkgManagerCard />
      </section>

      {/* [CN-FORK] 支持作者：从移动端底部导航迁移至此，符合中文应用惯例 */}
      <section className="space-y-5">
        <h3 className="label-caps !text-ps-blue !opacity-100 flex items-center space-x-2 text-base tracking-[0.15em]">
          <Heart className="w-6 h-6" />
          <span>{t("settings.donate_title", "Support the Project")}</span>
        </h3>

        <button
          onClick={() => onNavigate('donate')}
          className="group w-full text-left grid grid-cols-[1fr_auto] md:flex md:items-center md:justify-between p-4 md:p-5 bg-white/[0.03] rounded-2xl border border-white/10 hover:border-red-500/50 hover:bg-red-500/5 transition-all gap-x-4 gap-y-3 md:gap-6"
        >
          <div className="flex items-start md:items-center space-x-4 md:space-x-6 min-w-0 col-span-1">
            <div className="p-3 md:p-4 bg-white/5 rounded-2xl group-hover:bg-red-500/10 transition-colors shrink-0">
              <Heart className="w-5 h-5 md:w-6 md:h-6 text-zinc-500 group-hover:text-red-500 transition-colors" />
            </div>
            <div className="space-y-1 min-w-0">
              <p className="font-bold text-white uppercase text-base md:text-lg tracking-tight leading-tight">{t("app.nav.donate", "Donate")}</p>
              <p className="hidden md:!block text-sm text-zinc-500 max-w-md leading-relaxed">{t("settings.donate_desc", "If you like this project, you can buy the author a coffee.")}</p>
            </div>
          </div>
          <div className="shrink-0 col-start-2 row-start-1 md:ml-8 self-center md:self-auto">
            <ChevronRight className="w-6 h-6 md:w-8 md:h-8 text-zinc-700 group-hover:text-red-500 group-hover:translate-x-2 transition-all" />
          </div>
          <p className="md:hidden col-span-2 text-xs text-zinc-500 leading-relaxed">
            {t("settings.donate_desc", "If you like this project, you can buy the author a coffee.")}
          </p>
        </button>
      </section>

      {/* [CN-FORK] 更新日志弹窗 */}
      {showChangelog && (
        <div className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 backdrop-blur-sm p-4" onClick={() => setShowChangelog(false)}>
          <div className="bg-[#1a1a1e] border border-white/10 rounded-2xl w-full max-w-lg max-h-[70vh] flex flex-col shadow-2xl" onClick={e => e.stopPropagation()}>
            <div className="flex items-center justify-between p-5 border-b border-white/10 shrink-0">
              <h3 className="text-lg font-bold text-white uppercase tracking-tight">{t("settings.changelog_title", "Changelog")}</h3>
              <button onClick={() => setShowChangelog(false)} className="p-2 rounded-lg hover:bg-white/10 transition-colors">
                <X className="w-5 h-5 text-zinc-400" />
              </button>
            </div>
            <div className="overflow-y-auto p-5 space-y-4 text-sm leading-relaxed">
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.2.67</p>
                <p className="text-zinc-400">合并上游 v0.5.2；修复主屏启动器图标不出现（icon0.png 重生成为 1024×1024 RGBA）；默认开启「多个来源」、默认关闭内置官方云端仓库，并首次开机预置启用 repo.630810.xyz。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.66</p>
                <p className="text-zinc-400">精简界面文案：「更新日志」各条压缩为一行摘要，「参考与致谢」去掉每条借鉴说明、只保留作者名与地址。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.65</p>
                <p className="text-zinc-400">新增「默认云端仓库」总开关（DEFAULT_REPO_ENABLED，默认开）；关闭后开机、手动刷新、安装前补缓存三条路径都不再拉官方源，管理页显示「云端仓库已关闭」。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.64</p>
                <p className="text-zinc-400">整体删除主页与硬件信息检测（温度/风扇/内存/显存/存储/FPS/当前游戏），下线 /system_stats、/game_icon、/sysinfo_probe 与 READ_HARDWARE_INFO 开关，启动直接进「管理 Payload」页；顺带修好设置页开关必须带 AUTOLOAD_ENABLED 才保存的问题。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.63</p>
                <p className="text-zinc-400">死机止血两步：默认停用 DCE 换帧计数 ioctl；新增安全模式哨兵文件 /data/nayeplmtools/safe_mode，建此空文件后启动跳过全部高危动作。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.62</p>
                <p className="text-zinc-400">/system_stats 快照改在互斥锁内构建并缓存 5s，杜绝传感器并发死机；日志落盘到 /data/nayeplmtools/pldmgr.log 并记录启动开关值。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.61</p>
                <p className="text-zinc-400">修 autoload.txt 开头多出一个空行：改以「延时行 + 文件名行」整条为单位规整。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.60</p>
                <p className="text-zinc-400">autoload.txt 格式一致性：缺延时行自动补 !1000、保存时保留自定义延时、行规整统一走 strip_config_line。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.59</p>
                <p className="text-zinc-400">autoloader 残包自愈：新增 payload_file_sane 体积/魔数校验、copy_file 改为写临时文件校验后 rename、同版本坏文件重新复制。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.58</p>
                <p className="text-zinc-400">修「常用入口」卡片删除按钮与外链图标重叠；dev 补全 /quicklinks* 等代理。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.57</p>
                <p className="text-zinc-400">仓库固化，功能同 .56：重建丢失的 .55/.56 源码、内置 pkg-manager.elf 入库、settings 词条迁回 locales-overlay。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.56</p>
                <p className="text-zinc-400">「PKG安装」视图铺满整页；设置页 PKG Manager 卡片新增「关闭」按钮，后端新增 /pkgmanager_stop。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.55</p>
                <p className="text-zinc-400">硬件信息改走官方 libkernel API（型号/序列号/系统版本），inst0.dat 等降为兜底；「支持作者」页新增「参考与致谢」。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.54</p>
                <p className="text-zinc-400">⚠ 回滚 .53 的进程内提权（12.20 HEN 上导致整机死机），默认永久禁用；保留 .52 的系统版本修复，型号/序列号继续显 —。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.53</p>
                <p className="text-zinc-400 line-through">型号/序列号攻坚：首次读 inst0.dat 前用 SDK 自带内核原语把自身进程提权为 root 再探测——⚠ 导致实机死机，已在 .54 禁用</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.52</p>
                <p className="text-zinc-400">实机 probe 定位后修正：系统版本按 BCD 移位组合解码为 12.20；型号白名单收紧宁显 —；inst0.dat 新增 4 条候选路径探测。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.51</p>
                <p className="text-zinc-400">移除仪表盘“当前用户”格（.50 加的，直连 7788 头像服务 whoami），头部信息区恢复四格：型号/序列号/系统版本/开机时间</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.50（已撤销）</p>
                <p className="text-zinc-400">仪表盘新增“当前用户”（直连头像服务 :7788 whoami，30s 轮询），头部信息区改为五格。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.49（版本解码仍有误，见 .52）</p>
                <p className="text-zinc-400">型号/序列号改从 inst0.dat 读取，系统版本按 BCD 解为 12.20；/sysinfo_probe 增加 inst0.dat 可读性输出。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.48（版本解码已被 .49 修正）</p>
                <p className="text-zinc-400">型号接真数据（hw.model 补空格形态）、系统版本改用真实编码、序列号确认 userland 无源继续显 —。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.47</p>
                <p className="text-zinc-400">修 .46 系统信息三项错乱：探测结果须过严格白名单才上界面，可疑值回退 kern.osrelease。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.46（已被 .47 修正）</p>
                <p className="text-zinc-400">系统信息补齐：新增系统版本（kernel_get_fw_version）、型号三路探测、/sysinfo_probe 诊断端点。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.45</p>
                <p className="text-zinc-400">「PKG安装」内嵌界面铺满右侧：去留白、取消顶部 URL 栏，「新标签页打开」缩为悬浮小按钮。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.44</p>
                <p className="text-zinc-400">「PKG安装」改为内嵌运行：点菜单在右侧内嵌 PKG Manager 界面（iframe :8844），未运行自动拉起。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.43</p>
                <p className="text-zinc-400">「PKG安装」菜单一步直达：未运行自动拉起后直接跳转其自带界面，上传/启停/版本迁到设置页新卡片。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.42</p>
                <p className="text-zinc-400">「常用入口」页上线：手动添加插件/局域网服务快捷卡片，逐条 TCP 探活，点卡片新标签打开；启动通知加版本号。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.41</p>
                <p className="text-zinc-400">内置 PKG Manager 基线升级到 v1.4.0-CN；上传校验改用 ELF 头 + 体积 + "PKG Manager" 字符串判身份。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.40</p>
                <p className="text-zinc-400">PKG Manager 内置上线：「PKG安装」页可直接启动内置 PKG Manager（端口 8844）并支持上传新版 ELF 更新。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.39</p>
                <p className="text-zinc-400">PS4 兼容游戏图标修复（实机反馈）：新增 /user/appmeta/「TID」/icon0.png 候选；app.db iconPath 带 ?ts= 时间戳参数时先剥除再读文件</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.38</p>
                <p className="text-zinc-400">游戏图标候选扩充：新增 app.db iconPath 与 appmeta 缓存两级候选，全落空时按 tid 记 miss 诊断。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.37</p>
                <p className="text-zinc-400">首页「当前游戏」图标上线：新增 /game_icon 端点按 sce_sys/icon0.png 顺序读取，读不到回退占位图。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.36</p>
                <p className="text-zinc-400">去掉设置页「读取硬件信息」行的脉冲图标（仅去图标，开关与说明不变）</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.35</p>
                <p className="text-zinc-400">修首页数据卡住不刷新：轮询改 setInterval + 1.8s 超时中断；开机时间带秒。</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.34</p>
                <p className="text-zinc-400">首页仪表盘与侧栏菜单的标签/正文/卡片标题等小字整体放大 2px（FPS、温度、风扇% 等大号展示数字保持不变）</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.33</p>
                <p className="text-zinc-400">修复首页放着不自动更新：轮询 /system_stats 加时间戳 + no-store，后端该路由回 Cache-Control: no-store，避免 PS5 浏览器缓存 GET 导致数值冻住</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.32</p>
                <p className="text-zinc-400">存储按参考项目取数对齐：内置 SSD 读 /user（games+apps 分区）、内置 M.2 扩展盘用 getmntinfo 动态探测（同文件系统类型、容量最大、60s 重试），不再用错误的 /data、/mnt/ext 路径</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.31</p>
                <p className="text-zinc-400">补上参考项目正常读取、之前遗漏的 SSD/硬盘温度（SoC sensor 1）；温度读取失败统一返回 -1、前端不再错显示 0°C</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.30</p>
                <p className="text-zinc-400">首页接入真实 FPS（DCE 换帧计数 / OnionHEN）与当前运行游戏名（前台大应用 appinfo + app.db/param.sfo），均受“读取硬件信息”开关门控</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.29</p>
                <p className="text-zinc-400">首页仪表盘接入硬件信息（CPU/GPU 温度、风扇、内存/显存、存储、开机时间）；设置新增“读取硬件信息”开关（默认开，关闭时显示 —）</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.27</p>
                <p className="text-zinc-400">移除背景图尝试，恢复轻量状态</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.23–.26</p>
                <p className="text-zinc-400">autoload.txt 写入格式修正（!延时 + 文件名）；有则更新无则追加，不影响其他程序</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.22</p>
                <p className="text-zinc-400">启动自检支持 ps5_autoloader 空文件时主动登记</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.20–.21</p>
                <p className="text-zinc-400">新增 ps5_autoloader 同步机制：安装 payload 后自动更新引导器目录；启动自检 bootstrap 解决旧版本无同步代码问题</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.18–.19</p>
                <p className="text-zinc-400">浅色主题修复：新增 text-ps-text 语义变量 + 补全 text-white/90 透明度变体映射</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.11–.17</p>
                <p className="text-zinc-400">UI 主题统一改造、分类组头、QR 页脚地址主题适配、侧栏导航优化</p>
              </div>
            </div>
          </div>
        </div>
      )}
    </div>
  )
}

export default SettingsView
