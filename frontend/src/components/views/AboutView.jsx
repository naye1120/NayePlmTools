import React from 'react'
import { Info, BookMarked, ScrollText, Heart } from 'lucide-react'
import { useTranslation } from 'react-i18next'
import { isPS5 } from '../../utils/helpers'
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'
/* [CN-FORK] v0.5.2.171 本 fork 的捐赠收款码改成**图片**（原先是用 QRCodeSVG 现场画的
 * 仓库链接二维码，扫出来只是打开 GitHub，不是收款）。图片走 Vite 资源导入 —— 产物是
 * viteSingleFile 单页 HTML，assetsInlineLimit=100000 会把这张 27KB 的 PNG 内联成
 * base64 data URI 烧进 HTML，所以不会多出外链文件（否则嵌进 ELF 后必 404）。 */
import donateQr from '../../assets/donate-qr.png'

/* [CN-FORK] v0.5.2.163 lucide-react 1.x **删掉了全部品牌图标**（`Github` / `Twitter` …），
 * 从它那儿 import `Github` 会在模块求值时直接抛
 *   `does not provide an export named 'Github'`
 * ——整个 App 白屏（Vite 预打包的 dep 是静态 ESM，缺一个具名导出就整包失败）。
 * 所以这里自带一份官方 GitHub mark（viewBox 16×16，currentColor 填充），
 * 不引第三方品牌图标包。 */
function GithubMark({ className = '' }) {
  return (
    <svg viewBox="0 0 16 16" fill="currentColor" aria-hidden="true" className={className}>
      <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27s1.36.09 2 .27c1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8z" />
    </svg>
  )
}

/* [CN-FORK] v0.5.2.163 「关于」页重构：
 *   - 移除原「支持作者」独立 Section（用户口径：「取消支持作者板块」）。
 *   - 在「关于本 Fork」**右边**加：原作者捐赠链接 + 本作者捐赠二维码。
 *     这是把「支持作者」内容合并到 fork 板块的方式 —— 用户在同一处能看到
 *     上面 fork 信息 + 右边「原作者 itsPLK 捐赠页 / 本 fork naye 捐赠二维码」。
 *   - 参考与致谢简化为「一行一个作者 + 项目链接」，不写到具体文件（用户口径）。
 *   - 更新日志顶部追加本轮新增条目（标日期）。
 */

/* [CN-FORK] v0.5.2.163 参考致谢按用户口径收窄为「一个作者一行 + 项目链接」，
  不再写「哪个文件」那一截。多位作者合作一个项目时也归并成一行。
 * [CN-FORK] v0.5.2.174 用户口径：「参考与致谢中差了好多，你把参考过的项目都写上，写全」。
 *   上一版只列了 5 条（还是上游 README 的一个子集），而本分支真正参考/移植/依赖过的
 *   项目远不止这些 —— 逐条从源码注释里核出来的（`src/` `include/` 的文件头 +
 *   CHANGELOG/HANDOVER），分四组：上游与 SDK / 游戏与存档 / 音频与图像 / 风扇与系统。
 *   只写「作者 + 项目 + 链接」，不写具体文件（沿用 .163 的口径）。 */
const REFERENCES = [
  /* ── 上游与 SDK ── */
  { author: 'itsPLK', authorUrl: 'https://github.com/itsPLK', project: 'ps5-payload-manager（本分支上游）', projectUrl: 'https://github.com/itsPLK/ps5-payload-manager' },
  { author: 'itsPLK', authorUrl: 'https://github.com/itsPLK', project: 'ps5_next_menu（侧栏与导航原型）', projectUrl: 'https://github.com/itsPLK/ps5_next_menu' },
  { author: 'itsPLK', authorUrl: 'https://github.com/itsPLK', project: 'ps5-pkg-manager', projectUrl: 'https://github.com/itsPLK/ps5-pkg-manager' },
  { author: 'itsPLK', authorUrl: 'https://github.com/itsPLK', project: 'ps5-bdjb-autoloader', projectUrl: 'https://github.com/itsPLK/ps5-bdjb-autoloader' },
  { author: 'itsPLK', authorUrl: 'https://github.com/itsPLK', project: 'ps5-lua-autoloader', projectUrl: 'https://github.com/itsPLK/ps5-lua-autoloader' },
  { author: 'itsPLK', authorUrl: 'https://github.com/itsPLK', project: 'ps5-y2jb-autoloader', projectUrl: 'https://github.com/itsPLK/ps5-y2jb-autoloader' },
  { author: 'John Törnblom', authorUrl: 'https://github.com/john-tornblom', project: 'ps5-payload-sdk', projectUrl: 'https://github.com/ps5-payload-dev/sdk' },
  { author: 'ps5-payload-dev', authorUrl: 'https://github.com/ps5-payload-dev', project: 'pacbrew-repo（libsmb2 正式包）', projectUrl: 'https://github.com/ps5-payload-dev/pacbrew-repo' },
  { author: 'ps5-payload-dev', authorUrl: 'https://github.com/ps5-payload-dev', project: 'websrv（libsmb2 用法参考）', projectUrl: 'https://github.com/ps5-payload-dev/websrv' },
  { author: 'ps5-payload-dev', authorUrl: 'https://github.com/ps5-payload-dev', project: 'offact（离线账号激活，账号工具源自它）', projectUrl: 'https://github.com/ps5-payload-dev/offact' },

  /* ── 游戏与存档 ── */
  { author: 'drakmor', authorUrl: 'https://github.com/drakmor', project: 'ShadowMountPlus（挂载 / 游戏识别 / 详情跳转）', projectUrl: 'https://github.com/drakmor/ShadowMountPlus' },
  { author: 'earthonion', authorUrl: 'https://github.com/earthonion', project: 'garlic-savemgr（兼容导入的格式依据）', projectUrl: 'https://github.com/earthonion/garlic-savemgr' },
  { author: 'manos555555', authorUrl: 'https://github.com/manos555555', project: 'PS5-Upload-Suite', projectUrl: 'https://github.com/manos555555/PS5-Upload-Suite' },
  { author: 'Pharaoh2k', authorUrl: 'https://github.com/Pharaoh2k', project: 'ps5debug-NG', projectUrl: 'https://github.com/Pharaoh2k/ps5debug-NG' },

  /* ── 音频与图像 ── */
  { author: 'blackbearreloaded', authorUrl: 'https://github.com/blackbearreloaded', project: 'ps5-at9-converter（浏览器端 ATRAC9 编码器移植自它）', projectUrl: 'https://github.com/blackbearreloaded/ps5-at9-converter' },
  { author: 'Thealexbarney', authorUrl: 'https://github.com/Thealexbarney', project: 'LibAtrac9（ATRAC9 解码表来源，MIT）', projectUrl: 'https://github.com/Thealexbarney/LibAtrac9' },
  { author: 'nothings', authorUrl: 'https://github.com/nothings', project: 'stb（stb_image / stb_image_write）', projectUrl: 'https://github.com/nothings/stb' },

  /* ── 风扇与系统 ── */
  { author: 'drakmor', authorUrl: 'https://github.com/drakmor', project: 'ps5-hwinfo（温度 / 传感器）', projectUrl: 'https://github.com/drakmor/ps5-hwinfo' },
  { author: 'drakmor', authorUrl: 'https://github.com/drakmor', project: 'fan_target（风扇阈值）', projectUrl: 'https://github.com/drakmor/fan_target' },
  { author: 'owendswang', authorUrl: 'https://github.com/owendswang', project: 'ps5-fan-control', projectUrl: 'https://github.com/owendswang/ps5-fan-control' },
  { author: 'BenNoxXD', authorUrl: 'https://github.com/BenNoxXD', project: 'Disc Player 终止逻辑', projectUrl: 'https://github.com/BenNoxXD' },
]

/* [CN-FORK] v0.5.2.156 用户：「精简所有更新日志，只把现在有的功能简单列出来就可以，
 * 作为第一版更新日志」。`.163` 在这份「第一版」基础上追加本轮新功能 —— 都是按板块
 * 描述能力，不是逐版历史。
 *
 * [CN-FORK] v0.5.2.166 改成**按日期分组**。用户口径：「日志同一个日期，只写一个就行，
 * 内容再下边排版」。原来是一个扁平数组、每条自己带 `2026-10-11 · xxx` 前缀，
 * 于是同一天的日期被印了 8 遍，看起来像 8 个版本。现在日期只出现一次做标题，
 * 条目排在它下面（`items`），同一天的新内容往同一个 items 里加。
 *
 * 没有 date 的那一组（第一版能力清单）用 `title` 当标题，样式上区分开：
 * 日期是琥珀色、能力清单是蓝色。 */
const CHANGELOG = [
  {
    date: "2026-10-18",
    items: [
      "本程序在线自更新：面板能从你已配置的仓库直接更新自身，下载好后点「重启生效」即接管（仅加载器确收下新 ELF 才让当前进程退出）。",
      "移动端「工具 / 系统」二级菜单改成公众号式横向弹出，按被点按钮所在位置锚定，不再铺满整行。",
      "「个性化软件」弹窗重排：移除换图标 / 换背景（只留主机自带图标的预览与置顶 / 隐藏），列表改卡片宫格，图标移到最左独占一列。",
      "已安装 payload 的图标尺寸与仓库卡片统一。",
      "「已安装的 Payload」不再重复显示本程序：宿主自身只由顶部专属自更新行统一呈现（运行版本 / 待重启 / 已是最新 / 更新），普通宫格里的同名条目已剔除。",
      "手动上传新版本程序时，照常完成普通导入与引导器登记；若比当前运行版本更新，会额外归入自更新待接管位，顶部行直接给出「重启生效」按钮干净接管。",
      "修复仓库详情页把非 URL 的来源标签当外链打开导致的 404（如 source 被填成「github release v0.x.x」时不再渲染成可点链接）。",
      "仓库图标目录解析更健壮：单张图标过大/非法只会跳过那一张，不再因为一张超标图标导致整个源的图标全部消失。",
      "顶部「本程序」行新增「启动/重启」按钮：原地重新拉起一份与当前运行版本相同的已安装 ELF（同版本、不改版本，仅重启面板进程）；需盘上有与当前版本一致的已安装副本才能重启。",
      "顶部「本程序」行的图标与仓库目录卡/普通已安装卡同源：命中仓库条目就显示真图标，不再固定显示名字首字兜底块。",
      "payload 图标整体减半（顶部本程序行 / 普通已安装卡 / 仓库目录卡三处同步）：从 64/80px 缩到 32/40px，字母与圆角同比缩小，三处仍保持同尺寸。",
    ],
  },
  {
    date: "2026-10-14",
    items: [
      "合并上游 v0.6.0 —— 上游版本从 v0.5.2 跳到 v0.6.0（「关于本 Fork」里的上游版本号已同步）。底层换成新的仓库上下文、槽位管理、进程管理与日志服务，并带上上游这一版的稳定性修复。",
      "「获取载荷」页重做：卡片网格 + 搜索 + 按来源筛选 + 安装状态徽标，仓库目录大时不再卡顿。",
      "新增载荷详情页：直接看该载荷的 README 文档与版本信息，不用再跳去 GitHub。",
      "收藏与自定义顺序改为存在主机上：换浏览器、换设备都同步，不再各存各的。",
      "遥控器返回键 / 浏览器后退：现在先关弹窗、详情、编辑态，不再直接退出浏览器；并支持用 URL 直达某一页。",
      "自动加载页样式刷新、载荷启动浮层重做、通知条更干净；新增荷兰语。",
      "「关于本 Fork」版式调整：原作者捐赠入口（支持 itsPLK）从右上角移到 fork 信息表下方，右侧只留本 fork 收款码与仓库入口。",
    ],
  },
  {
    date: "2026-10-13",
    items: [
      "本地快照只在两个时机发生：游戏退出时（与往远程推送同一时刻）和手动「立即备份」。删掉了全部「静默窗口」与延迟重试队列 —— 退出就当场拍，不再有「等下一轮」这种永远等不到的等待。",
      "游戏退出时本地快照**成功**会弹一条主机系统通知（「本地快照完成 · 已生成 N 份」）—— 退出后往往已经回到主屏、不看面板，弹窗是当场知道「存档安全落地」的唯一通道。没拍成 / 没得拍不发弹窗，在「最近活动」里看。",
      "「本地快照」里新增「最近活动」：每一次快照的结果都写到屏幕上（含「内容没变、未新增」「没得拍」「拍失败」这些以前只在日志里的负向结果），带时刻。",
      "三张列表的每一行都加「下载」：存档列表（主机上当前那份）、远程存档（云上那份备份）、本地快照（某个时刻那份），三颗都排在删除按钮前面。",
      "存档管理版式调整：兼容导入（Garlic）从「存档列表」那张卡里搬出来单独占一行，目标用户与导入按钮并排；存档行不再被右侧那栏劈成两半。",
      "删掉「存档转换」：它想干的事（把别人的存档弄到自己名下）兼容导入本来就能做，而且是逐文件合并、更干净。",
      "兼容导入改成明确「改签」：不论存档原来属于谁，都落到你选定的用户名下，提示与确认框都写清楚；不再把 garlic 自带的 manifest.json 写进主机存档目录。",
      "「参考与致谢」补全：把本分支参考 / 移植 / 依赖过的项目按来源分四组列全（上游与 SDK、游戏与存档、音频与图像、风扇与系统）。",
    ],
  },
  {
    date: "2026-10-11",
    items: [
      "移动端二级菜单：手机端 PKG 安装 / 常用配置 / 游戏管理 / 活动进程 / 设置 / 关于 改为二级下拉菜单，桌面与电视端保持原侧栏。",
      "自动存档屏蔽自制应用：自动存档（启动时拉取 / 退出时推送）跳过 homebrew 类游戏，避免误推镜像 / 误拉覆盖。",
      "存档管理显示游戏编号：存档管理标题里把游戏 TID 直接显示在名称后边。",
      "多媒体分享二维码：多媒体管理每个下载按钮旁加分享按钮，扫码即可拿到对应资源。",
      "关于页重排：取消独立的「支持作者」板块，原作者捐赠链接与本 fork 捐赠二维码并入「关于本 Fork」右侧。参考与致谢精简为一行一项。",
      "改名与兼容导入两个 bug：改名完成后 PS5 主屏不变、garlic 导入的存档没落到当前用户，两处都已修复并给出可读的原因。",
      "背景音乐改成真正写进主机：PS5 磁贴音乐只认 ATRAC9，现在由浏览器内置的 ATRAC9 编码器转码，再写进游戏包的 sce_sys/snd0.at9（带响度元数据与原件备份）。以前只存在面板自己目录里，主机根本不读 —— 这才是「改了没生效」的根因。",
      "置顶改成写主机：置顶不再只记在面板里，而是写主机 app.db 的 MyHome 固定列表（idx 就是主屏位置），并且放开了「只能置顶内置软件」的限制，游戏也能置顶。",
      "排序改成拖拽：点「排序」进入排序模式，磁贴左上角显示位置号，直接拖到想要的位置松手即可；已固定的条目会同步写回主机顺序，遥控器上可用上移 / 下移。",
      "分享二维码改成直接下载：扫码后直接下载分享的那份文件（单文件是直链，多选 / 全量是打包链接），不再是打开本页面。",
      "更新日志排版：同一天只显示一次日期，条目排在日期下面。",
      "改名弹窗版式重排：磁贴图标用左侧小预览 + 右侧说明与按钮，游戏名称与保存按钮同一行，背景音乐带内嵌播放器（播放 / 时长 / 进度 / 音量）。",
    ],
  },
  {
    title: "第一版 · 当前功能",
    items: [
      "按板块列出这个工具现在能做的事。",
    ],
  },
  {
    title: "Payload 管理",
    items: ["浏览 / 安装 / 更新 / 删除 payload；内置云端仓库与自定义来源；装完自动同步越狱引导器（ps5_autoloader）。"],
  },
  {
    title: "自动加载",
    items: ["编排开机自动加载序列（延时行 + 载荷名），可上移 / 下移 / 插延时；写回幂等，不会自己多出「1 秒延时」。"],
  },
  {
    title: "游戏管理",
    items: ["扫描主机与 dump 游戏，磁贴墙 + 详情；改名写回系统 app.db / appinfo.db（电视主屏跟着变）；换背景与图标；背景音乐（ATRAC9）；置顶与拖拽排序写回主屏固定列表；多媒体管理。"],
  },
  {
    title: "存档管理",
    items: ["远程拉取 / 推送（FTP / HTTP / SMB）；本地快照（每次游戏退出后自动存一份）；garlic 兼容导入（改签到选定的用户）；拉取前比新旧；游戏运行中禁推拉。"],
  },
  {
    title: "账号工具",
    items: ["登录 / 登出 / 反激活，头像上传与裁剪。"],
  },
  {
    title: "PKG 安装",
    items: ["内嵌 PKG Manager，可在面板里上传、启停、更新版本。"],
  },
  {
    title: "常用入口",
    items: ["手动添加插件 / 局域网服务快捷卡片，逐条 TCP 探活，点卡片新标签打开。"],
  },
  {
    title: "风扇温控",
    items: ["读风扇状态，向 /dev/icc_fan 下发目标温度。"],
  },
  {
    title: "其它",
    items: ["远程游玩配对、SMP（ShadowMountPlus）探活与拉起、运行日志查看。"],
  },
  {
    title: "界面",
    items: ["深色主题；电视端（遥控器）适配；移动端二级菜单；刷新后停在当前页；存档、多媒体等面板「铺满右侧」。"],
  },
]

const CHANGELOG_COUNT = CHANGELOG.reduce((n, g) => n + g.items.length, 0)

const AboutView = ({ version }) => {
  const { t } = useTranslation()
  const upstreamRepoUrl = 'https://github.com/itsPLK/ps5-payload-manager'
  /* [CN-FORK] v0.5.2.163 原作者的捐赠入口（来自其上游仓库 DONATE.md）。
   * [CN-FORK] v0.5.2.171 本 fork 维护者 naye 的**仓库地址**与**捐赠收款码**：
   *   - 仓库地址（`forkRepoUrl`）直接显示在「关于本 Fork」信息表与右侧卡片上；
   *   - 捐赠二维码改成真实收款码图片（`assets/donate-qr.png`），不再是仓库链接的
   *     现场二维码 —— 那个扫出来只打开 GitHub，用户要的是「扫码打赏」。 */
  const originalDonateUrl = 'https://github.com/itsPLK/ps5-payload-manager/blob/main/DONATE.md'
  const forkRepoUrl = 'https://github.com/naye1120/NayePlmTools'

  return (
    <div className="space-y-6">
      <PageHead
        title={t("about.title", "关于")}
        desc={t("about.desc", "本分支信息、参考致谢与更新日志。")}
      />

      {/* ── 关于本 Fork + 捐赠栏**：左右并排 ──────────────────────────────
       * 左侧：fork 信息（原作者 / 上游 / **本 fork 仓库** / 程序名 / 版本）。
       * 右侧：两块「支持」 —— 上方是原作者 itsPLK 的捐赠链接（按钮指向其 DONATE.md），
       *        下方是本 fork 维护者 naye 的**捐赠收款码图片** + **本 fork 仓库按钮**。
       * 这就是用户要求的「取消支持作者板块，在关于本fork右边加原作者捐赠连接和本作者捐赠二维码」
       * 的实现 —— 不再单开一个 Section，并进同一行更紧凑。 */}
      <Section
        id="fork"
        icon={Info}
        title={t("donate.fork_title", "关于本 Fork")}
        bodyClass="grid grid-cols-1 xl:grid-cols-[1fr_auto] gap-8 xl:gap-10 items-start"
      >
        {/* 左侧：fork 文本信息 */}
        <div className="space-y-3 min-w-0">
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
            {/* [CN-FORK] v0.5.2.171 本 fork 自己的仓库地址（用户口径：「把我的 github
                地址也写上」）。放在上游地址下一行，同一种链接样式。 */}
            <dt className="text-zinc-500">{t("donate.fork_repo", "本 Fork 地址")}</dt>
            <dd>
              <a href={forkRepoUrl} target="_blank" rel="noopener noreferrer" className="text-ps-blue hover:underline break-all">
                github.com/naye1120/NayePlmTools
              </a>
            </dd>
            <dt className="text-zinc-500">{t("donate.fork_upstream_version", "上游版本")}</dt>
            {/* [CN-FORK] v0.6.1.189 合并上游 v0.6.1 后同步（原写死 v0.6.0 已过时）。 */}
            <dd className="text-white font-semibold">v0.6.1</dd>
            <dt className="text-zinc-500">{t("donate.fork_version", "本 Fork 版本")}</dt>
            {/* [CN-FORK] v0.5.2.113 现读构建时烧入的 __APP_VERSION__（vite.config.js
                从 include/pldmgr.h 的 MENU_VERSION 取）。dev server 下它是 'dev'。 */}
            <dd className="text-white font-semibold">v{version || __APP_VERSION__}</dd>
          </dl>

          {/* [CN-FORK] v0.6.0.176 用户口径：「红框圈起来的东西放到绿框的位置」——
              原作者捐赠入口从右列顶部搬到 fork 信息表**下方**（左列底部），
              右列只留本 fork 的收款码 + 仓库按钮。 */}
          <a
            href={originalDonateUrl}
            target="_blank"
            rel="noopener noreferrer"
            className="flex items-center gap-3 px-4 py-3 bg-red-600/10 border border-red-600/30 hover:bg-red-600/20 rounded-2xl transition-colors group"
          >
            <span className="p-2 rounded-xl bg-red-600/20 text-red-400 group-hover:text-red-300 shrink-0">
              <GithubMark className="w-5 h-5" />
            </span>
            <span className="min-w-0">
              <span className="block text-[11px] uppercase tracking-wider text-red-300/80 font-bold">原作者</span>
              <span className="block text-white font-bold truncate">支持 itsPLK →</span>
            </span>
          </a>
        </div>

        {/* 右侧：本 fork 收款码 + 仓库按钮（原作者入口 v0.6.0.176 起移到左列） */}
        <div className="flex flex-col gap-6 items-center xl:items-end min-w-0 w-full xl:w-[18rem]">
          {/* 本 fork 维护者捐赠二维码 —— **真实收款码图片**（v0.5.2.171 起，
              之前是用 QRCodeSVG 画的仓库链接二维码，扫出来只是打开 GitHub）。
              图片经 Vite 资源导入，单文件产物里被内联成 base64 data URI。 */}
          <div className="w-full flex flex-col items-center gap-2 px-4 py-4 rounded-2xl border border-ps-blue/30 bg-ps-blue/5">
            <div className="bg-white p-3 rounded-xl shadow-[0_0_40px_rgba(56,139,253,0.2)]">
              <img
                src={donateQr}
                alt={t("donate.fork_qr_alt", "捐赠收款码")}
                width={isPS5 ? 200 : 140}
                height={isPS5 ? 200 : 140}
                className="block w-auto h-auto"
                style={{ width: isPS5 ? 200 : 140, height: isPS5 ? 200 : 140 }}
              />
            </div>
            <span className="flex items-center gap-1.5 text-ps-blue text-xs font-bold uppercase tracking-wider mt-1">
              <Heart className="w-3.5 h-3.5 fill-current" />
              {t("donate.fork_maintainer_qr", "本 Fork 维护者 {{maintainer}}", { maintainer: "naye" })}
            </span>
            {/* [CN-FORK] v0.5.2.171 维护者的 GitHub 仓库入口（用户口径：「把我的 github
                地址也写上」）。做成与上方「支持 itsPLK」同档的按钮，视觉对称。 */}
            <a
              href={forkRepoUrl}
              target="_blank"
              rel="noopener noreferrer"
              className="w-full flex items-center gap-3 px-4 py-3 bg-ps-blue/10 border border-ps-blue/30 hover:bg-ps-blue/20 rounded-2xl transition-colors group"
            >
              <span className="p-2 rounded-xl bg-ps-blue/20 text-ps-blue shrink-0">
                <GithubMark className="w-5 h-5" />
              </span>
              <span className="min-w-0">
                <span className="block text-[11px] uppercase tracking-wider text-ps-blue/80 font-bold">
                  {t("donate.fork_repo_label", "本 Fork 仓库")}
                </span>
                <span className="block text-white font-bold truncate">github.com/naye1120/NayePlmTools</span>
              </span>
            </a>
          </div>
        </div>
      </Section>

      {/* ── 参考与致谢：精简为一行一项（用户口径），去掉「具体文件」那截 ── */}
      <Section
        id="refs"
        icon={BookMarked}
        title={t("donate.ref_title", "参考与致谢")}
        count={REFERENCES.length}
        bodyClass="space-y-3"
      >
        <p className="text-xs text-zinc-500 leading-relaxed">
          {t("donate.ref_desc", "本 fork 的改动参考了下列开源项目，每个作者与项目一行。")}
        </p>
        <ul className="space-y-2">
          {REFERENCES.map((r) => (
            <li key={`${r.author}-${r.project}`} className="text-sm leading-relaxed flex flex-wrap items-baseline">
              <a href={r.authorUrl} target="_blank" rel="noopener noreferrer" className="mr-2 text-white font-semibold hover:underline">
                {r.author}
              </a>
              <span className="mx-1.5 text-zinc-600">·</span>
              <a href={r.projectUrl} target="_blank" rel="noopener noreferrer" className="text-ps-blue hover:underline break-all">
                {r.project}
              </a>
            </li>
          ))}
        </ul>
      </Section>

      {/* ── 更新日志：**按日期分组**（用户口径：「同一个日期只写一个就行，内容再下边排版」）。
          日期只出现一次做标题，条目排在它下面；没有 date 的那几组（第一版能力清单）
          用 title 当标题，颜色区分开。 ── */}
      <Section
        id="changelog"
        icon={ScrollText}
        title={t("about.changelog_title", "更新日志")}
        count={CHANGELOG_COUNT}
        bodyClass="space-y-5 text-sm leading-relaxed"
      >
        <p className="text-xs text-zinc-500 leading-relaxed">
          {t("about.changelog_desc", "本轮新功能（标日期）+ 第一版按板块列出的能力清单。更细的逐版历史见仓库 CHANGELOG.md。")}
        </p>
        {CHANGELOG.map((g) => (
          <div key={g.date || g.title} className="space-y-2">
            <p className={`font-bold ${g.date ? 'text-amber-400' : 'text-ps-blue'}`}>
              {g.date || g.title}
            </p>
            <ul className="space-y-1.5">
              {g.items.map((it, i) => (
                <li key={i} className="flex gap-2 text-zinc-400">
                  <span className="text-zinc-600 shrink-0 select-none">·</span>
                  <span className="min-w-0">{it}</span>
                </li>
              ))}
            </ul>
          </div>
        ))}
      </Section>
    </div>
  )
}

export default AboutView