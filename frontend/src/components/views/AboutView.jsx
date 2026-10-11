import React from 'react'
import { Info, BookMarked, ScrollText, Heart } from 'lucide-react'
import { QRCodeSVG } from 'qrcode.react'
import { useTranslation } from 'react-i18next'
import { isPS5 } from '../../utils/helpers'
import PageHead from '../ui/PageHead'
import Section from '../ui/Section'

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
  不再写「哪个文件」那一截。多位作者合作一个项目时也归并成一行。 */
const REFERENCES = [
  { author: 'John Törnblom', authorUrl: 'https://github.com/john-tornblom', project: 'ps5-payload-sdk', projectUrl: 'https://github.com/ps5-payload-dev/sdk' },
  { author: 'manos555555', authorUrl: 'https://github.com/manos555555', project: 'PS5-Upload-Suite', projectUrl: 'https://github.com/manos555555/PS5-Upload-Suite' },
  { author: 'drakmor', authorUrl: 'https://github.com/drakmor', project: 'ps5-hwinfo', projectUrl: 'https://github.com/drakmor/ps5-hwinfo' },
  { author: 'Pharaoh2k', authorUrl: 'https://github.com/Pharaoh2k', project: 'ps5debug-NG', projectUrl: 'https://github.com/Pharaoh2k/ps5debug-NG' },
  { author: 'earthonion', authorUrl: 'https://github.com/earthonion', project: 'garlic-savemgr', projectUrl: 'https://github.com/earthonion/garlic-savemgr' },
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
    date: "2026-10-11",
    items: [
      "移动端二级菜单：手机端 PKG 安装 / 常用配置 / 游戏管理 / 活动进程 / 设置 / 关于 改为二级下拉菜单，桌面与电视端保持原侧栏。",
      "自动存档屏蔽自制应用：自动存档（启动时拉取 / 退出时推送）跳过 homebrew 类游戏，避免误推镜像 / 误拉覆盖。",
      "存档转换：把其它用户的存档一键转换到当前用户可用，逐文件合并而非整份替换。",
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
    items: ["远程拉取 / 推送（FTP / HTTP / SMB）；本地快照（每次游戏退出后自动存一份）；garlic 兼容导入；拉取前比新旧；游戏运行中禁推拉；存档转换。"],
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
   * 本 fork 维护者 naye 的捐赠二维码：左侧面板展示，二维码内容是收款码链接
   * （项目尚未公布占位，发布者填入真实收款码后即可扫码打赏）。 */
  const originalDonateUrl = 'https://github.com/itsPLK/ps5-payload-manager/blob/main/DONATE.md'
  const forkDonateUrl = 'https://github.com/naye/ps5-payload-manager'

  return (
    <div className="space-y-6">
      <PageHead
        title={t("about.title", "关于")}
        desc={t("about.desc", "本分支信息、参考致谢与更新日志。")}
      />

      {/* ── 关于本 Fork + 捐赠栏**：左右并排 ──────────────────────────────
       * 左侧：fork 信息（原作者 / 上游 / 程序名 / 版本 / 维护者 naye）。
       * 右侧：两块「支持」 —— 上方是原作者 itsPLK 的捐赠链接（按钮指向其 DONATE.md），
       *        下方是本 fork 维护者 naye 的捐赠二维码（QRCodeSVG 直接画在板上）。
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
            <dt className="text-zinc-500">{t("donate.fork_upstream_version", "上游版本")}</dt>
            <dd className="text-white font-semibold">v0.5.2</dd>
            <dt className="text-zinc-500">{t("donate.fork_version", "本 Fork 版本")}</dt>
            {/* [CN-FORK] v0.5.2.113 现读构建时烧入的 __APP_VERSION__（vite.config.js
                从 include/pldmgr.h 的 MENU_VERSION 取）。dev server 下它是 'dev'。 */}
            <dd className="text-white font-semibold">v{version || __APP_VERSION__}</dd>
          </dl>
        </div>

        {/* 右侧：两块捐赠栏（原作者按钮 + 本 fork 二维码） */}
        <div className="flex flex-col gap-6 items-center xl:items-end min-w-0 w-full xl:w-[18rem]">
          {/* 上：原作者捐赠入口 —— 按钮而不是二维码：itsPLK 的 DONATE.md 里写的是
              「查看捐赠方式」而非单一收款码，按钮形态更合适（点开新页去看）。 */}
          <a
            href={originalDonateUrl}
            target="_blank"
            rel="noopener noreferrer"
            className="w-full flex items-center gap-3 px-4 py-3 bg-red-600/10 border border-red-600/30 hover:bg-red-600/20 rounded-2xl transition-colors group"
          >
            <span className="p-2 rounded-xl bg-red-600/20 text-red-400 group-hover:text-red-300 shrink-0">
              <GithubMark className="w-5 h-5" />
            </span>
            <span className="min-w-0">
              <span className="block text-[11px] uppercase tracking-wider text-red-300/80 font-bold">原作者</span>
              <span className="block text-white font-bold truncate">支持 itsPLK →</span>
            </span>
          </a>

          {/* 下：本 fork 维护者捐赠二维码 —— 固定 QR 码形态，与头像/原本那个
              「查看捐赠方式」按钮属于同一档视觉。SVG 直接画，规避图片外链。 */}
          <div className="w-full flex flex-col items-center gap-2 px-4 py-4 rounded-2xl border border-ps-blue/30 bg-ps-blue/5">
            <div className="bg-white p-3 rounded-xl shadow-[0_0_40px_rgba(56,139,253,0.2)]">
              <QRCodeSVG value={forkDonateUrl} size={isPS5 ? 200 : 140} level="H" />
            </div>
            <span className="flex items-center gap-1.5 text-ps-blue text-xs font-bold uppercase tracking-wider mt-1">
              <Heart className="w-3.5 h-3.5 fill-current" />
              本 Fork 维护者 naye
            </span>
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