# PS5 Payload Manager 中文 Fork 维护手册（CN-FORK）

> 本文档由本次改造（深色/浅色双主题、中文菜单布局、多语言 overlay、本地调试环境）
> 沉淀而来。**升级上游前请先读第 4 节。**

上游仓库：<https://github.com/itsPLK/ps5-payload-manager>（本次基线：v0.5.1 / commit 218f291）

---

## 1. 远程仓库（payload 源）组成与自建方案

### 1.1 上游镜像仓库 `ps5-payloads-mirror` 的构成

极简，仅 4 部分，**完全可以自建**：

| 组件 | 作用 |
| --- | --- |
| `payloads.json`（仓库根，经 GitHub Pages 暴露） | payload 清单。注意：官方源是**裸数组**（无 `{"name":...,"payloads":[...]}` 包装），因为 C 端解析器 `parse_repository_payloads()` 只是逐个扫描 `{...}` 对象，自定义源才需要 `name` 字段在前 |
| GitHub Release `payloads-mirror` | 二进制（.elf/.bin）实际存储处，URL 形如 `.../releases/download/payloads-mirror/<filename>` |
| `update_payloads.py` + `add_payload.py` | 每日扫描各 payload 上游 GitHub Release → 择优下载 → 重命名 `<name>_<version>.<ext>` → 算 SHA-256 → 更新 JSON/README → 清理过期 asset（记入 `download_stats.json`） |
| `.github/workflows/update_mirror.yml` | cron 每日 0 点：跑脚本 → `gh release upload --clobber` → commit & push JSON |

清单条目字段：`name / filename / url / source / source_direct / asset_pattern? / extract_file? / description / last_update / version / category / checksum(SHA-256)`。
可选字段：`asset_pattern`（正则筛选 asset）、`extract_file`（release 附件是 zip 时从中提取）。

### 1.2 自建步骤

1. Fork 或直接新建仓库，把 `ps5-payloads-mirror` 的 4 个组件拷入。
2. 全局替换 `update_payloads.py` 中的 `itsPLK` / `BASE_URL` / owner/repo 常量为你的；`payloads.json` 初始为 `[]`，用 `python add_payload.py <name> <source_repo_url>` 添加条目。
3. 开启 Pages（main 分支根目录）与 Releases；仓库 Settings → Actions 允许 workflow（`GITHUB_TOKEN` 自带 contents:write 即可，无需额外 secret）。
4. 用户侧接入（**无需改代码，推荐**）：Payload Manager → 设置 → Payload 来源 → 管理来源 → 添加
   `https://<你的用户名>.github.io/<repo>/payloads.json`。
5. 若要替换「默认源」：改 `include/pldmgr.h` 的 `REPOSITORY_SOURCE_URL` 并重新构建 ELF（见第 5 节，需要 Docker）。

### 1.3 国内可达性风险与对策

GitHub Pages/Releases 在大陆网络下不稳定（PS5 端 curl 直连）。对策优先级：
1. 自建源 JSON + 二进制放**国内可直连的静态托管**（自己的 VPS/对象存储，需 HTTPS；curl 用内嵌 cacert.pem 校验，证书链必须完整；建议响应带 `Access-Control-Allow-Origin: *`）。
2. 或走 GitHub + CDN 加速域名（反代 GitHub raw/releases），注意 checksum 是下载后本地校验，中转不破坏完整性，但**JSON 被篡改即失去信任锚**，中转方必须可信（HTTPS 同源）。
3. PS5 端 `repository.c` 在 SSL 校验失败时会降级为不安全下载（依赖 SHA-256 兜底），自建私有源时务必给每个条目配 `checksum`。

## 2. 多语言（i18n）结构

- 词条文件：`frontend/src/locales/*.json` 是**独立 git 子模块**
  （`itsPLK/ps5-payload-manager-translations`），由 **Crowdin 单向同步**，扁平 key
  （命名空间风格 `app.nav.dashboard`、`settings.xxx`、`storage_hub.xxx`）。
- 加载逻辑 `frontend/src/i18n.js`：Vite glob 导入 → 与 en-US 基线对比，**翻译完成度 <70% 的语言不注册**。
- 语言检测：localStorage(`i18nextLng`) → navigator；「跟随系统语言」= 删除 localStorage。
- 中文已有独立 locale `zh-CN.json`（本 fork 不改子模块，见下）。
- 提取工具：`npm run extract`（i18next-parser），配置 `i18next-parser.config.js`。

### 本 fork 新增：翻译 overlay 层（`frontend/src/locales-overlay/`）

`i18n.js` 在子模块加载后合并 `locales-overlay/<lang>.json`（后者优先）。
**所有 fork 专属词条/措辞微调只放 overlay，绝不改子模块**，因此永不与 Crowdin 同步冲突。
新增界面 key 的流程：组件里 `t("my.new_key", "Default English")` → 在 overlay 的
zh-CN.json / en-US.json 补词条（其他语言靠默认英文兜底，愿意可提 PR 给上游 Crowdin）。

## 3. 主题/排版架构（本次 UI 改造的机制）

设计原则：**零修改上游组件类名**，用「类名重映射层」翻转配色，把合并冲突面压到最小。

```
frontend/src/theme/
├── light-theme.css    # [data-theme="light"] 下的暗→亮类名重映射（含审计修复补丁 v1.1）
├── cn-typography.css  # <html lang^="zh"> 下：中文字体栈、关 uppercase/italic、收敛字距
├── useTheme.js        # 偏好存储(localStorage pldmgrTheme: auto|light|dark)、
│                      # <html data-theme> 应用、防 FOUC、语言<->lang 同步
└── ThemeToggle.jsx    # 三段式外观切换控件（侧栏底部 compact / 设置页完整）
```

- 上游暗色 UI 的配色词汇高度集中（扫描统计：text-white 94 处、zinc 灰阶 100 处、
  white/black 半透明 120 处），全部已在 light-theme.css 建立映射。
- 上游若引入**新的颜色类**（如 `bg-orange-500`），只需在 light-theme.css 追加一行。
- 注意两个陷阱（已处理，审计时留意）：
  1. `!text-white` 这类 important 变体是**独立类名** `.\!text-white`，需单独映射；
  2. `tailwind.config.js` 里的字面色（ps-card/ps-border）不跟随 CSS 变量，需单独映射。
- 改动过的上游文件（每处均有 `[CN-FORK]` 注释标记）：
  `App.jsx`（菜单分组、侧栏底部主题控件、移动端底栏移除捐赠、lang 同步）、
  `SettingsView.jsx`（外观分区、支持作者入口）、`i18n.js`（overlay 合并）、`main.jsx`（导入主题层）。
- 菜单中文习惯调整：侧栏按「常用功能 / 系统管理」分组；「捐赠」从移动端底部标签位
  迁移到 设置→支持作者 与侧栏底部弱化的入口；视图 id（dashboard/storage/autoload/
  processes/settings/donate/sources/move_from_usb）**与上游完全一致，未增删路由**。

## 4. 跟进上游更新的维护策略

仓库模型：
```
upstream (itsPLK) ──fetch──> origin (你的fork): main = 跟随上游, cn-fork = 我们的改动
```
```bash
git remote add upstream https://github.com/itsPLK/ps5-payload-manager
git fetch upstream
git checkout main && git merge --ff-only upstream/main
git checkout cn-fork && git rebase main        # 或 merge，二选一
```

冲突预期（按风险排序）：
1. **低**：`theme/`、`locales-overlay/` 为新增文件/目录，永不冲突（但注意 `.gitignore`
   需确保 `src/locales-overlay/*.json` 被提交）。
2. **中**：`App.jsx` / `SettingsView.jsx` / `i18n.js` / `main.jsx` 的 `[CN-FORK]` 注释块——
   rebase 时保留双方语义即可，改动都是局部插入式。
3. **高关注**：上游若**重命名颜色类体系**（如从 zinc 换成 slate）或**拆分视图 id**，需同步
   更新 light-theme.css 映射表与 overlay 词条 key。升级后跑一次第 5 节的视觉审计即可发现。

约定：
- 永远不改 C 后端的路由/配置键/文件路径（升级 ELF 时前端产物直接嵌入，后端行为保持一致）；
- 不修改 `frontend/src/locales`（子模块）；
- 上游版本号变更后，在本文件顶部更新「基线」记录。

## 5. 本地测试/调试环境（Windows 已验证）

前置：Node ≥ 18（实测 v24.19）。PowerShell 下用 `npm.cmd`（npm.ps1 被执行策略拦截）。

```powershell
cd frontend
npm.cmd install                    # 首次（esbuild postinstall 被 allow-scripts 拦截也可正常跑）
git submodule update --init        # 翻译子模块
npm.cmd run dev:mock               # 一键起 mock 后端(:8081) + vite(:5173)
```
- 打开 http://localhost:5173 —— 全部后端 API 由 `mock-server.js` 模拟
  （payload 列表、仓库安装、autoload 时序、SSE 日志流都是真的交互）。
- 无 mock 联调真机：`npm.cmd run dev` 前改 `vite.config.js` 代理端口 8081 → `PS5_IP:8084`。
- 生产构建验证：`npm.cmd run build`（单文件 dist/index.html，约 740KB / gzip 195KB）。
- ESLint：`npm.cmd run lint`。

### 构建真正的 pldmgr.elf（需 Linux 工具链环境）

> 本机（Windows）实测：未安装 Docker；装有 WSL2 (Ubuntu, default)。二选一即可，
> 也可在任意 Linux 机器上完成。前端产物 `dist/index.html` 已由 `make frontend-build`
> 流程嵌入 ELF，所以在 Windows 上先跑 `npm.cmd run build` 生成 dist 再进 Linux 编译也可以。

```bash
make frontend-build                                   # 生成 dist + 占位符替换
docker build -t ps5-payload-sdk -f Dockerfile.sdk .   # 拉取 ps5-payload-sdk
# 注意：Makefile 引用 /opt/ps5-payload-sdk/bin/prospero-clang（旧版工具链名）
#      新版 SDK 用 orbis-clang，需按本地 SDK 版本调整 Makefile CC/STRIP 路径
docker run --rm -v $(pwd):/src -w /src ps5-payload-sdk make all   # 产出 pldmgr.elf
./deploy.sh [PS5_IP]                                  # shutdown → 构建 → socat 推送 :9021
```

WSL2 内等价流程（不用 Docker）：在 Ubuntu 中安装 git/clang-18/protobuf/make 等依赖后，
克隆 `PS5-Payload-SDK` 并将其工具链接到 Makefile 期望的 `/opt/ps5-payload-sdk/bin/prospero-clang`，
再执行 `./build_deps.sh`（构建 libmicrohttpd/mbedTLS/libcurl 静态库）+ `make all`。
