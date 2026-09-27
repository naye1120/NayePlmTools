import React, { useState } from 'react'
import { Terminal, ChevronRight, Globe, Languages, Palette, Heart, ScrollText, X, Package } from 'lucide-react'
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
  const multiSources = config.MULTI_SOURCES_ENABLED === true
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
                <p className="font-bold text-ps-blue">v0.5.1.64</p>
                <p className="text-zinc-400">主页与硬件信息检测整体删除：温度/风扇/内存/显存/存储/FPS/当前游戏 全部仍靠每 2s 一次的 /system_stats 轮询驱动，是不可重入的 Sony 传感器 API、页表统计与显示驱动 ioctl 拼起来的，也是目前唯一能解释「一运行就死机」的功能；既然它只为了首页那几个数字，本版直接抹掉：删 src/stats.c 与 include/stats.h（不再进构建）、/system_stats 与 /game_icon 与 /sysinfo_probe 三个端点下线、READ_HARDWARE_INFO 开关（配置项/设置页/后端 JSON）一并移除；前端删 DashboardView 与侧栏、移动端底部、默认页三处「主页」入口，启动直接进「管理 Payload」页（旧配置里残留的 READ_HARDWARE_INFO= 行会被忽略，不影响加载）。dev 侧同步删 mock 的 /system_stats 与 /game_icon、vite 代理两项。保留 .63 的 safe_mode 哨兵文件（现在只管装 app / 杀光盘 / 开浏览器 / 自启序列四件事），死机取证靠 /data/nayeplmtools/pldmgr.log。另加固设置页开关：后端 /set_config 旧实现要求请求体里必须带 AUTOLOAD_ENABLED 才会保存任何开关，缺这个键就整份静默丢弃（只要 /get_config 有一次没读到值就会踩到，症状是开关点了没反应），现在各开关无条件应用，只有「关掉自启」的 abort 动作仍拿 AUTOLOAD_ENABLED 做判据</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.63</p>
                <p className="text-zinc-400">死机止血两步：① FPS 的「/dev/dce 显示控制器换帧计数器」默认停用——那是全程序唯一直接对显示/GPU 驱动发 ioctl 的地方（拿一个伪造的 selector/mask 让内核往我们的缓冲区里写硬件计数器），却跟着首页轮询一直跑，换来的只是一个 FPS 数字，性价比太差；OnionHEN 写在 /system_tmp/fps_sample 的 seqlock 文件源保留，想重新实验那个 ioctl 就编译时加 -DPLDMGR_ENABLE_DCE_FPS=1；② 新增安全模式哨兵文件 /data/nayeplmtools/safe_mode：用 FTP 建这个空文件（内容无所谓），下次启动就跳过引导期全部高危动作——不装启动器 app、不杀光盘播放器、不自动开浏览器、不跑自动加载序列，同时 /system_stats 一个内核调用也不发（首页硬件卡片全显 —）。不用重编译、不用进界面就能把机器带回可用状态；日志页与 /data/nayeplmtools/pldmgr.log 会记一条 SAFE MODE on 作为确认</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.62</p>
                <p className="text-zinc-400">死机排查与传感器安全闸：首页每 2s 一次拉 /system_stats，而后端一直是裸调——CPU 温度、SoC sensor 0/1、风扇占空比、内存/显存页表、DCE 帧计数 ioctl、全进程表 sysctl 全部每请求跑一遍，既没锁也没节流；加上前端 1.8s 就 abort（只断客户端，服务端 handler 线程照旧跑完）与 MHD 的 thread-per-connection，多个线程会并发把传感器请求打进同一条 ICC 通道——这正是本项目「Sensor API 必须串行化 + 限流」规范要防的死机形态。现在整份快照在互斥锁内构建并缓存 5s，并发请求直接命中缓存不再产生内核调用（前端无需改，节流后 abort 重发已无危害）。同时日志落盘到 /data/nayeplmtools/pldmgr.log（带时间戳、每行 fflush、超 512KB 截断），此前日志只在内存环形缓冲里，一断电死机现场就全没；启动时还会把 hw_stats / auto_browser / kill_disc / auto_install / autoload 几个默认开启的开关值记进日志，下次死机可直接看最后几行指认现场</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.61</p>
                <p className="text-zinc-400">修 autoload.txt 开头多出一个空行：上一版补延时行的逻辑向上找参照行时是「跳过空行继续往上」，于是「!1000」被插到空行下面，空行留在原地——本工具那条正好排在文件第一位，看着就是文件前面多了一行。现在改成以「!延时行 + 文件名行」整条为单位：先折叠这条上方紧邻的连续空行，再判断要不要补延时行；空行只清理本工具块自己上方的，其它程序的条目一律不动，且只清理空行这一项变化也会正常触发回写（旧逻辑没改动就原样跳过）</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.60</p>
                <p className="text-zinc-400">autoload.txt 格式一致性：① 载荷文件名行上面缺「!延时」时自动补「!1000」（旧逻辑只在首次追加时写延时行，换版本号时只替文件名）；追加前先探末尾，上一行没换行符就补一个，避免延时行粘到上一条尾巴上两条一起坏；② 设置页保存 AUTOLOAD_LIST 时整份覆盖只写文件名，等于每勾一次就把手写的延时行全冲掉——现在先读旧文件建「文件名→延时行」映射再写回，查不到历史的用 !1000 兜底，自定义延时不会被抹平；③ 行规整抽成 utils 的 pldmgr_utils_strip_config_line（剥 UTF-8 BOM 与首尾空白），自启同步、启动自检、自身加载序列、改名与删除条目四处统一走它——记事本手改的缩进会让延时行被当成载荷名、带缩进的文件名拼不出路径而报 Payload not found。另确认上一版 0 字节残包的真凶在复制环节（storage 里 4.9MB 完好），加载一次新版即可由同名自愈补回</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.59</p>
                <p className="text-zinc-400">autoloader 残包自愈：修实机「autoload.txt 指向的 NayePlmTools ELF 是 0 字节、重启后自启什么都不起来」。三处：① 新增 payload_file_sane 判据（体积下限 1KB + .elf 魔数，goldHEN 这类 .bin 原始载荷不误杀），上传/仓库安装提交前先校验，0 字节或被截断的 temp 直接拒收并清掉，不再 rename 进 storage；② copy_file 改成先写 dst.tmp、校验字节数与源一致后 rename 覆盖，旧实现一上来就把目标截断成 0 字节，中途失败就在自启目录留个残包，更严的是源与目标同一个文件时会把好文件清空还返回成功；③ 同版本已登记时旧逻辑直接返回，引导器目录里那份坏文件永远没人修，现在会检测到并重新复制；bootstrap 自检也不再拿 readdir 的随机首个，改取合法里最新的一份</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.58</p>
                <p className="text-zinc-400">前端：修「常用入口」卡片右上角删除按钮与外链图标叠在一起的问题——两者本来都占右上角（按钮 absolute top-2 right-2，外链图标 ml-auto 顶到行末），实机看起来就是链接和关闭按钮重合；改为给标题行留 pr-8 把那个角让给删除按钮（实测间隙恒 12px，420px 窄卡与超长名称下同样不重叠），并给名称补 min-w-0 让 truncate 在 flex 子里真正生效；本地 dev 补全 /quicklinks*、/pkgmanager_upload、/game_icon 代理（缺 /quicklinks 时该页被 SPA fallback 吞成 index.html，res.json() 抛错又被 catch 静默吃掉，永远卡在转圈且看不出原因）</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.57</p>
                <p className="text-zinc-400">仓库固化，功能与 .56 完全一致：重建因 IDE 变更面板误点拒绝而丢失的 .55/.56 源码并验收（产物字节数与动态符号表与丢失前一致）；third_party/pkg-manager.elf 内置基线纳入版本库（clone 后可直接 make all）；settings.read_hw_* 词条从翻译子模块迁回 locales-overlay；全部改动按「构建通过即本地提交」固化到 cn-fork 分支。本版构建标题带真实 commit 短哈希，作为上机测试的正式包</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.56</p>
                <p className="text-zinc-400">前端：「PKG安装」视图铺满整个模块页（四边内边距清零、解除 1800px 居中宽度上限、由 iframe 自己接管滚动；手机端不再塌成 150px 高）；设置页 PKG Manager 卡片新增「关闭」按钮，后端新增 /pkgmanager_stop 按进程名 SIGKILL pkg-manager.elf（只影响它自己；端口在响但找不到进程时报错而不误杀）；本地 dev 补全 pkgmanager_* 代理</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.55</p>
                <p className="text-zinc-400">硬件信息改走官方 libkernel API（出处 ps5-payload-sdk samples/hwinfo，作者 John Törnblom）：型号 sceKernelGetHwModelName()、序列号 sceKernelGetHwSerialNumber()、系统版本字符串 sceKernelGetGenuineProsperoSystemSwVersion() 插到探测链最前，均声明为 weak + 运行时判空；inst0.dat / kenv / sysctl / RegMgr 降为兜底；官方值走新的宽松校验（避免误杀 "PlayStation 5"），会塞垃圾的源仍走严格白名单；/sysinfo_probe 逐项输出 sym/rc/val 与 SoC ID，一次实机就能定位；「支持作者」页新增「参考与致谢」，README Credits 同步补 4 组出处</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.54</p>
                <p className="text-zinc-400">⚠ 回滚 .53 的进程内提权：实机验证该方案（SDK sysdebug RPC 族 kernel_set_ucred_*/rootvnode）在 12.20 HEN 上会导致整机死机，已默认永久禁用（代码存档、零内核调用）；.52 的系统版本修复（→12.20）保留，型号/序列号继续显 —</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.53</p>
                <p className="text-zinc-400 line-through">型号/序列号攻坚：首次读 inst0.dat 前用 SDK 自带内核原语把自身进程提权为 root 再探测——⚠ 导致实机死机，已在 .54 禁用</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.52</p>
                <p className="text-zinc-400">实机 probe 定位后修正：系统版本最终确定编码为 (major BCD&lt;&lt;24)|(minor BCD&lt;&lt;16)|build（0x12200005→12.20，与官方页一致，低 16 位含义未定不上界面）；型号白名单取消对 hw.model 内部标识（100-xxxxxxx）的放行，宁显 — 不显示错误型号；/root/data/inst0.dat 实机无读权限，新增 4 条候选路径探测，/sysinfo_probe 逐路径输出 open/errno 定位可读位置</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.51</p>
                <p className="text-zinc-400">移除仪表盘“当前用户”格（.50 加的，直连 7788 头像服务 whoami），头部信息区恢复四格：型号/序列号/系统版本/开机时间</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.50（已撤销）</p>
                <p className="text-zinc-400">仪表盘新增“当前用户”：直连头像服务（端口 7788）/api/avatar/whoami 显示登录用户名（30s 轮询，CORS 全开可跨域），头像服务未启动时静默显示 —；头部信息区改为五格</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.49（版本解码仍有误，见 .52）</p>
                <p className="text-zinc-400">系统信息与官方“主机信息”页对齐：型号/序列号改从 /root/data/inst0.dat 读取（官方页同源，offset 0x14/0x40，读不到时回退旧探测）；系统版本修正解码——0x12200005 按 BCD 字节解为 12.20（与官方页一致，.46 的 34.00/.48 的 1.34 都是解错），build 非零时追加第三段；/sysinfo_probe 增加 inst0.dat 可读性与字段裸内容输出</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.48（版本解码已被 .49 修正）</p>
                <p className="text-zinc-400">根据实机 /sysinfo_probe 输出修正系统信息探测：型号接上真数据（PS5 的 hw.model 实际返回空格补齐的 "100-xxxxxxx" 形态，旧白名单误杀）；系统版本改用真实编码 (major&lt;&lt;24)|(minor&lt;&lt;16)|build（0x12200005→1.34，与 websrv 的 api_version 常量吻合），不再按 SDK 文档的 BCD 格式误解；序列号确认 userland 无数据源（相关 sysctl 全部不存在），继续显示 —</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.47</p>
                <p className="text-zinc-400">修正 .46 系统信息三项全部错乱的问题：kenv/sysctl 对不存在的变量会返回垃圾值，现在所有探测结果必须过严格格式白名单（型号=CUH/CFI 格式、序列号=12~17 位大写字母数字、版本=合理区间）才上界面；kernel_get_fw_version 读死的 dynlib handle 在我们进程里不对应 libc_internal，可疑值一律丢弃并回退 kern.osrelease；/sysinfo_probe 增加各数据源裸值输出便于定位</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.46（已被 .47 修正）</p>
                <p className="text-zinc-400">系统信息补齐：仪表盘新增系统版本（改用 SDK 的 kernel_get_fw_version，纯 userland 读 libc_internal，不受版本仿冒影响）；主机型号改为 kenv→sysctl→RegMgr 三路探测；新增 /sysinfo_probe 诊断端点（dump 内核环境变量表，用于实机定位型号/序列号数据源）；序列号仍无可靠 userland 来源，探测命中前仍显示 —</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.45</p>
                <p className="text-zinc-400">「PKG安装」内嵌界面铺满右侧：去掉内容区左右/底部留白，iframe 占满整个右侧区域；顶部 URL 栏取消，「新标签页打开」缩为右下角悬浮小按钮</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.44</p>
                <p className="text-zinc-400">「PKG安装」改为内嵌运行：点击菜单不再跳转离开，而是在右侧内容区内嵌 PKG Manager 自带界面（iframe 指向 :8844，侧栏保留），未运行时自动拉起并等待就绪后加载；右上角提供「新标签页打开」作全屏备选</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.43</p>
                <p className="text-zinc-400">「PKG安装」菜单改为一步直达：点击后若 PKG Manager 未运行则自动拉起并等待就绪，随后直接跳转到它自带的界面（:8844），不再经过中间管理页；原页面上的上传更新/启动重启/版本状态迁移到设置页新增的「PKG Manager」管理卡片</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.42</p>
                <p className="text-zinc-400">「常用入口」页上线：可手动添加常用插件/局域网服务的快捷访问卡片（名称+URL，存主机端 quicklinks.json，多设备共享同一份），逐条 TCP 探活显示在线状态，点卡片新标签打开；启动通知加上了版本号</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.41</p>
                <p className="text-zinc-400">内置 PKG Manager 基线升级到 v1.4.0-CN；上传校验策略修正：发现 PKGManager 发布的 ELF 里 PKGMGR_VER 签名串被其自身 --gc-sections 链接回收、实际不存在，改为 ELF 头 + 体积 + 二进制含 "PKG Manager" 字符串判身份，版本号按签名→上传文件名→unknown 顺序提取</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.40</p>
                <p className="text-zinc-400">PKG Manager 内置上线：「PKG安装」页不再是占位页，可直接启动内置的 PKG Manager（独立守护进程，UI 端口 8844）并跳转其自带界面；支持手动上传新版 ELF 更新（ELF 头 + PKGMGR_VER 签名校验、原子覆盖），内置基线仅作离线保底</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.39</p>
                <p className="text-zinc-400">PS4 兼容游戏图标修复（实机反馈）：新增 /user/appmeta/「TID」/icon0.png 候选；app.db iconPath 带 ?ts= 时间戳参数时先剥除再读文件</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.38</p>
                <p className="text-zinc-400">游戏图标候选扩充：新增 app.db iconPath 列（系统真正显示的图标路径，含 PS4 兼容游戏）与 appmeta 缓存目录两级候选；全部落空时每个 tid 在日志页记一行 miss 诊断便于定位</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.37</p>
                <p className="text-zinc-400">首页「当前游戏」图标上线：新增 /game_icon 端点，按 /user/app/「TID」/sce_sys/icon0.png → 沙盒 app0/sce_sys/icon0.png 顺序读取（psdevwiki 确认只有 PNG、无 gif），受同一硬件信息开关门控，读不到时回退占位图</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.36</p>
                <p className="text-zinc-400">去掉设置页「读取硬件信息」行的脉冲图标（仅去图标，开关与说明不变）</p>
              </div>
              <div className="space-y-1">
                <p className="font-bold text-ps-blue">v0.5.1.35</p>
                <p className="text-zinc-400">修复首页数据卡住不自动刷新：轮询改用 setInterval（旧链式 setTimeout 只要一次请求挂起就永久死链），加 1.8s 超时中断、防堆积、回到前台立即补拉；开机时间改为带秒（如 8h 39m 12s）</p>
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
