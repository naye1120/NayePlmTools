import express from 'express';
import cors from 'cors';
import http from 'node:http'; // [CN-FORK] PKG Manager 桩服务：真实监听 8844 供本地端到端验证
import net from 'node:net';   // [CN-FORK] 真实 TCP 探活（与实机 pkgmanager.c 探活语义一致）

const app = express();
app.use(cors());
app.use(express.json());

// Log all requests to terminal
app.use((req, res, next) => {
  console.log(`[Mock] ${req.method} ${req.url}`);
  next();
});

const PORT = Number(process.env.PORT) || 8081; // [CN-FORK] 允许用 PORT 环境变量换端口，避开被占用的 8081

let logs = [
  "[PLDMGR] Mock Server initialized",
  "[PLDMGR] System ready",
  "[PLDMGR] Found 3 payloads on storage"
];

const remoteRepository = [
  {
    name: "ftpsrv",
    filename: "ftpsrv_v0.19.elf",
    url: "https://itsplk.github.io/ps5_payloads/payloads/ftpsrv_v0.19.elf",
    description: "A simple FTP server that accepts connections on port 2121",
    version: "v0.19",
    checksum: "e6c1babbfd5e1b766d12b659853b514b9faedf6333cbe8cb514b1a3e79b7ce39"
  },
  {
    name: "kstuff-lite",
    filename: "kstuff-lite_v1.03.elf",
    url: "https://itsplk.github.io/ps5_payloads/payloads/kstuff-lite_v1.03.elf",
    description: "Lite version of kstuff",
    version: "v1.03",
    checksum: "54df47a48d9c5ee4338ef70ba66093908a4f2845e53468bdd7c080b65d7488c1"
  }
];

const communityRepository = [
  {
    name: "PS5 Hen Test",
    filename: "ps5_hen_test_v1.0.elf",
    url: "https://example.com/payloads/ps5_hen_test_v1.0.elf",
    description: "Community hen test payload — no checksum",
    version: "v1.0"
  },
  {
    name: "Debug Tool",
    filename: "debug_tool_v2.1.elf",
    url: "https://example.com/payloads/debug_tool_v2.1.elf",
    description: "Debug utility from community repo",
    version: "v2.1"
  }
];

let lastRepositoryUpdate = Math.floor(Date.now() / 1000);
/* [CN-FORK] mock 里的「默认云端仓库」，与 include/pldmgr.h 的 REPOSITORY_SOURCE_URL 保持一致 */
const MOCK_DEFAULT_REPO_URL = 'https://itsplk.github.io/ps5-payloads-mirror/payloads.json';
/* [CN-FORK] v0.5.1.64 mock 开始真正保存配置：旧实现 /get_config 返回硬编码字面量、/set_config 只打日志
 * 不回存，本地 dev 里设置页每个开关点完都弹回原状，看着就像「开关都不好用」；而缺的四个键
 * （AUTO_BROWSER_OPEN / AUTOLOAD_DELAY / KILL_DISC_PLAYER_ON_STARTUP / SCAN_USB_PAYLOADS）连回显都没有。
 * 默认值与 src/config.c 一致（MULTI_SOURCES_ENABLED / AUTOLOAD_ENABLED 仍留 true 只为让本地首屏数据更丰富，
 * 两个模式的仓库响应格式现在都由下面的 buildRepositoryResponse 真实区分）。 */
const mockConfig = {
  AUTOLOAD_ENABLED: true,
  AUTOLOAD_LIST: "goldhen_v2.4b17.elf,etaHEN_1.8.elf",
  LAST_REPOSITORY_UPDATE: lastRepositoryUpdate,
  AUTO_BROWSER_OPEN: true,
  AUTOLOAD_DELAY: 5,
  KILL_DISC_PLAYER_ON_STARTUP: true,
  SCAN_USB_PAYLOADS: false,
  AUTO_INSTALL_APP: true,
  /* [CN-FORK] v0.5.2.67 默认开多来源，才能列出预置的 repo.630810.xyz（内置官方源已默认关） */
  MULTI_SOURCES_ENABLED: true,
  /* [CN-FORK] v0.5.2.67 默认云端仓库总开关改为默认关（与 src/config.c config_read 默认值一致） */
  DEFAULT_REPO_ENABLED: false
};
/* [CN-FORK] v0.5.1.64 删除 readHardwareEnabled：主页硬件信息功能（/system_stats + stats.c）已整体下线 */

let mockSources = [
  { id: 'default', name: 'Official Repository', url: MOCK_DEFAULT_REPO_URL, removable: false },
  /* [CN-FORK] v0.5.2.67 预置的国内镜像仓库（与后端 sources_seed_defaults 保持一致） */
  { id: 'source_cn', name: 'repo.630810.xyz', url: 'https://repo.630810.xyz/payloads.json', removable: true }
];

let autoloadStatus = {
  remaining: 10,
  total: 2,
  done: 0,
  current: "IDLE",
  list: "goldhen_v2.4b17.elf,etaHEN_1.8.elf"
};

const autoloadList = autoloadStatus.list.split(',');
let simulationTicks = 0;

setInterval(() => {
  if (autoloadStatus.remaining > 0) {
    autoloadStatus.remaining--;
    if (autoloadStatus.remaining === 0) {
      autoloadStatus.current = autoloadList[0];
      logs.push(`[PLDMGR] Autoload sequence started: ${autoloadList[0]}`);
    }
  } else if (autoloadStatus.remaining === 0 && autoloadStatus.current !== "DONE" && autoloadStatus.current !== "IDLE") {
    simulationTicks++;
    if (simulationTicks >= 3) { // 3 seconds per payload simulation
      simulationTicks = 0;
      autoloadStatus.done++;
      if (autoloadStatus.done < autoloadStatus.total) {
        autoloadStatus.current = autoloadList[autoloadStatus.done];
        logs.push(`[PLDMGR] Autoloading: ${autoloadStatus.current}`);
      } else {
        autoloadStatus.current = "DONE";
        logs.push(`[PLDMGR] Autoload sequence complete`);
      }
    }
  }
}, 1000);

// --- API Routes ---

app.get('/getip', (req, res) => {
  res.send('127.0.0.1');
});

app.get('/version', (req, res) => {
  res.send('0.5.2.92'); // [CN-FORK] 与 pldmgr.h MENU_VERSION 保持一致
});

/* [CN-FORK] v0.5.1.64 删 /game_icon mock：后端该端点随主页硬件信息（当前游戏探测）一起下线 */

/* [CN-FORK] PKG Manager 内置集成 mock：状态/启动/上传（实机由 pkgmanager.c 探活 127.0.0.1:8844 + elfldr 拉起）。
 * 这里用「真实端口」模拟：launch 拉起一个监听 8844 的桩 HTTP 服务、stop 关掉；running/alive 全部来自
 * 真实 TCP 探活——与实机语义一致（端口活 = 服务已就绪），前端「端口存活即直接打开」逻辑可端到端验证 */
let pkgStub = null;
let pkgInstalled = '-';

const pkgPortAlive = () => new Promise((resolve) => {
  const sock = net.connect({ host: '127.0.0.1', port: 8844 });
  const done = (ok) => { sock.destroy(); resolve(ok); };
  sock.setTimeout(400, () => done(false));
  sock.once('connect', () => done(true));
  sock.once('error', () => done(false));
});

const pkgStubStart = () => new Promise((resolve, reject) => {
  if (pkgStub) return resolve();
  const srv = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end('<!doctype html><title>PKG Manager (mock stub)</title><body style="margin:0;height:100vh;display:flex;align-items:center;justify-content:center;background:#101014;color:#e4e4e7;font:600 16px/1.6 system-ui,sans-serif">PKG Manager mock stub · port 8844</body>');
  });
  srv.once('error', (err) => { pkgStub = null; reject(err); });
  srv.listen(8844, '127.0.0.1', () => { pkgStub = srv; resolve(); });
});

app.get('/pkgmanager_status', async (req, res) => {
  res.json({
    running: await pkgPortAlive(),
    port: 8844,
    embedded_version: '1.4.0-CN',
    installed_version: pkgInstalled,
    file: pkgInstalled !== '-',
  });
});
app.get('/pkgmanager_launch', async (req, res) => {
  try {
    await pkgStubStart();
  } catch (err) {
    /* 8844 被其它进程占用：对前端而言「端口活=服务可达」，不算失败 */
    if (!(await pkgPortAlive())) {
      return res.json({ ok: false, message: `mock 拉起失败: ${err.code || err.message}` });
    }
  }
  if (pkgInstalled === '-') pkgInstalled = '1.4.0-CN';
  res.json({ ok: true, message: 'OK' });
});
// [CN-FORK] 关闭 PKG Manager mock（实机由 pkgmanager.c 按进程名 SIGKILL）
app.get('/pkgmanager_stop', async (req, res) => {
  const was = await pkgPortAlive();
  if (pkgStub) {
    await new Promise((resolve) => pkgStub.close(resolve));
    pkgStub = null;
  }
  if (!was) {
    return res.json({ ok: true, message: 'PKG Manager was not running' });
  }
  res.json({ ok: true, message: 'SIGKILL sent to 1 process(es)' });
});
app.post('/pkgmanager_upload', (req, res) => {
  req.resume();
  req.on('end', () => {
    const m = /v([0-9][0-9A-Za-z.-]*)/.exec(req.query.filename || '');
    pkgInstalled = m ? m[1] : 'unknown';
    res.json({ ok: true, version: pkgInstalled });
  });
});

// [CN-FORK] 常用入口 mock：内存清单 + 新增/删除（实机由 quicklinks.c 读写 /data 下 json 并 TCP 探活）
/* [CN-FORK] program 字段：本机程序类入口（实机由 quicklinks.c 按「本机 host + 8844 端口」识别），
 * 前端点击时以「8844 端口存活」为准：存活直接打开、不存活试着拉起；本条目 alive 同样来自真实探活 */
/* [CN-FORK] v0.5.2.83 通用「不活即拉起」预览：本机 payload 端口同样用真实 TCP 探活表示存活，
 * /loadpayload: 命中已知程序时延迟几秒起一个监听桩，模拟「进程起来 → 端口转活」。 */
const tcpAlive = (host, port) => new Promise((resolve) => {
  const sock = net.connect({ host, port });
  const done = (ok) => { sock.destroy(); resolve(ok); };
  sock.setTimeout(400, () => done(false));
  sock.once('connect', () => done(true));
  sock.once('error', () => done(false));
});

/* 归一化：只留字母（丢掉版本号/变体/中文/符号），与前端 CommonEntriesView 同一套匹配语义 */
const norm = (s) => String(s || '').toLowerCase().replace(/\.elf$/, '').replace(/[^a-z]/g, '');
const progHit = (a, b) => {
  const x = norm(a), y = norm(b);
  return !!x && !!y && Math.min(x.length, y.length) >= 3 && (x.startsWith(y) || y.startsWith(x));
};

/* 桩程序表：目录/文件名可识别名 → 监听端口（新 payload 要预览就在加一行） */
const MOCK_PROGS = { shadowmountplus: { port: 10101 } };
const progStubs = new Map(); // 端口 → http.Server

const progStubStart = (port) => new Promise((resolve, reject) => {
  if (progStubs.has(port)) return resolve();
  const srv = http.createServer((req, res) => {
    res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(`<!doctype html><title>Payload stub :${port} (mock)</title><body style="margin:0;height:100vh;display:flex;align-items:center;justify-content:center;background:#101014;color:#e4e4e7;font:600 16px/1.6 system-ui,sans-serif">本机 payload 桩服务 · 端口 ${port}</body>`);
  });
  srv.once('error', reject);
  srv.listen(port, '127.0.0.1', () => { srv.removeListener('error', reject); progStubs.set(port, srv); resolve(); });
});

/* /loadpayload: 的路径 → 桩程序端口；匹配不到返回 null（前端会按普通链接直接打开） */
const stubPortForPath = (path) => {
  const seg = String(path).split('/');
  const dir = seg[seg.length - 2] || '';
  const file = seg[seg.length - 1] || '';
  for (const [name, cfg] of Object.entries(MOCK_PROGS))
    if (progHit(dir, name) || progHit(file, name)) return cfg.port;
  return null;
};

const quicklinks = [
  /* 用 127.0.0.1:8844 对齐本机桩服务，预览时点卡片可直接打开桩页面（实机 quicklinks.c 会把回环
   * 地址改写为 PS5 当前 LAN IP；本机主机名/回环/LAN IP 三种形式都算程序类入口） */
  { name: 'PKG Manager', url: 'http://127.0.0.1:8844', src: 'manual', program: 'pkgmanager' },
  { name: 'NAS 面板', url: 'http://192.168.1.10:5000', src: 'manual' },
  /* [CN-FORK] 模拟 app.db 磁贴链接自动收录条目（实机由 quicklinks.c 同步 pprDeeplinkUri，
   * 且会把 127.0.0.1 回环地址改写为本机当前 LAN IP，故这里直接用示例 IP） */
  { name: 'NayePlmTools 磁贴', url: 'http://192.168.1.214:8911/', src: 'auto' },
  /* [CN-FORK] v0.5.2.83 灰点条目：未拉起时 10101 无监听 → 探活不通，点卡片走通用拉起流程 */
  { name: 'Shadow Mount+ 自动', url: 'http://127.0.0.1:10101/', src: 'auto' },
];
app.get('/quicklinks', async (req, res) => {
  const pkgAlive = await pkgPortAlive();
  const entries = [];
  for (const e of quicklinks) {
    let alive;
    if (e.program === 'pkgmanager') {
      alive = pkgAlive;
    } else {
      /* 与实机同源语义：直接探 URL 的 host:port（NAS 面板等外网地址在本机必然连不通 = 灰点） */
      const u = new URL(e.url);
      alive = await tcpAlive(u.hostname, Number(u.port) || (u.protocol === 'https:' ? 443 : 80));
    }
    entries.push({ ...e, alive });
  }
  res.json({ entries });
});
app.get('/quicklinks_add', (req, res) => {
  const { name, url } = req.query;
  if (!name || !/^https?:\/\//.test(url || '')) {
    return res.status(400).json({ ok: false, message: '参数不合法' });
  }
  if (quicklinks.some((e) => e.url === url)) {
    return res.status(400).json({ ok: false, message: '该 URL 已存在' });
  }
  quicklinks.push({ name, url, src: 'manual' });
  res.json({ ok: true, message: 'OK' });
});
app.get('/quicklinks_remove', (req, res) => {
  const idx = parseInt(req.query.index, 10);
  if (!(idx >= 0 && idx < quicklinks.length)) {
    return res.status(400).json({ ok: false, message: '无效的条目序号' });
  }
  quicklinks.splice(idx, 1);
  res.json({ ok: true, message: 'OK' });
});

// [CN-FORK] 温控只读 mock（预览用；实机由 thermal.c 读 sceKernel* 传感器）
app.get('/thermal_stats', (req, res) => {
  // [CN-FORK] 预览用带抖动回值，便于肉眼确认「自动刷新」在跑（实机由 thermal.c 的 5s 快照提供）
  const j = (base, span) => base + Math.floor(Math.random() * span);
  res.json({ enabled: true, cpu: { temp: j(50, 8) }, gpu: { temp: j(55, 8) }, ssd: { temp: j(44, 6) }, fan: { duty: j(35, 10), max: 100 } });
});

// [CN-FORK] 游戏列表 mock（预览用；实机由 game_mgr.c 解析 app.db + 扫描 dump）
const mockGames = [
  { tid: 'PPSA01234', name: '战神 诸神黄昏', src: 'installed', fmt: 'ffpkg', size: 68500000000 },
  { tid: 'PPSA00802', name: '漫威蜘蛛侠 2', src: 'installed', fmt: 'ffpkg', size: 94000000000 },
  { tid: 'CUSA01111', name: '血源诅咒（PS4 兼容）', src: 'dump', fmt: 'folder', size: 41000000000 },
  { tid: 'PPSA02000', name: 'LZ4 实时解压游戏', src: 'dump', fmt: 'lz4', size: 71000000000 },
  { tid: 'PPJB01010', name: '演示合集（内置 pkg 无体积）', src: 'installed', fmt: 'pkg', size: 0 },
  { tid: 'EMUA90001', name: '无图标的游戏（验证回退）', src: 'dump', fmt: 'exfat', size: 120000000000 },
  { tid: 'PAUS00100', name: 'Pulse Demo（无图标）', src: 'installed', fmt: 'pkg', size: 0 },
  { tid: 'PPSA01500', name: '容器镜像游戏', src: 'dump', fmt: 'ffpfsc', size: 145465737216 },
];
app.get('/game_list', (req, res) => {
  res.json({ games: mockGames, count: mockGames.length });
});

// [CN-FORK] 游戏图标 mock：返回带 TID 的占位 SVG；两个 tid 故意 404，验证前端 onError 回退 Gamepad2
app.get('/game_icon', (req, res) => {
  const raw = String(req.query.tid || '');
  const tid = /^[A-Za-z0-9]{0,16}$/.test(raw) ? raw : '';
  if (!tid || tid === 'EMUA90001' || tid === 'PAUS00100') {
    return res.status(404).type('text/plain').send('not found');
  }
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="256" height="256">`
    + `<rect width="256" height="256" fill="#1e293b"/><circle cx="128" cy="100" r="46" fill="#2563eb"/>`
    + `<text x="128" y="200" font-family="monospace" font-size="24" fill="#e2e8f0" text-anchor="middle">${tid}</text></svg>`;
  res.type('image/svg+xml').send(svg);
});

// [CN-FORK] 风扇温控 mock（预览用）：开关只置 enabled+清 applied；apply 才置生效。
const fanCfg = { supported: true, enabled: false, applied: false, target: 72, hw_target: 91, min: 60, max: 85, interval: 7 };
app.get('/fan_control', (req, res) => {
  if (req.query.set_enabled !== undefined) {
    fanCfg.enabled = req.query.set_enabled === '1' || req.query.set_enabled === 'true';
    fanCfg.applied = false; // 开启后默认失效
    return res.json({ ok: true, message: 'switch_set', ...fanCfg });
  }
  if (req.query.apply !== undefined) {
    const tv = parseInt(req.query.target, 10);
    if (!Number.isNaN(tv)) fanCfg.target = Math.min(fanCfg.max, Math.max(fanCfg.min, tv));
    fanCfg.enabled = true;
    fanCfg.applied = true;
    fanCfg.hw_target = fanCfg.target; // 模拟下发成功
    return res.json({ ok: true, message: 'applied', ...fanCfg });
  }
  const { supported, enabled, applied, target, hw_target, min, max, interval } = fanCfg;
  res.json({ supported, enabled, applied, target, hw_target, min, max, interval });
});

/* [CN-FORK] v0.5.2.83 改用实机 payload_mgr.c 的真实布局：payloads/<目录>/<文件>.elf，
 * meta 以文件名为键并带 display_name——前端按名称匹配拉起目标时读的就是这两处。 */
app.get('/list_payloads', (req, res) => {
  res.json({
    payloads: [
      "/data/nayeplmtools/payloads/GoldHEN_v2.4b17/goldhen_v2.4b17.elf",
      "/data/nayeplmtools/payloads/etaHEN_1.8/etaHEN_1.8.elf",
      "/data/nayeplmtools/payloads/kstuff/kstuff.elf",
      "/data/nayeplmtools/payloads/ShadowMountPlus/shadowmountplus.elf",
      "/mnt/usb0/payloads/linux_loader/linux_loader.elf"
    ],
    meta: {
      // payloads with no entry here = local/direct upload (no badge shown)
      "kstuff.elf": {
        display_name: "kstuff",
        source_name: "Community Payloads",
        install_source: "repository",
        install_source_detail: "https://example.com/community-payloads.json"
      },
      "shadowmountplus.elf": { display_name: "Shadow Mount+" }
    }
  });
});

/* [CN-FORK] v0.5.2.83 进程列表 mock（实机 process_mgr.c）：通用拉起前先查同名进程，
 * 避免对「已在跑但只监听 127.0.0.1」的程序重复拉起叠出第二个实例。未拉起的 ShadowMountPlus 不在表内。 */
const mockProcs = ['elfldr.elf', 'NayePlmTools.elf', 'pkgmgr.elf', 'dbst.elf', 'kstuff.elf'];
app.get('/processes_list', (req, res) => {
  res.json({ processes: mockProcs.map((name, i) => ({ pid: 89 + i, name })) });
});

app.get('/autoload_status', (req, res) => {
  res.json(autoloadStatus);
});

app.get('/get_config', (req, res) => {
  mockConfig.LAST_REPOSITORY_UPDATE = lastRepositoryUpdate;
  res.json(mockConfig);
});

/* [CN-FORK] v0.5.1.65 跟后端 repository_list_json / sources_multi_repository_list_json 保持同一套开关语义：
 * 默认云端仓库关掉后，官方源整组不输出；单来源模式下直接回空清单（cache_status=disabled）。 */
const buildRepositoryResponse = () => {
  /* [CN-FORK] v0.5.2.67 DEFAULT_REPO_ENABLED 默认关，回退语义与后端一致：仅显式为 true 才输出官方源 */
  const defaultOn = mockConfig.DEFAULT_REPO_ENABLED === true;
  if (!mockConfig.MULTI_SOURCES_ENABLED) {
    return defaultOn
      ? { payloads: remoteRepository, last_update: lastRepositoryUpdate, cache_status: 'ok', repo_url: MOCK_DEFAULT_REPO_URL }
      : { payloads: [], last_update: lastRepositoryUpdate, cache_status: 'disabled' };
  }
  const groups = [];
  if (defaultOn) {
    groups.push({
      id: 'default',
      name: 'Official Repository',
      last_update: lastRepositoryUpdate,
      error: false,
      payloads: remoteRepository.map(p => ({ ...p, source_id: 'default', source_name: 'Official Repository' }))
    });
  }
  /* [CN-FORK] v0.5.2.67 预置国内镜像仓库组，与后端 sources_seed_defaults / mockSources 保持一致 */
  groups.push({
    id: 'source_cn',
    name: 'repo.630810.xyz',
    last_update: lastRepositoryUpdate,
    error: false,
    payloads: communityRepository.map(p => ({ ...p, source_id: 'source_cn', source_name: 'repo.630810.xyz' }))
  });
  return { sources: groups };
};

app.get('/repository_payloads', (req, res) => {
  res.json(buildRepositoryResponse());
});

app.get('/repository_refresh', (req, res) => {
  lastRepositoryUpdate = Math.floor(Date.now() / 1000);
  logs.push(`[PLDMGR] Repository manually refreshed`);
  res.json(buildRepositoryResponse());
});

app.get('/sources_list', (req, res) => {
  /* [CN-FORK] v0.5.1.65 默认源不从列表里抹掉（下标就是缓存文件名的语义与后端一致），而是带 enabled=false */
  const defaultOn = mockConfig.DEFAULT_REPO_ENABLED === true;
  res.json({ sources: mockSources.map(s => ({ ...s, enabled: s.id === 'default' ? defaultOn : true })) });
});

app.post('/sources_set', (req, res) => {
  const { sources } = req.body;
  if (Array.isArray(sources)) {
    mockSources = sources;
    logs.push(`[PLDMGR] Sources updated: ${sources.length} sources`);
  }
  res.send('OK');
});

app.get('/sources_add', (req, res) => {
  const { url } = req.query;
  if (!url) return res.status(400).json({ ok: false, message: 'Missing url' });
  // Mock: always succeed with a fake source name derived from the URL
  const fakeName = `Community Source (${new URL(url).hostname})`;
  const newSource = { id: `source_${Date.now()}`, name: fakeName, url, removable: true };
  mockSources.push(newSource);
  logs.push(`[PLDMGR] Source added: ${fakeName}`);
  res.json({ ok: true, name: fakeName });
});

app.get('/sources_remove', (req, res) => {
  const idx = parseInt(req.query.index);
  if (isNaN(idx) || idx <= 0 || idx >= mockSources.length) {
    return res.status(400).json({ ok: false, message: 'Invalid index or cannot remove default source' });
  }
  const removed = mockSources.splice(idx, 1);
  logs.push(`[PLDMGR] Source removed: ${removed[0]?.name}`);
  res.json({ ok: true, message: 'OK' });
});

app.get('/repository_install', (req, res) => {
  const { filename } = req.query;
  if (!filename) {
    return res.status(400).json({ ok: false, message: 'Missing filename' });
  }
  logs.push(`[PLDMGR] Repository install requested: ${filename}`);
  res.json({ ok: true, message: `Installed ${filename}` });
});

app.post('/set_config', (req, res) => {
  console.log('Received Config:', req.body);
  logs.push(`[PLDMGR] Config updated: ${JSON.stringify(req.body)}`);
  /* [CN-FORK] v0.5.1.64 存回内存，下一次 /get_config 就是新值（LAST_REPOSITORY_UPDATE 由服务端自己算，不接受前端回写） */
  if (req.body && typeof req.body === 'object') {
    for (const [key, value] of Object.entries(req.body)) {
      if (key === 'LAST_REPOSITORY_UPDATE' || value === undefined) continue;
      mockConfig[key] = value;
    }
  }
  res.send('OK');
});

app.get(/^\/loadpayload:(.*)/, (req, res) => {
  const path = req.params[0];
  logs.push(`[PLDMGR] Executing payload: ${path}`);
  /* [CN-FORK] v0.5.2.83 模拟拉起：已知桩程序延迟 1.5s 起监听端口 + 登记进程名，
   * 让前端「拉起 → 轮询探活转绿 → 打开」的完整链路可端到端看到 */
  const port = stubPortForPath(decodeURIComponent(path));
  if (port) {
    setTimeout(() => {
      progStubStart(port).catch((err) => console.log(`[Mock] stub listen ${port} failed: ${err.code || err.message}`));
      const pname = String(path).split('/').pop();
      if (pname && !mockProcs.includes(pname)) mockProcs.push(pname);
    }, 1500);
  }
  res.send('OK');
});

app.get('/manage\\:delete', (req, res) => {
  const filename = req.query.filename;
  logs.push(`[PLDMGR] Deleted payload: ${filename}`);
  res.send('OK');
});

app.post('/manage\\:upload', (req, res) => {
  const filename = req.query.filename;
  logs.push(`[PLDMGR] Uploaded payload: ${filename}`);
  res.send('OK');
});

app.get('/abort', (req, res) => {
  logs.push(`[PLDMGR] Autoload sequence aborted by user`);
  autoloadStatus.remaining = -1;
  autoloadStatus.current = "IDLE";
  res.send('OK');
});

app.get('/autoload_clear', (req, res) => {
  logs.push(`[PLDMGR] Autoload status cleared`);
  autoloadStatus.done = 0;
  autoloadStatus.current = "IDLE";
  autoloadStatus.remaining = -1;
  res.send('OK');
});

app.get('/shutdown', (req, res) => {
  logs.push(`[PLDMGR] System shutdown requested`);
  res.send('Shutting down...');
  process.exit(0);
});

// SSE for logs
app.get('/events', (req, res) => {
  res.setHeader('Content-Type', 'text/event-stream');
  res.setHeader('Cache-Control', 'no-cache');
  res.setHeader('Connection', 'keep-alive');

  const sendLog = (data) => {
    res.write(`data: ${data}\n\n`);
  };

  // Send history
  logs.forEach(sendLog);

  // Poll for new logs
  let lastCount = logs.length;
  const interval = setInterval(() => {
    if (logs.length > lastCount) {
      for (let i = lastCount; i < logs.length; i++) {
        sendLog(logs[i]);
      }
      lastCount = logs.length;
    }
  }, 500);

  req.on('close', () => {
    clearInterval(interval);
  });
});

app.listen(PORT, '127.0.0.1', () => {
  console.log(`\x1b[36m%s\x1b[0m`, `--- Payload Manager Mock Backend ---`);
  console.log(`Running at http://localhost:${PORT}`);
  console.log(`Proxy your Vite requests to this port to test the frontend locally.`);
});
