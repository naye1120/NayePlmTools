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
/* [CN-FORK] v0.5.2.156 用户：「精简所有更新日志，只把现在有的功能简单列出来就可以，
 * 作为第一版更新日志」。原来这里是 46 条「哪个版本改了什么」的历史 —— 对用面板的人
 * 没什么用，他要知道的是「这个工具现在能干什么」。所以整份历史撤掉，换成按板块的功能
 * 清单，版本号只留一条「第一版」。逐版细节去仓库 `CHANGELOG.md`（那份保留不动）。 */
const CHANGELOG = [
  { v: "第一版 · 当前功能", t: "按板块列出这个工具现在能做的事。" },
  { v: "Payload 管理", t: "浏览 / 安装 / 更新 / 删除 payload；内置云端仓库与自定义来源；装完自动同步越狱引导器（ps5_autoloader）。" },
  { v: "自动加载", t: "编排开机自动加载序列（延时行 + 载荷名），可上移 / 下移 / 插延时；写回幂等，不会自己多出「1 秒延时」。" },
  { v: "游戏管理", t: "扫描主机与 dump 游戏，磁贴墙 + 详情；改名写回系统 app.db / appinfo.db（电视主屏跟着变）；换背景与图标；多媒体管理。" },
  { v: "存档管理", t: "远程拉取 / 推送（FTP / HTTP / SMB）；本地快照（每次游戏退出后自动存一份）；garlic 兼容导入；拉取前比新旧；游戏运行中禁推拉。" },
  { v: "账号工具", t: "登录 / 登出 / 反激活，头像上传与裁剪。" },
  { v: "PKG 安装", t: "内嵌 PKG Manager，可在面板里上传、启停、更新版本。" },
  { v: "常用入口", t: "手动添加插件 / 局域网服务快捷卡片，逐条 TCP 探活，点卡片新标签打开。" },
  { v: "风扇温控", t: "读风扇状态，向 /dev/icc_fan 下发目标温度。" },
  { v: "其它", t: "远程游玩配对、SMP（ShadowMountPlus）探活与拉起、运行日志查看。" },
  { v: "界面", t: "深色主题；电视端（遥控器）适配；刷新后停在当前页；存档、多媒体等面板「铺满右侧」。" },
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
          {t("about.changelog_desc", "第一版：当前已有的功能。更细的逐版历史见仓库 CHANGELOG.md。")}
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
