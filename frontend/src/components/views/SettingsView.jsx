import React, { useState } from 'react'
import { Terminal, ChevronRight, Globe, Languages, Palette, Heart, ScrollText, CloudDownload, Power } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { cn } from '../../utils/helpers'
// [CN-FORK] 外观（深/浅色）切换控件
import ThemeToggle from '../../theme/ThemeToggle'
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'
import Switch from '../ui/Switch'
import Segmented from '../ui/Segmented' /* [CN-FORK] v0.5.2.97 互斥档位统一 */
import Modal from '../ui/Modal'

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

/* [CN-FORK] v0.5.2.94 「点整行去别处」的四份副本（管理来源 / 日志 / 更新日志 / 支持作者）收成一个：
 * 整行就是按钮，命中区从图标那条 24px 变成整行，遥控器移到行上直接确认即可。 */
const NavRow = ({ icon: Icon, title, description, onClick, danger }) => (
  <button
    onClick={onClick}
    className="row-link group w-full text-left flex items-center p-4 md:p-5 bg-white/[0.03] rounded-xl border border-white/10 hover:border-ps-blue/30 transition-colors"
  >
    <div className="p-3 shrink-0 -ml-3">
      <Icon className={cn("w-5 h-5", danger ? "text-zinc-500 group-hover:text-red-500" : "text-ps-blue")} />
    </div>
    <div className="min-w-0 flex-1 ml-3">
      <p className="font-bold text-white text-base tracking-tight leading-tight">{title}</p>
      {description && <p className="text-sm text-zinc-500 leading-relaxed mt-1">{description}</p>}
    </div>
    <ChevronRight className={cn("w-6 h-6 shrink-0 ml-3 text-zinc-700 group-hover:translate-x-1 transition-transform",
      danger ? "group-hover:text-red-500" : "group-hover:text-ps-blue")} />
  </button>
)

/* [CN-FORK] v0.5.2.94 更新日志从写死在 JSX 里的 45 段副本改成数据数组 + 共用 <Modal>。
 * 原来的自制浮层不认 Esc/返回键、打开后焦点还留在弹窗背后，且 bg-[#1a1a1e] 是脱离配色契约的第 6 种底。 */
const CHANGELOG = [
  { v: "v0.5.2.67", t: "合并上游 v0.5.2；修复主屏启动器图标不出现（icon0.png 重生成为 1024×1024 RGBA）；默认开启「多个来源」、默认关闭内置官方云端仓库，并首次开机预置启用 repo.630810.xyz。" },
  { v: "v0.5.1.66", t: "精简界面文案：「更新日志」各条压缩为一行摘要，「参考与致谢」去掉每条借鉴说明、只保留作者名与地址。" },
  { v: "v0.5.1.65", t: "新增「默认云端仓库」总开关（DEFAULT_REPO_ENABLED，默认开）；关闭后开机、手动刷新、安装前补缓存三条路径都不再拉官方源，管理页显示「云端仓库已关闭」。" },
  { v: "v0.5.1.64", t: "整体删除主页与硬件信息检测（温度/风扇/内存/显存/存储/FPS/当前游戏），下线 /system_stats、/game_icon、/sysinfo_probe 与 READ_HARDWARE_INFO 开关，启动直接进「管理 Payload」页；顺带修好设置页开关必须带 AUTOLOAD_ENABLED 才保存的问题。" },
  { v: "v0.5.1.63", t: "死机止血两步：默认停用 DCE 换帧计数 ioctl；新增安全模式哨兵文件 /data/nayeplmtools/safe_mode，建此空文件后启动跳过全部高危动作。" },
  { v: "v0.5.1.62", t: "/system_stats 快照改在互斥锁内构建并缓存 5s，杜绝传感器并发死机；日志落盘到 /data/nayeplmtools/pldmgr.log 并记录启动开关值。" },
  { v: "v0.5.1.61", t: "修 autoload.txt 开头多出一个空行：改以「延时行 + 文件名行」整条为单位规整。" },
  { v: "v0.5.1.60", t: "autoload.txt 格式一致性：缺延时行自动补 !1000、保存时保留自定义延时、行规整统一走 strip_config_line。" },
  { v: "v0.5.1.59", t: "autoloader 残包自愈：新增 payload_file_sane 体积/魔数校验、copy_file 改为写临时文件校验后 rename、同版本坏文件重新复制。" },
  { v: "v0.5.1.58", t: "修「常用入口」卡片删除按钮与外链图标重叠；dev 补全 /quicklinks* 等代理。" },
  { v: "v0.5.1.57", t: "仓库固化，功能同 .56：重建丢失的 .55/.56 源码、内置 pkg-manager.elf 入库、settings 词条迁回 locales-overlay。" },
  { v: "v0.5.1.56", t: "「PKG安装」视图铺满整页；设置页 PKG Manager 卡片新增「关闭」按钮，后端新增 /pkgmanager_stop。" },
  { v: "v0.5.1.55", t: "硬件信息改走官方 libkernel API（型号/序列号/系统版本），inst0.dat 等降为兜底；「支持作者」页新增「参考与致谢」。" },
  { v: "v0.5.1.54", t: "⚠ 回滚 .53 的进程内提权（12.20 HEN 上导致整机死机），默认永久禁用；保留 .52 的系统版本修复，型号/序列号继续显 —。" },
  { v: "v0.5.1.53", t: "型号/序列号攻坚：首次读 inst0.dat 前用 SDK 自带内核原语把自身进程提权为 root 再探测——⚠ 导致实机死机，已在 .54 禁用", strike: true },
  { v: "v0.5.1.52", t: "实机 probe 定位后修正：系统版本按 BCD 移位组合解码为 12.20；型号白名单收紧宁显 —；inst0.dat 新增 4 条候选路径探测。" },
  { v: "v0.5.1.51", t: "移除仪表盘“当前用户”格（.50 加的，直连 7788 头像服务 whoami），头部信息区恢复四格：型号/序列号/系统版本/开机时间" },
  { v: "v0.5.1.50（已撤销）", t: "仪表盘新增“当前用户”（直连头像服务 :7788 whoami，30s 轮询），头部信息区改为五格。" },
  { v: "v0.5.1.49（版本解码仍有误，见 .52）", t: "型号/序列号改从 inst0.dat 读取，系统版本按 BCD 解为 12.20；/sysinfo_probe 增加 inst0.dat 可读性输出。" },
  { v: "v0.5.1.48（版本解码已被 .49 修正）", t: "型号接真数据（hw.model 补空格形态）、系统版本改用真实编码、序列号确认 userland 无源继续显 —。" },
  { v: "v0.5.1.47", t: "修 .46 系统信息三项错乱：探测结果须过严格白名单才上界面，可疑值回退 kern.osrelease。" },
  { v: "v0.5.1.46（已被 .47 修正）", t: "系统信息补齐：新增系统版本（kernel_get_fw_version）、型号三路探测、/sysinfo_probe 诊断端点。" },
  { v: "v0.5.1.45", t: "「PKG安装」内嵌界面铺满右侧：去留白、取消顶部 URL 栏，「新标签页打开」缩为悬浮小按钮。" },
  { v: "v0.5.1.44", t: "「PKG安装」改为内嵌运行：点菜单在右侧内嵌 PKG Manager 界面（iframe :8844），未运行自动拉起。" },
  { v: "v0.5.1.43", t: "「PKG安装」菜单一步直达：未运行自动拉起后直接跳转其自带界面，上传/启停/版本迁到设置页新卡片。" },
  { v: "v0.5.1.42", t: "「常用入口」页上线：手动添加插件/局域网服务快捷卡片，逐条 TCP 探活，点卡片新标签打开；启动通知加版本号。" },
  { v: "v0.5.1.41", t: "内置 PKG Manager 基线升级到 v1.4.0-CN；上传校验改用 ELF 头 + 体积 + \"PKG Manager\" 字符串判身份。" },
  { v: "v0.5.1.40", t: "PKG Manager 内置上线：「PKG安装」页可直接启动内置 PKG Manager（端口 8844）并支持上传新版 ELF 更新。" },
  { v: "v0.5.1.39", t: "PS4 兼容游戏图标修复（实机反馈）：新增 /user/appmeta/「TID」/icon0.png 候选；app.db iconPath 带 ?ts= 时间戳参数时先剥除再读文件" },
  { v: "v0.5.1.38", t: "游戏图标候选扩充：新增 app.db iconPath 与 appmeta 缓存两级候选，全落空时按 tid 记 miss 诊断。" },
  { v: "v0.5.1.37", t: "首页「当前游戏」图标上线：新增 /game_icon 端点按 sce_sys/icon0.png 顺序读取，读不到回退占位图。" },
  { v: "v0.5.1.36", t: "去掉设置页「读取硬件信息」行的脉冲图标（仅去图标，开关与说明不变）" },
  { v: "v0.5.1.35", t: "修首页数据卡住不刷新：轮询改 setInterval + 1.8s 超时中断；开机时间带秒。" },
  { v: "v0.5.1.34", t: "首页仪表盘与侧栏菜单的标签/正文/卡片标题等小字整体放大 2px（FPS、温度、风扇% 等大号展示数字保持不变）" },
  { v: "v0.5.1.33", t: "修复首页放着不自动更新：轮询 /system_stats 加时间戳 + no-store，后端该路由回 Cache-Control: no-store，避免 PS5 浏览器缓存 GET 导致数值冻住" },
  { v: "v0.5.1.32", t: "存储按参考项目取数对齐：内置 SSD 读 /user（games+apps 分区）、内置 M.2 扩展盘用 getmntinfo 动态探测（同文件系统类型、容量最大、60s 重试），不再用错误的 /data、/mnt/ext 路径" },
  { v: "v0.5.1.31", t: "补上参考项目正常读取、之前遗漏的 SSD/硬盘温度（SoC sensor 1）；温度读取失败统一返回 -1、前端不再错显示 0°C" },
  { v: "v0.5.1.30", t: "首页接入真实 FPS（DCE 换帧计数 / OnionHEN）与当前运行游戏名（前台大应用 appinfo + app.db/param.sfo），均受“读取硬件信息”开关门控" },
  { v: "v0.5.1.29", t: "首页仪表盘接入硬件信息（CPU/GPU 温度、风扇、内存/显存、存储、开机时间）；设置新增“读取硬件信息”开关（默认开，关闭时显示 —）" },
  { v: "v0.5.1.27", t: "移除背景图尝试，恢复轻量状态" },
  { v: "v0.5.1.23–.26", t: "autoload.txt 写入格式修正（!延时 + 文件名）；有则更新无则追加，不影响其他程序" },
  { v: "v0.5.1.22", t: "启动自检支持 ps5_autoloader 空文件时主动登记" },
  { v: "v0.5.1.20–.21", t: "新增 ps5_autoloader 同步机制：安装 payload 后自动更新引导器目录；启动自检 bootstrap 解决旧版本无同步代码问题" },
  { v: "v0.5.1.18–.19", t: "浅色主题修复：新增 text-ps-text 语义变量 + 补全 text-white/90 透明度变体映射" },
  { v: "v0.5.1.11–.17", t: "UI 主题统一改造、分类组头、QR 页脚地址主题适配、侧栏导航优化" },
]

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
        <NavRow
          icon={ScrollText}
          title={t("settings.changelog_title", "Changelog")}
          description={t("settings.changelog_desc", "View version update history for this fork.")}
          onClick={() => setShowChangelog(true)}
        />
      </Section>

      {/* [CN-FORK] 支持作者：从移动端底部导航迁移至此，符合中文应用惯例。
          说明沿用 settings.donate_desc（overlay 里已带 fork 归属与版本号），不另起一行重复。 */}
      <Section id="donate" icon={Heart} title={t("settings.donate_title", "Support the Project")}>
        <NavRow
          icon={Heart}
          title={t("app.nav.donate", "Donate")}
          description={t("settings.donate_desc", "If you like this project, you can buy the author a coffee.")}
          onClick={() => onNavigate('donate')}
          danger
        />
      </Section>

      {/* [CN-FORK] 更新日志：走共用 Modal（Esc/返回键可关、打开即聚焦、内容长时可滚） */}
      <Modal
        show={showChangelog}
        title={t("settings.changelog_title", "Changelog")}
        onClose={() => setShowChangelog(false)}
      >
        <div className="space-y-4 text-sm leading-relaxed">
          {CHANGELOG.map((c) => (
            <div key={c.v} className="space-y-1">
              <p className="font-bold text-ps-blue">{c.v}</p>
              <p className={cn(c.strike ? "text-zinc-500 line-through" : "text-zinc-400")}>{c.t}</p>
            </div>
          ))}
        </div>
      </Modal>
    </div>
  )
}

export default SettingsView
