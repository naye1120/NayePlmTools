import React from 'react'
import { Heart, ScrollText, Info, BookMarked } from 'lucide-react'
import { QRCodeSVG } from 'qrcode.react'
import { useTranslation } from 'react-i18next'
import { isPS5 } from '../../utils/helpers'
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'

/* [CN-FORK] v0.5.2.154 「关于」页 —— 设置页下面的新菜单。
 *
 * 用户口径：「在设置下边增加一个菜单叫关于，把更新日志和支持作者的内容全部放到关于里面」。
 * 于是原来分散在两处的两块内容搬到这里合并：
 *   · 设置页 → 诊断 → 「更新日志」（`CHANGELOG` 数组 + 一个 Modal）
 *   · 设置页 → 「支持作者」（一个跳去 `view='donate'` 的入口）
 * 并把原来独立的「支持作者」页（`DonateView`）整个吸收进来 —— 那个页面的内容本来就是
 * 三块：捐赠卡片 / 关于本 Fork / 参考与致谢，正好都是「关于」该讲的事。
 *
 * ⚠ 「参考与致谢」必须与「关于本 Fork」**同页**（原 DonateView 里的约定）：用户要求所有
 * 引用的开源作者与地址都标识到与原作者署名相同的页面。搬到本页时两块仍然挨着，别拆开。
 *
 * 页面容器按 v0.5.2.99 的约定：根节点 `space-y-6`、有且只有一个 `<PageHead>`、不限宽不居中。 */
const REFERENCES = [
  {
    author: 'John Törnblom', authorUrl: 'https://github.com/john-tornblom',
    project: 'ps5-payload-sdk', projectUrl: 'https://github.com/ps5-payload-dev/sdk',
    where: 'samples/hwinfo/main.c',
    whereUrl: 'https://github.com/ps5-payload-dev/sdk/tree/master/samples/hwinfo',
  },
  {
    author: 'John Törnblom', authorUrl: 'https://github.com/john-tornblom',
    project: 'ps5-payload-sdk', projectUrl: 'https://github.com/ps5-payload-dev/sdk',
    where: 'samples/test_privileges/main.c',
    whereUrl: 'https://github.com/ps5-payload-dev/sdk/tree/master/samples/test_privileges',
  },
  {
    author: 'John Törnblom', authorUrl: 'https://github.com/john-tornblom',
    project: 'ps5-payload-sdk', projectUrl: 'https://github.com/ps5-payload-dev/sdk',
    where: 'sce_stubs/libkernel_sys.c',
    whereUrl: 'https://github.com/ps5-payload-dev/sdk/blob/master/sce_stubs/libkernel_sys.c',
  },
  {
    author: 'manos555555', authorUrl: 'https://github.com/manos555555',
    project: 'PS5-Upload-Suite', projectUrl: 'https://github.com/manos555555/PS5-Upload-Suite',
    where: 'payload/main.c',
    whereUrl: 'https://github.com/manos555555/PS5-Upload-Suite/blob/main/payload/main.c',
  },
  {
    author: 'drakmor', authorUrl: 'https://github.com/drakmor',
    project: 'ps5-hwinfo', projectUrl: 'https://github.com/drakmor/ps5-hwinfo',
    where: 'main.c',
    whereUrl: 'https://github.com/drakmor/ps5-hwinfo',
  },
  {
    author: 'Pharaoh2k', authorUrl: 'https://github.com/Pharaoh2k',
    project: 'ps5debug-NG', projectUrl: 'https://github.com/Pharaoh2k/ps5debug-NG',
    where: 'PROTOCOL.md · debugger/source/meta.c',
    whereUrl: 'https://github.com/Pharaoh2k/ps5debug-NG',
  },
  /* [CN-FORK] v0.5.2.154 存档同步 / 自动存档引擎这一轮的参考项目。 */
  {
    author: 'earthonion', authorUrl: 'https://github.com/earthonion',
    project: 'garlic-savemgr', projectUrl: 'https://github.com/earthonion/garlic-savemgr',
    where: 'src/main.c（存档镜像挂载 / 导入导出）',
    whereUrl: 'https://github.com/earthonion/garlic-savemgr',
  },
]

/* [CN-FORK] v0.5.2.94 更新日志从写死在 JSX 里的 45 段副本改成数据数组 + 共用 <Modal>。
 * v0.5.2.154 跟着「关于」页从设置页搬过来，Modal 换成页内板块（内容长，弹窗里滚不如直接看）。 */
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

const AboutView = ({ version }) => {
  const { t } = useTranslation()
  const donateUrl = 'https://github.com/itsPLK/ps5-payload-manager/blob/main/DONATE.md';
  const upstreamRepoUrl = 'https://github.com/itsPLK/ps5-payload-manager';

  return (
    <div className="space-y-6">
      <PageHead
        title={t("about.title", "关于")}
        desc={t("about.desc", "支持作者、本分支信息、参考致谢与更新日志。")}
      />

      {/* ── 支持作者（原「支持作者」页，设置页的入口已撤）────────────────── */}
      <Section
        id="donate"
        icon={Heart}
        title={t("about.donate_title", "支持作者")}
        bodyClass="flex flex-col xl:flex-row items-center justify-center gap-10 xl:gap-20 py-6 xl:py-10"
      >
        <div className="flex flex-col items-center text-center space-y-6 md:space-y-8 flex-1 px-4">
          <div className="p-6 md:p-8 rounded-[2.5rem] md:rounded-[3.5rem] bg-red-600/10 border border-red-600/20 text-red-500 shadow-2xl inline-flex">
            <Heart className="w-14 h-14 md:w-20 md:h-20 fill-current animate-pulse" />
          </div>
          <div className="space-y-4 md:space-y-6">
            <h3 className="text-2xl md:text-4xl font-black text-white uppercase italic tracking-tighter">
              {t("donate.title_1", "Support")} <span className="text-red-500">{t("donate.title_2", "Project")}</span>
            </h3>
            <p className="text-base md:text-lg text-zinc-400 font-medium leading-relaxed italic">
              {t("donate.description", "如果你喜欢本项目，可以支持原作者。非常感谢！")}
            </p>
          </div>
        </div>

        <div className="flex items-center justify-center flex-1 px-4">
          {isPS5 ? (
            <div className="bg-white p-6 md:p-8 rounded-[2rem] md:rounded-[3rem] shadow-[0_0_60px_rgba(220,38,38,0.3)]">
              <QRCodeSVG value={donateUrl} size={isPS5 ? 280 : 180} level="H" />
            </div>
          ) : (
            <a
              href={donateUrl}
              target="_blank"
              rel="noopener noreferrer"
              className="px-8 md:px-12 py-4 md:py-5 bg-red-600 text-white text-base md:text-xl font-black uppercase rounded-2xl hover:bg-red-500 transition-all transform active:scale-95 shadow-[0_0_50px_rgba(220,38,38,0.4)] text-center block"
            >
              {t("donate.view_options_btn", "查看捐赠方式")}
            </a>
          )}
        </div>
      </Section>

      {/* ── 关于本 Fork：原作者、上游地址、程序名、上游/本 fork 版本与维护者 naye 署名 ── */}
      <Section id="fork" icon={Info} title={t("donate.fork_title", "关于本 Fork")} bodyClass="space-y-3">
        <p className="text-sm text-zinc-300 leading-relaxed">
          {t("donate.fork_desc", "本程序是 Payload Manager 的分支，由 {{maintainer}} 维护。", { maintainer: "naye" })}
        </p>
        <dl className="grid grid-cols-[auto_1fr] gap-x-4 gap-y-1.5 text-sm">
          <dt className="text-zinc-500">{t("donate.fork_original_author", "原作者")}</dt>
          <dd className="text-white font-semibold">itsPLK</dd>
          <dt className="text-zinc-500">{t("donate.fork_app_name", "程序名")}</dt>
          <dd className="text-white font-semibold">Payload Manager</dd>
          <dt className="text-zinc-500">{t("donate.fork_upstream", "上游地址")}</dt>
          <dd>
            <a href={upstreamRepoUrl} target="_blank" rel="noopener noreferrer" className="text-ps-blue hover:underline break-all">
              github.com/itsPLK/ps5-payload-manager
            </a>
          </dd>
          <dt className="text-zinc-500">{t("donate.fork_upstream_version", "上游版本")}</dt>
          <dd className="text-white font-semibold">v0.5.2</dd>
          <dt className="text-zinc-500">{t("donate.fork_version", "本 Fork 版本")}</dt>
          {/* [CN-FORK] v0.5.2.113 兜底值原来写死 `'0.5.2.112'` —— 版本一涨它就是**过期的**，
              而这个字段正是用来告诉用户「你现在装的是哪一版」。改成读构建时烧入的
              __APP_VERSION__（vite.config.js 从 include/pldmgr.h 的 MENU_VERSION 取），
              下一次升版本这里不用再有人记得改。dev server 下它是 'dev'，也不比旧版本号差。 */}
          <dd className="text-white font-semibold">v{version || __APP_VERSION__}</dd>
        </dl>
        <p className="text-xs text-zinc-500 leading-relaxed">
          {t("donate.fork_donate_note", "上方捐赠按钮支持的是原作者 itsPLK，与本 fork 无关。")}
        </p>
      </Section>

      {/* ── 参考与致谢：与上方本 fork 署名**同页**（用户要求），逐条给作者主页与具体出处 ── */}
      <Section
        id="refs"
        icon={BookMarked}
        title={t("donate.ref_title", "参考与致谢")}
        count={REFERENCES.length}
        bodyClass="space-y-4"
      >
        <p className="text-xs text-zinc-500 leading-relaxed">
          {t("donate.ref_desc", "本 fork 的改动参考了下列开源项目，逐条标注作者与具体出处。")}
        </p>
        <ul className="space-y-3">
          {REFERENCES.map((r) => (
            <li key={`${r.author}-${r.where}`} className="text-sm leading-relaxed">
              <div className="flex flex-wrap items-baseline">
                <a href={r.authorUrl} target="_blank" rel="noopener noreferrer" className="mr-2 text-white font-semibold hover:underline">
                  {r.author}
                </a>
                <span className="mx-1.5 text-zinc-600">·</span>
                <a href={r.projectUrl} target="_blank" rel="noopener noreferrer" className="mr-2 text-ps-blue hover:underline break-all">
                  {r.project}
                </a>
                <span className="mx-1.5 text-zinc-600">—</span>
                <a href={r.whereUrl} target="_blank" rel="noopener noreferrer" className="text-zinc-400 hover:underline break-all font-mono text-xs">
                  {r.where}
                </a>
              </div>
            </li>
          ))}
        </ul>
      </Section>

      {/* ── 更新日志（原设置页 → 诊断 → 「更新日志」弹窗里的那份数组）────────── */}
      <Section
        id="changelog"
        icon={ScrollText}
        title={t("about.changelog_title", "更新日志")}
        count={CHANGELOG.length}
        bodyClass="space-y-4 text-sm leading-relaxed"
      >
        <p className="text-xs text-zinc-500 leading-relaxed">
          {t("about.changelog_desc", "本 Fork 的版本更新历史，新的在上。")}
        </p>
        {CHANGELOG.map((c) => (
          <div key={c.v} className="space-y-1">
            <p className="font-bold text-ps-blue">{c.v}</p>
            <p className={c.strike ? "text-zinc-500 line-through" : "text-zinc-400"}>{c.t}</p>
          </div>
        ))}
      </Section>
    </div>
  )
}

export default AboutView
