/* [CN-FORK] v0.5.2.99 eslint 不认 Node 全局（与 vite.config.js 同一处理），这里只声明用到的两个 */
/* global process, Buffer */
import express from 'express';
import cors from 'cors';
import http from 'node:http'; // [CN-FORK] PKG Manager 桩服务：真实监听 8844 供本地端到端验证
import net from 'node:net';   // [CN-FORK] 真实 TCP 探活（与实机 pkgmanager.c 探活语义一致）
import { readFileSync } from 'node:fs'; // [CN-FORK] v0.5.2.113 /version 从 pldmgr.h 现读版本号
import zlib from 'node:zlib'; // [CN-FORK] v0.5.2.112 磁贴背景要真转 DXT1 DDS：PNG 解压用（Node 自带）
/* [CN-FORK] v0.5.2.113 /version 的词条从 pldmgr.h 现读，不再手抄。
 * 病因：这里原来写死 '0.5.2.112' 并附了一句「与 pldmgr.h MENU_VERSION 保持一致」——
 * 靠人去保持一致的注释，等于每次升版本都埋一颗雷：忘了改的话，mock 会报一个
 * **已经不存在**的版本号，而前端在挂载时会拿它与构建时烧入的 __APP_VERSION__ 比对
 * （见 src/main.jsx 的版本握手），对不上就触发一次「版本已更新」的刷新 —— 本地联调
 * 会莫名其妙地刷页面，且原因看不出来。改成读头文件（与 vite.config.js 同一手法）。 */
const MENU_VERSION = (() => {
  try {
    const src = readFileSync(new URL('../include/pldmgr.h', import.meta.url), 'utf8');
    const m = src.match(/#define\s+MENU_VERSION\s+"([^"]+)"/);
    return m ? m[1] : 'dev';
  } catch {
    return 'dev';
  }
})();

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
  res.send(MENU_VERSION); // [CN-FORK] v0.5.2.113 现读 pldmgr.h，不再手抄（见文件头）
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
/* [CN-FORK] v0.5.2.131 ShadowMount+ 探活 mock：与实机同一语义——只探 loopback 10101，
 * 端口活=服务已就绪。真实 TCP 探活，不用假开关：/loadpayload: 命中 ShadowMountPlus 路径时
 * 会起一个监听 10101 的桩，探活随之转活，前端「先探 → 不活则拉 → 等转活 → 打开」整条链路
 * 在预览里就能端到端验证。 */
app.get('/smp_status', async (req, res) => {
  const sock = net.connect({ host: '127.0.0.1', port: 10101 });
  const done = (ok) => { sock.destroy(); res.json({ running: ok, port: 10101 }); };
  sock.setTimeout(400, () => done(false));
  sock.once('connect', () => done(true));
  sock.once('error', () => done(false));
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

/* [CN-FORK] v0.5.2.99 常用配置「账号 / 远程游玩配对」板块 mock。
 * 接口形状逐条对齐并入后的真接口（src/psaccounts/api.c + accounts.c/avatar.c），状态在内存里
 * 真的会翻转：激活 / 登入登出 / 改名 / 换 Account ID 都会改到位，用来把每个按钮的前端逻辑走一遍。
 * 不写注册表、不碰文件 —— 真机行为只由 payload 侧决定。 */
/* [CN-FORK] v0.5.2.99 PIN 有效期与实机同源一档：src/psaccounts/accounts.c 的 PIN_TTL_SEC=180 */
const ACCT_PIN_TTL = 180
const acct = {
  name: 'User11', activated: true, signedIn: true, autoLogin: true, slot: 1,
  userId: 438122023, userIdHex: '0x1A1D3627',
  accountId: '0x7ed3586971779f76', derivedId: '0x7ed3586971779f76',
  pendingRecords: 2, pin: null, safetyDirty: true,
}
const b64OfHex = (raw) => {
  const be = []
  for (let i = 0; i < 8; i++) be.push(parseInt(raw.slice(i * 2, i * 2 + 2), 16))
  return Buffer.from(be.slice().reverse()).toString('base64')
}
const decOfHex = (raw) => BigInt('0x' + raw).toString(10)
const acctObj = () => {
  const raw = acct.accountId.replace(/^0x/, '')
  return {
    userId: acct.userId, userIdHex: acct.userIdHex, name: acct.name, slot: acct.slot,
    activated: acct.activated, signedIn: acct.signedIn, current: true, autoLogin: acct.autoLogin,
    loginFlag: acct.signedIn ? 6 : 4098,
    state: acct.signedIn ? 'signed-in' : 'activated-offline',
    pendingRecords: acct.pendingRecords,
    accountId: acct.activated ? acct.accountId : '',
    accountIdB64: acct.activated ? b64OfHex(raw) : '',
    accountIdDec: acct.activated ? decOfHex(raw) : '',
    derivedId: acct.derivedId,
  }
}
const acctPinLine = () => {
  const raw = acct.accountId.replace(/^0x/, '')
  return {
    pin: acct.pin.pinRaw.slice(0, 4) + ' ' + acct.pin.pinRaw.slice(4),
    pinRaw: acct.pin.pinRaw, expiresIn: ACCT_PIN_TTL,
    remaining: Math.max(0, Math.round((acct.pin.deadline - Date.now()) / 1000)),
    userId: acct.userId, name: acct.name, slot: acct.slot,
    activated: acct.activated, signedIn: acct.signedIn,
    accountId: acct.activated ? acct.accountId : '',
    accountIdB64: acct.activated ? b64OfHex(raw) : '',
  }
}

app.get('/api/accounts', (req, res) => {
  /* 第二条是 current:false：验证界面挑的确实是前台账号，而不是清单里的第一个 */
  const other = {
    ...acctObj(), userId: 911234567, userIdHex: '0x36425207', name: 'SecondUser', slot: 2,
    current: false, autoLogin: false, activated: false, signedIn: false, pendingRecords: 0,
    accountId: '', accountIdB64: '', accountIdDec: '', derivedId: '0x2f6c0a1b3c4d5e6f',
    loginFlag: 4098, state: 'activated-offline',
  }
  res.json({ ok: true, mainUserId: acct.userId, currentUserId: acct.userId, accounts: [acctObj(), other] })
})

app.get('/api/accounts/:action', (req, res) => {
  const a = req.params.action
  if (a === 'preview') {
    return res.json({
      ok: true, action: 'signin', slot: acct.slot, mainUserId: acct.userId, owner: acct.userId,
      pendingForOwner: acct.pendingRecords, targetIsMain: true, writesBaseSlot: false,
      baseSlotKeys: 0, targetSlotKeys: 22, regKeys: 22, npFiles: 2, ents: [],
    })
  }
  if (a === 'activate') {
    const id = req.query.accountId
    if (id) {
      const s = String(id).toLowerCase()
      acct.accountId = s.startsWith('0x') ? s : '0x' + BigInt(s).toString(16).padStart(16, '0')
    }
    acct.activated = true
  }
  if (a === 'deactivate') acct.activated = false
  if (a === 'signout') acct.signedIn = false
  if (a === 'signin') {
    /* 服务端那道 confirm=online 闸在 mock 里同样要生效，否则测不出「漏参数=409」 */
    if (req.query.confirm !== 'online') {
      return res.status(409).json({ ok: false, needConfirm: 'online', message: 'need confirm' })
    }
    acct.signedIn = true
  }
  if (a === 'rename') {
    const to = req.query.to
    if (to === 'Taken') return res.json({ ok: false, message: 'rejected: "Taken" already occupies slot 2' })
    if (to) acct.name = String(to)
  }
  const msg = {
    activate: 'activated ' + acct.name, deactivate: 'deactivated ' + acct.name,
    signout: acct.name + ' signed out', signin: acct.name + ' signed in', rename: 'renamed',
  }[a]
  res.json({ ok: true, message: msg || 'ok', account: acctObj() })
})

app.get('/api/safety', (req, res) => {
  res.json({
    ok: true, mode: 'status', dir: '/data/offact/backup', line: '2 reg, 1 file', writable: true,
    regBackups: acct.safetyDirty ? 2 : 0, fileBackups: acct.safetyDirty ? 1 : 0,
    regRestored: 0, filesRestored: 0, message: '',
    entries: [{ ent: '0x07800500', type: 'bin', declared: 8, owner: acct.userId }],
  })
})
app.get('/api/safety/restore', (req, res) => {
  acct.safetyDirty = false
  res.json({ ok: true, message: 'restored 2 registry value(s) and 1 file(s); backup log cleared — reboot to apply' })
})

app.get('/api/remoteplay/pin', (req, res) => {
  acct.pin = { pinRaw: String(Math.floor(10000000 + Math.random() * 89999999)), deadline: Date.now() + ACCT_PIN_TTL * 1000 }
  res.json({ ok: true, status: 'waiting', ...acctPinLine(), message: '配对要填 PIN + Account ID。这一步只读。' })
})
app.get('/api/remoteplay/pin/status', (req, res) => {
  if (!acct.pin) {
    return res.json({ ok: false, status: 'idle', message: '当前没有进行中的配对 —— 点「生成 PIN」重新来一条' })
  }
  const remain = Math.round((acct.pin.deadline - Date.now()) / 1000)
  if (remain <= 0) {
    acct.pin = null
    return res.json({ ok: true, status: 'expired', message: 'PIN 已过期，会话已收尾（0x0）' })
  }
  res.json({ ok: true, status: 'waiting', ...acctPinLine(), stat: 0,
    message: '正在等你在 Chiaki 里填 PIN + Account ID（还剩 ' + remain + ' 秒）' })
})

/* [CN-FORK] v0.5.2.109 头像在 mock 里也走一个真闭环：上传的字节留在内存里，
 * `/api/avatar?id=` 原样吐回来。原来这里写死 404，于是本地预览里「上传成功但缩略图
 * 永远是首字母占位」—— 与实机上那个「上传没反应」长得一模一样，白白误导排查方向。
 * 现在本地就能验完「选图 → 裁剪 → 上传 → 应用 → 缩略图刷新」整条链。 */
let mockAvatarBuf = null
app.get('/api/avatar', (req, res) => {
  if (!req.query.id) return res.json({ inputDir: '/data/ps5-account-tools/avatar/in', workDir: '/data/ps5-account-tools/avatar/work', sizes: [64, 128, 260, 440] })
  if (!mockAvatarBuf) return res.status(404).json({ ok: false, error: 'no avatar for that user' })
  res.set('Content-Type', 'image/png')
  res.set('Cache-Control', 'no-cache')
  res.send(mockAvatarBuf)
})
app.post('/api/avatar/upload', (req, res) => {
  const chunks = []
  req.on('data', (c) => { chunks.push(c) })
  req.on('end', () => {
    mockAvatarBuf = Buffer.concat(chunks)
    res.json({
      ok: true, source: 'mock', size: mockAvatarBuf.length, sourceWidth: 440, sourceHeight: 440,
      mode: req.query.mode || 'crop', sizes: [64, 128, 260, 440], workDir: '/data/ps5-account-tools/avatar/work',
    })
  })
})
app.get('/api/avatar/apply-current-user', (req, res) => {
  res.json({
    ok: true, dest: '/system_data/priv/cache/profile/0x1A1D3627', user: acct.name + ' (0x1A1D3627)',
    copied: 11, failed: 0, safety: '1 file backed up', files: [],
  })
})

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
    res.end(`<!doctype html><title>Payload stub :${port} (mock)</title><body style="margin:0;height:100vh;display:flex;align-items:center;justify-content:center;background:#101014;color:#e4e4e7;font:600 16px/1.6 system-ui,sans-serif">本机 payload 桩服务 · 端口 ${port}<script>document.body.insertAdjacentHTML('beforeend','<p>SMP_STUB_LOADED</p>')</script></body>`);
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
/* [CN-FORK] v0.5.2.133 mock 里也照抄后端那份 60s 快照缓存，
 * 否则「刷新按钮跳过缓存」这条断言在本地永远绿（mock 每次都现算，看不出差别）。
 * force=1 → 重建并刷新时间戳；不带参数 → 60s 内直接回快照。
 * /_game_list_cache 是测试钩子：读当前快照，不触发重建（和后端行为一致）。 */
const LIST_TTL_MS = 60 * 1000;
let listSnap = null;
let listSnapAt = 0;

/* [CN-FORK] v0.5.2.136 模拟真机「列表不扫盘 + 后台预热体积」这条时序。
 *
 * 为什么必须模拟：真机上 folder/dump 游戏的真实体积只能靠递归 du（一款上万文件），
 * 所以列表**只读体积缓存**（game_mgr.c 的 gm_dir_size_peek），未命中先留空，同时起
 * 后台线程扫、扫完作废快照，响应里带 warming:true 让前端自动重拉。
 * 旧 mock 直接给 folder 游戏塞了真实 size，把「列表不扫盘」这个前提整个抹掉了 ——
 * 于是 `.135` 那个「磁贴拿 app.db 的 size 列兜底」的 bug 在本地**永远复现不出来**
 * （真机上 132 GB 的博德之门 3 被显示成 1.22 GB，本地却一直是对的）。
 *
 * 这里照抄后端口径：镜像(exfat/ffpkg/ffpfsc) = 镜像文件大小（一次 stat，列表就有）；
 * pkg = 体积 0，只有 app.db 的 size 列可兜底；folder/lz4 = 预热过才有。 */
const warmSizes = new Map()   /* tid → 预热出来的真实体积 */
let warmTimer = null

function buildGameList() {
  const pending = []
  const games = mockGames
    .filter((g) => !gameState.deleted.has(g.tid))
    .map((g) => {
      const out = { ...g, name: gameState.renamed[g.tid] || g.name };
      if (gameState.icon[g.tid]) out.icon = gameState.icon[g.tid];
      if (gameState.bg[g.tid]) out.bg = gameState.bg[g.tid];
      const isPkg = g.fmt === 'pkg';
      const isFolder = g.fmt === 'folder' || g.fmt === 'lz4';
      /* 与 buildDetail() 同一算法：只有内置 PKG 才有 app.db 的 size 列。 */
      out.appdb_size = isPkg ? 3145728 + (g.tid.charCodeAt(6) % 7) * 1048576 : 0;
      if (isFolder) {
        const real = warmSizes.get(g.tid);
        out.size = real || 0;
        if (!real) pending.push(g.tid);
      } else if (isPkg) {
        out.size = 0;
      }
      return out;
    });
  if (pending.length && !warmTimer) {
    /* 后台「扫盘」：3 秒后把真实体积填上并作废快照（对应后端预热线程收尾时
     * g_game_cache_len=0）—— 下一次 /game_list 就带真实体积、warming=false。 */
    warmTimer = setTimeout(() => {
      for (const tid of pending) {
        const g = mockGames.find((x) => x.tid === tid);
        if (g) warmSizes.set(tid, g.size);
      }
      listSnap = null;
      warmTimer = null;
    }, 3000);
  }
  return { games, count: games.length, warming: pending.length > 0 };
}

app.get('/game_list', (req, res) => {
  const force = req.query.force === '1';
  const now = Date.now();
  if (!force && listSnap && now - listSnapAt < LIST_TTL_MS) return res.json(listSnap);
  listSnap = buildGameList();
  listSnapAt = now;
  res.json(listSnap);
});

app.get('/_game_list_cache', (req, res) => res.json(listSnap));

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

/* ══════════════════════════════════════════════════════════════════════════════
 * [CN-FORK] v0.5.2.112 游戏管理新功能的 mock（详情 / 删除 / 存档 / 多媒体 / 改名）
 *
 * 接口契约以 `frontend/src/utils/gameApi.js` 顶部注释为准，这里只实现桩。
 * 三个刻意的设计，让本地预览能覆盖**真实分支**而不是只走顺风路：
 *
 *  ① 删除的三种「非失败结局」按 tid 固定触发（DELETE_SPECIAL），逐条可核对：
 *       PPSA01500 → 403 EPERM       源在受保护路径下，不允许删除
 *       PPSA02000 → 409 EBUSY       游戏还在运行
 *       EMUA90001 → 200 source_missing  源已不存在（不算失败）
 *     其余 tid 走 202 + 异步任务，进度条会真的动。
 *     ⚠ 这三个样本必须是 **dump 游戏**（can_manage_source=true）：前端完全认后端这个字段
 *       来决定要不要画「删除源」，用已安装的 PKG 当样本的话，按钮根本不出现，错误路径白写。
 *  ② 所有写操作（改名 / 换背景 / 删存档 / 删媒体 / 删游戏）都**改内存状态**，
 *     刷新列表能看见变化 —— 只回 {ok:true} 的桩验证不了前端有没有真的重读。
 *  ③ 媒体缩略图与原图都返回 SVG（带 TID 与序号），不塞假二进制：
 *     目的是核对布局与交互，SVG 还能顺便把「这是第几号图」画在图上。
 *
 * 另：zip 返回的是**合法的空 zip**（22 字节的 end-of-central-directory），
 * 不是随便一段字节 —— 这样「下载打包文件」在预览里能真的落一个能被解压工具打开的文件，
 * 从而验证到「点了下载确实有文件下来」这一步，而不只是「按钮能点」。
 * ══════════════════════════════════════════════════════════════════════════════ */

const EMPTY_ZIP = Buffer.from('504b0506' + '0'.repeat(36), 'hex');

const DELETE_SPECIAL = {
  /* 三个样本都必须是**可管源的 dump 游戏**（can_manage_source=true），否则前端不会渲染
   * 「删除源」，这几条错误路径就只能在 curl 里验、界面上永远走不到。 */
  PPSA01500: { status: 403, body: { error: 'EPERM', message: '这个游戏的源在受保护路径下，不允许删除' } },
  PPSA02000: { status: 409, body: { error: 'EBUSY', reason: 'game_active', message: '这个游戏还在运行' } },
  EMUA90001: { status: 200, body: { source_missing: true, scan_queued: true } },
};

/* [CN-FORK] v0.5.2.166 主机「主屏固定列表」（app.db 的 tbl_group_<uid>，MyHome 组）的**唯一**夹具源。
 * 做成函数而不是常量：`/tile_group_order` 会就地改 items 的 idx，`/reset` 要能回到初始状态。
 * 抽出来的另一个理由是上面那段注释里说的 —— 两处各抄一份必然漂移。 */
function tileGroupFixture() {
  return {
    group: 'MyHome',
    accounts: 4,
    items: [
      { itemId: 'NPXS40037', idx: 1 },
      { itemId: 'PPSA01234', idx: 2 },   // ← 游戏列表里的（游戏也能被固定进 MyHome）
      { itemId: 'NPXS40047', idx: 3 },
      { itemId: 'PPSA32786', idx: 4 },
      { itemId: 'CUSA01111', idx: 5 },   // ← 同上
      { itemId: 'WKAL00001', idx: 6 },
      { itemId: 'NPXS40071', idx: 7 },
    ],
  };
}

const gameState = {
  deleted: new Set(),   // 已删除的 tid，列表里过滤掉
  renamed: {},          // tid -> 新名字（对应真机上我们自己的 name.txt —— 面板的事实源）
  /* [CN-FORK] v0.5.2.137 tid -> **主机索引里**的名字（真机上是 app.db/appinfo.db 的平铺名字列）。
   * 为什么要跟 renamed 分开存：改名只在改名那一刻写主机，所以这两份**会漂**——
   * 真机上撞到的就是「面板有中文名、app.db 被 SMP 重扫按源冲回英文原名」。
   * /game_title_sync 干的事就是把 renamed 重放回这里。列表名仍取 renamed（面板就该显示 name.txt），
   * 这份只给回归断言用（测试钩子 _host_names / _host_drift），产品路径不读它。 */
  hostNames: {},
  icon: {},             // [CN-FORK] v0.5.2.112 tid -> 自定义**图标**版本号（封面那一格，替代 icon0.png）
  bg: {},               // tid -> 自定义**背景**版本号（整卡底图，图标压在它上面）
  iconBytes: {},        // tid -> 上传的图标 PNG 原始字节（自定义过之后由 /game_tile_icon 回它）
  bgBytes: {},          // tid -> 上传的背景 PNG 原始字节（/game_tile_bg 回的预览就是它）
  bgDds: {},            // tid -> 上面那张背景转出来的 DXT1 DDS（/game_tile_bg_dds 回它）
  /* [CN-FORK] v0.5.2.163 背景音乐：tid -> { name, bytes, ext }（落 /data/nayeplmtools/music/<TID>.<ext>） */
  music: {},
  /* [CN-FORK] v0.5.2.166 写进主机的 ATRAC9：tid -> { bytes, lufs }（落 <sce_sys>/snd0.at9）。
   * 与上面那份**分开记**是刻意的：用户这一轮报的「改了没生效」根因就是这两份被混为一谈 ——
   * 面板那份写成了、主机那份压根没写，界面上却都叫「已应用」。mock 分开存，
   * 回归才能断言「设一次音乐之后主机那份真的出现了」。 */
  musicHost: {},
  /* [CN-FORK] v0.5.2.166 强制「主机 snd0.at9 写失败」的 tid 集合（默认空 = 都成功）。
   * 用来测前端在主机写失败时到底报不报红 —— 那正是这一轮要修的东西。 */
  musicHostFail: [],
  /* [CN-FORK] v0.5.2.164 游戏磁贴的收藏 / 置顶：tid -> { fav, pin }（对应后端的 game_prefs.json） */
  prefs: {},
  /* [CN-FORK] v0.5.2.166 面板侧的手工顺序（拖拽排序的结果，对应 game_prefs.json 的 order）。
   * 一个 tid 数组，前端按它的下标排。 */
  order: [],
  /* [CN-FORK] v0.5.2.166 主机 app.db 的「主屏固定列表」（tbl_group_<uid> 的 MyHome 组）。
   * 形状照真机取证件裁（见 include/tile_group.h）：4 个账号，itemId 是 tid，idx 是主屏位置。
   * PPSA32786 / WKAL00001 是取证时看到的那两个「用户自己固定上去的」条目。
   *
   * ⚠ 里面**必须**混进两个 `mockGames` 里也有的 tid（PPSA01234 / CUSA01111）。
   * 只有系统条目的话，`/tile_group_order` 那条「主机侧只重排组里已有的条目」永远是
   * `moved=0` —— 回归里「拖完顺序主机侧真的动了吗」这条断言就变成了空转，
   * 而它恰恰是这一轮用户报的那个 bug（「排序按钮没有实现我想让排哪个位置就排哪个位置」）
   * 唯一能测到的落点。这就是「mock 抹掉了真机的前提」那一类坑：夹具不像真机，
   * 功能在本地就永远是绿的。真机上用户当然可以把自己装的游戏固定进主屏。
   *
   * ⚠ 只能由 `tileGroupFixture()` 产出 —— `/reset` 也要用它。上一版这里写一份、
   * reset 里另抄一份，加夹具时只改了这里，于是「跑第二遍 game112 时夹具退回旧的」，
   * 那种红会指向完全无关的地方。 */
  tileGroup: tileGroupFixture(),
  /* 强制「主机固定列表写不进去」（tbl_group 表不存在 / 全表读失败），供前端降级分支测试。 */
  tileGroupBroken: false,
  /* [CN-FORK] v0.5.2.164 「个性化软件」的置顶开关：tid -> bool（对应后端的 app_pins.json）。
   * 与 hidden 分开存是刻意的 —— hidden 在真机上写 app.db，置顶只落本面板的 JSON。 */
  appPins: {},
  saves: {},            // tid -> 存档数组
  media: {},            // tid -> 媒体数组
  auto: {},             // tid -> 自动存档配置
  snaps: {},            // [CN-FORK] v0.5.2.140 tid -> 本地快照数组（新的在前）
  autoState: {},        // [CN-FORK] v0.5.2.142 tid -> 自动存档引擎的运行期状态（auto_state）
  runningTid: '',       // [CN-FORK] v0.5.2.142 模拟「此刻哪个游戏在跑」，空 = 没有
  /* [CN-FORK] v0.5.2.154 garlic 兼容导入的流水账。**只记账、不写 saves** ——
   * 导入的语义是「逐文件合并」（不是整份替换），一个存档目录里除了当前镜像还有主机自己的
   * 记账（sdimg_sce_bu_* / sdimg_sce_sdmemory），合并不会动它们，列表也不该多出一条。
   * 回归靠它钉住「前端有没有把 file.name 当 name 传上来」—— 那是纯 query 拼装，
   * mock 不记这笔账就完全测不到（同族：坑 25 的「mock 抹掉了真机的前提」）。 */
  garlicImports: [],
  /* [CN-FORK] v0.5.2.165 强制「主机磁贴数据库写失败」的 tid 集合（默认空 = 都成功）。
   * 后端 tile_db_set_name 失败时回 host_ok:false，前端要弹红字；mock 里没有真库，
   * 所以留一个显式开关让回归能走到那条分支 —— 否则那行代码永远没人验。 */
  hostRenameFail: new Set(),
  remoteFiles: new Set(), // [CN-FORK] v0.5.2.142 远端已有的 `${tid}/${uid}`（推送往里加，拉取查它）
  jobs: {},             // job_id -> 任务
  nextJob: 1,
  seq: 0,               // 让每次生成的假数据（体积/时间）有变化，避免看起来是写死的
};

/* 一个假的异步任务。进度不存下来、按「现在 - 开始时间」现算 —— 这样前端轮询几秒就能
 * 看到进度条在动，而 mock 里不需要任何定时器（进程里少一个会泄漏的东西）。 */
function startJob(kind, totalBytes, totalFiles, meta) {
  const id = gameState.nextJob++;
  const j = { id, kind, start: Date.now(), totalBytes, totalFiles, cancelled: false, meta: meta || {} };
  gameState.jobs[id] = j;
  /* 任务「跑完」的**后果**必须自己定时发生，不能挂在 /game_job_status 里 ——
   * 那是读接口，而前端关掉弹窗就会停止轮询（真实行为：关窗不停任务）。
   * 原来把「删除生效」写在读接口里，导致「关掉弹窗等一会儿再看」这条最真实的使用路径下，
   * 删除永远不会落地。真机后端也不是靠谁来问才动手的。 */
  if (kind === 'delete' && j.meta.tid) {
    setTimeout(() => {
      if (!j.cancelled) gameState.deleted.add(j.meta.tid);
    }, jobDuration(j) * 1000);
  }
  return id;
}

/* 任务时长：体积越大跑越久，但夹在 3.5~9 秒 —— 太短看不清进度条，太长本地预览要等。 */
function jobDuration(j) {
  return Math.max(3.5, Math.min(9, j.totalBytes / 120e6));
}

function jobStatus(id) {
  const j = gameState.jobs[id];
  if (!j) return null;
  const elapsed = (Date.now() - j.start) / 1000;
  const dur = jobDuration(j);
  const p = j.cancelled ? 1 : Math.min(1, elapsed / dur);
  const done = p >= 1;
  const bytesDone = Math.floor(j.totalBytes * p);
  const filesDone = Math.floor(j.totalFiles * p);
  return {
    job_id: j.id,
    active: !done,
    /* [CN-FORK] v0.5.2.122 phase 取值与真机对齐：打包走 preparing → measuring →
     * packing（原来这里给的是 'transferring'，后端从来没有这个取值 —— 前端的中文
     * 映射表认不出它，界面上会直接显示一个英文单词）。 */
    phase: j.cancelled ? 'cancelled' : done ? 'done' : p < 0.15 ? 'measuring' : j.kind === 'delete' ? 'deleting' : 'packing',
    percent: Math.round(p * 1000) / 10,
    bytes_done: bytesDone,
    bytes_total: j.totalBytes,
    files_done: filesDone,
    files_total: j.totalFiles,
    rate: p > 0 ? Math.round(bytesDone / Math.max(0.1, elapsed)) : 0,
    elapsed: Math.round(elapsed * 10) / 10,
    /* mock 的进度是按时间现算的，永远不会「卡住」，所以 idle 恒 0。
     * 真机后端算的是「距上次有进展多少秒」（game_job.c 的 g_last_progress）。 */
    idle: 0,
    done,
    ok: done && !j.cancelled,
    error: j.cancelled ? '已取消' : '',
    reason: '',
    /* 打包成品文件名（游戏名称-打包时间戳.zip）；删除任务为空串 —— 与后端
     * gj_status_json 的 name 字段一一对应，前端拿它当 <a download> 的值。 */
    name: (j.meta && j.meta.name) || '',
  };
}

/* 详情：把列表项补齐成一份完整的详情。字段与 gameApi.js 的契约一一对应。
 * 刻意留两个 tid 没有 content_id / version（模拟读不到 app.db 行的情况），
 * 验证前端那些格子会不会正确显示成「—」而不是 undefined。
 *
 * [CN-FORK] v0.5.2.127 来源类别**按 fmt 推**，不按 src —— 与后端 game_mgr.c 的
 * gm_class_of_fmt() 同一口径。理由（真机取证）：src 记的是「从 app.db 还是扫描路径
 * 枚举到的」，而 ShadowMount 的镜像/裸目录游戏同样注册在 app.db 里（src 也是
 * installed）。拿 src 当来源类型，真机上 14 款游戏全被标成「已安装 PKG」，
 * 物理路径 / 已挂载 / can_manage_source 一起错。桩必须跟着后端一起改，
 * 否则回归验的是旧口径，改坏了也看不出来。 */
function buildDetail(tid) {
  const base = mockGames.find((x) => x.tid === tid);
  if (!base) return null;
  const platform = /^(CUSA|PPJB)/.test(tid) ? 'PS4' : 'PS5';
  const sourceType = base.fmt === 'pkg' ? 'pkg'
    : (base.fmt === 'folder' || base.fmt === 'lz4') ? 'folder' : 'image';
  const isPkg = sourceType === 'pkg';
  const imgType = sourceType === 'image' ? base.fmt : '';
  const known = tid !== 'PAUS00100' && tid !== 'EMUA90001';
  /* 镜像游戏的物理源是那个镜像文件，裸目录游戏是源目录，内置 PKG 才是 app 目录。 */
  const srcPath = sourceType === 'image'
    ? `/mnt/usb0/games/${tid}.${base.fmt}`
    : `/mnt/usb0/games/${tid}`;
  return {
    tid,
    name: gameState.renamed[tid] || base.name,
    src: base.src,
    fmt: base.fmt,
    size: base.size,
    platform,
    source_type: sourceType,
    image_type: imgType,
    content_id: known ? `${tid}-${tid}_00-${'0'.repeat(16)}` : '',
    version: known ? `0${1 + (tid.charCodeAt(8) % 3)}.00${(tid.charCodeAt(7) % 9) + 1}.000` : '',
    path: isPkg ? `/user/app/${tid}` : srcPath,
    /* 内置 PKG 没有 ShadowMount 运行时挂载；镜像游戏挂到 /mnt/shadowmnt，
     * 裸目录游戏的 mount.lnk 直接指向源目录。 */
    runtime_path: isPkg ? ''
      : sourceType === 'image' ? `/mnt/shadowmnt/${tid}_d919e306` : srcPath,
    appdb_size: isPkg ? 3145728 + (tid.charCodeAt(6) % 7) * 1048576 : 0,
    install_time: known ? '2026-09-12 21:33' : '',
    last_launch: known ? '2026-10-04 22:10' : '',
    /* [CN-FORK] v0.5.2.127 「已挂载」= 这个游戏此刻真的挂进系统了（后端查
     * /system_ex/app/<TID>/sce_sys/param.json）。**不是**「runtime_path 存在」——
     * 裸目录游戏的源目录一直都在，那样「已挂载」会对它们永远亮着。
     * 这里挑一款当「正在运行」的样本，好让这颗徽标在预览里真的能出现。 */
    mounted: !isPkg && tid === 'PPSA02000',
    installed_pkg: isPkg,
    can_uninstall: true,
    /* 与真机同义：内置 PKG 没有可管的物理源（不能挂载、不能删源）。
     * 前端不再自己用 src/installed_pkg 判一遍，完全认这个字段 —— 所以 DELETE_SPECIAL 里
     * 那两个「详情说可管、后端却拒绝」的样本必须是 dump 游戏，否则那两条错误路径
     * 在界面上根本走不到，桩就白做了。 */
    can_manage_source: !isPkg,
    saves_count: savesFor(tid).length,
    media_count: mediaFor(tid).length,
    /* 两层各自一个布尔（v0.5.2.112）：改名弹窗靠它决定「恢复默认」该不该可点。
     * 只给一个合成布尔的话，用户换了图标之后再想清掉背景就会连图标一起丢掉。 */
    title_icon_custom: !!gameState.icon[tid],
    title_bg_custom: !!gameState.bg[tid],
  };
}

/* 真机上的用户目录名（192.168.1.214 实测就是这四个）。后端「uid 必须是主机上
 * 真实存在的用户」这条规则，在 mock 里就靠这个集合来等价表达。 */
const MOCK_UIDS = ['1a1d3627', '1a1d3629', '1a1d362a', '1a1d362b'];

/* [CN-FORK] v0.5.2.165 当前（前台）用户 —— 对应真机的
 * sceUserServiceGetForegroundUser（后端 game_saves_current_uid）。
 * ⚠ **故意不是 MOCK_UIDS[0]**：后端那条 bug（拿 saves[0] 当默认，而 saves[0]
 * 恒是 uid 升序第一个）只有在「当前用户 ≠ 第一个用户」时才测得出来 ——
 * 两者相等的话，老代码与新代码表现完全一样，回归就是白写。 */
const MOCK_CURRENT_UID = '1a1d3629';

/* 存档：按 tid 生成 0~3 条，**每条 = 一个存过这个游戏的用户**，稳定（同 tid
 * 每次一样），便于对照。与真机同形：save/id 就是 uid，kind 由所在目录决定，
 * 一台主机可以有多个用户都存过同一个游戏 → 多行，互不覆盖。 */
function savesFor(tid) {
  if (gameState.saves[tid]) return gameState.saves[tid];
  if (tid === 'PAUS00100') return [];               // 留一个「没有存档」的样本
  const n = 1 + (tid.charCodeAt(5) % 3);
  const out = [];
  for (let i = 0; i < n; i++) {
    const uid = MOCK_UIDS[i % MOCK_UIDS.length];
    out.push({
      id: uid,
      name: uid,
      kind: /^(CUSA|PPJB)/.test(tid) ? 'PS4' : 'PS5',
      mtime: `2026-10-0${(i % 4) + 2} ${10 + i}:2${i}`,
      size: 1048576 * (3 + i * 2) + (tid.charCodeAt(6) % 9) * 131072,
      files: 8 + i * 3,
    });
  }
  gameState.saves[tid] = out;
  return out;
}

/* 与后端 gs_root_or_create 同语义：uid 必须是主机上真有的用户目录，
 * 没有这个用户就拒（不许瞎建）。uid 为空 = 落在**当前用户**下
 * （[CN-FORK] v0.5.2.165 之前是「第一个用户」，那正是导入落错人的根因）。 */
function resolveSaveUser(uid) {
  if (!uid) return MOCK_CURRENT_UID;
  return MOCK_UIDS.includes(uid) ? uid : null;
}

/* 游戏显示名（改过名就用新名）—— 与后端 game_title_get_name 同源语义。 */
function gameNameFor(tid) {
  const base = mockGames.find((x) => x.tid === tid);
  return gameState.renamed[tid] || (base ? base.name : tid);
}

/* 后端 md_sanitize_name 的等价物：空格 / 冒号 / 斜杠 / 引号 / 通配符 → '-'。
 * 游戏名里带空格（「战神 诸神黄昏」）与全角括号是常态，落盘前必须清一遍。 */
function sanitizeFileName(s) {
  // eslint-disable-next-line no-control-regex -- 控制字符是**故意**要挡的（后端同样挡）
  return String(s).replace(/[ :/\\<>"|?*\u0000-\u001f\u007f]/g, '-');
}

/* 「游戏名称-YYYYMMDD_HHMMSS.<ext>」—— 与后端 md_dlname 同一口径。
 * mock 的 mtime 是 'YYYY-MM-DD HH:MM'（没有秒），补 ':00' 再格式化。 */
function dlNameFor(tid, kind, mtime) {
  const ext = kind === 'video' ? 'mp4' : 'png';
  const d = new Date(`${String(mtime).replace(' ', 'T')}:00`);
  const p = (n) => String(n).padStart(2, '0');
  const ts = Number.isNaN(d.getTime())
    ? 'unknown'
    : `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}_${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
  return `${sanitizeFileName(gameNameFor(tid))}-${ts}.${ext}`;
}

/* 打包成品名：「游戏名称-打包时间戳.zip」 */
function packNameFor(tid) {
  const d = new Date();
  const p = (n) => String(n).padStart(2, '0');
  const ts = `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}_${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
  return `${sanitizeFileName(gameNameFor(tid))}-${ts}.zip`;
}

/* [CN-FORK] v0.5.2.125 Content-Disposition，与后端 http_server.c 的 add_disposition 同口径：
 * 一份 ASCII 兜底（非 ASCII 逐字节换成 '_'）+ 一份 RFC 5987 的 filename*。
 *
 * 不能只写 `filename="<中文名>"` —— Node 的 setHeader 遇到非 ASCII 会直接抛 ERR_INVALID_CHAR，
 * express 回 500，于是 mock 上「下载」这条路从来没被真正跑通过。后果不只是测试缺口：
 * 真机上 124 版那个「打包完但下载按钮永不出现」的 bug，本来靠「点一下下载」就能立刻暴露，
 * 却因为 mock 这一步自己先 500 而被长期掩盖。 */
function disposition(name) {
  const ascii = String(name).replace(/[^\x20-\x7e]/g, '_');
  let enc = '';
  for (const b of Buffer.from(String(name), 'utf8')) {
    enc += b === 0x22 || b === 0x5c || b < 0x20 || b >= 0x7f
      ? '%' + b.toString(16).toUpperCase().padStart(2, '0')
      : String.fromCharCode(b);
  }
  return `attachment; filename="${ascii}"; filename*=UTF-8''${enc}`;
}

/* 媒体：截图为主 + 少量视频。数量按 tid 变化，留一个「空」的样本。
 * 这里只出**原始字段**；name（显示名）/ dlname（下载名）由 /game_media 现算 ——
 * 因为游戏名可以被改，缓存进 gameState.media 就会把旧名字钉死。 */
function mediaFor(tid) {
  if (gameState.media[tid]) return gameState.media[tid];
  if (tid === 'PAUS00100') return [];
  const nShots = 6 + (tid.charCodeAt(7) % 7);
  const nVideos = tid.charCodeAt(8) % 3;
  const out = [];
  for (let i = 0; i < nShots; i++) {
    out.push({
      id: `${tid}-s${i}`,
      kind: 'screenshot',
      mtime: `2026-10-0${(i % 9) + 1} 1${i % 10}:2${i % 6}`,
      size: 3145728 + i * 262144,
      w: 3840, h: 2160,
    });
  }
  for (let i = 0; i < nVideos; i++) {
    out.push({
      id: `${tid}-v${i}`,
      kind: 'video',
      mtime: `2026-10-0${(i % 9) + 1} 1${i}:3${i}`,
      size: 52428800 + i * 10485760,
      w: 1920, h: 1080,
      duration: 18 + i * 27,
    });
  }
  gameState.media[tid] = out;
  return out;
}

/* [CN-FORK] v0.5.2.145 全局存档配置（设置页「存档配置」）。mock 里等价于后端那份
 * ARCHIVE_GLOBAL_PATH 文件：main config 的 ARCHIVE_GLOBAL_CONFIG 一非空就「生效」。
 * 返回解析后的对象；没配过（键不存在 / 空串 / 坏 JSON）返回 null ——
 * 与后端 config_read_archive_global() 返回 -1 是同一件事。 */
function globalArchive() {
  const raw = mockConfig.ARCHIVE_GLOBAL_CONFIG;
  if (typeof raw !== 'string' || !raw.trim()) {
    /* [CN-FORK] v0.5.2.147 **没配过就是 null** —— 与后端 config_read_archive_global()
     * 返回 -1 一字不差。
     *
     * `.146` 这里曾返回一份「开箱即用的默认 SMB 配置」（注释说是为了方便预览「远程游戏」
     * 区块）。那是**错的**：`.145` 定的核心契约是「全局那份**写过没有**」——
     * 没写过就必须回落到 per-game，否则「老装机行为不变」这条根本不成立。假默认一在，
     * `autoFor()` 就把全局盖在 per-game 上，mock 里**永远走不到回落分支**，
     * 于是回归里那条「mock 默认没有全局配置 → 读到本游戏已存的那份」变成了假红
     * （渲染出 SMB 而不是 per-game 的 FTP），另一条「快照间隔默认 30 分钟」也跟着
     * 读成了假默认里的 3600 秒 = 60 分钟。
     *
     * 而且它并不必要：「远程游戏」区块的数据来自 /game_save_remote_info（那份是硬编码的，
     * 不吃这里），per-game 默认本身也带一份配好的远程，该预览的一样看得到。
     * 要测「全局生效」的用例请照旧走设置页真存一次（第 5d/5e 节就是这么做的）。 */
    return null;
  }
  try {
    const o = JSON.parse(raw);
    return o && typeof o === 'object' ? o : null;
  } catch {
    return null;
  }
}

/* 这个游戏**自己存的那份**（不带全局）。写入路径必须用这个 —— autoFor() 在全局生效时
 * 返回的是合并后的**副本**，往副本上写会静默丢掉（`/game_save_auto` 正是全字段覆写）。 */
function pgAutoFor(tid) {
  if (!gameState.auto[tid]) {
    gameState.auto[tid] = {
      enabled: false,
      /* [CN-FORK] v0.5.2.142 默认协议从 smb 改成 ftp，与后端 gs_auto_defaults 一致 ——
       * .142 的理由（「libcurl 没编 smb = 做不了」）在 .143 已被推翻：SMB 现在走
       * libsmb2，是能用的一档。默认留 ftp 纯粹因为它最通用。
       * 想测「老用户存的是 smb」那条路，用 POST /game_save_auto {mode:'smb'} 显式造。 */
      mode: 'ftp',
      on_launch: true,
      on_exit: true,
      /* sub 留空：.143 起 <TID>/ 由后端自己加一层，mock 照同一口径摆（写 tid 会叠两层）。 */
      smb: { host: '192.168.1.10', port: 445, share: 'ps5save', sub: '', user: 'ps5', has_password: true },
      ftp: { host: '192.168.1.10', port: 2121, path: `/ps5save/${tid}`, user: 'ps5', has_password: false },
      http: { url: '', token_set: false },
      /* [CN-FORK] v0.5.2.140 本地快照。默认值必须与后端 gs_auto_defaults 一模一样，
       * 否则前端读到的默认值和真机上的对不上（这是「测试全绿、真机全错」的老路）。 */
      snap: { enabled: false, interval: 1800, keep: 5 },
    };
  }
  return gameState.auto[tid];
}

/* 这个游戏**实际生效**的那份（全局优先），供读取路径用。
 * 与后端 ga_load_cfg() / gs_snap_cfg() 同一条规则：全局配过就整套以全局为准（含远程段），
 * 没配过才用这个游戏自己那份。放在这里（而不是让每个调用方各自判一次）是为了让
 * mock 的 /game_saves 就是「引擎眼里的真相」，回归可以直接对着它断言「全局生效」。 */
function autoFor(tid) {
  const pg = pgAutoFor(tid);
  const g = globalArchive();
  if (!g) return pg;
  return {
    ...pg,
    enabled: !!g.enabled,
    on_launch: g.on_launch !== false,
    on_exit: g.on_exit !== false,
    mode: g.mode || pg.mode,
    smb: { ...pg.smb, ...(g.smb || {}) },
    ftp: { ...pg.ftp, ...(g.ftp || {}) },
    http: { ...pg.http, ...(g.http || {}) },
    snap: { ...pg.snap, ...(g.snap || {}) },
  };
}

/* [CN-FORK] v0.5.2.142 自动存档引擎的运行期状态（对应后端 /game_saves 的 auto_state）。
 * 后端是**当场探测** running，这里用 gameState.runningTid 模拟（测试可以摆布它）。
 * last 的形状与后端一字不差：{action, ok, at, message}，从没同步过就是 null。 */
function autoStateFor(tid) {
  const rec = gameState.autoState[tid] || null;
  return {
    running: !!gameState.runningTid && gameState.runningTid === tid,
    last: rec || null,
  };
}

function mockStamp(d) {
  const p = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

/* ── 本地快照（v0.5.2.140）────────────────────────────────────────────────
 * 与后端同一口径：不预置任何假快照（真机上没做过就是空的），只有 run 会新增。
 * 时间戳目录名 YYYYmmdd-HHMMSS —— 字典序 = 时间序，与后端一致。 */
function snapsFor(tid) {
  if (!gameState.snaps[tid]) gameState.snaps[tid] = [];
  return gameState.snaps[tid];
}

function snapStamp(d) {
  const p = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}${p(d.getMonth() + 1)}${p(d.getDate())}-${p(d.getHours())}${p(d.getMinutes())}${p(d.getSeconds())}`;
}

/* 与后端 gs_snap_clamp_* 完全一致：越界**回落默认**（不是夹到边界），
 * 只有上限那一侧才是夹。分开写两套是这类功能最容易漂的地方。 */
function clampSnapInterval(v) {
  if (!Number.isFinite(v) || v < 60) return 1800;
  return v > 86400 ? 86400 : v;
}
function clampSnapKeep(v) {
  if (!Number.isFinite(v) || v < 1) return 5;
  return v > 20 ? 20 : v;
}

/* 缩略图 / 原图 / 磁贴背景，统一用一个带文字的 SVG 生成 —— 一眼能看出是哪一张。 */
function artSvg(label, sub, w, h, accent) {
  return `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">`
    + `<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="1">`
    + `<stop offset="0" stop-color="${accent}"/><stop offset="1" stop-color="#0b1220"/></linearGradient></defs>`
    + `<rect width="${w}" height="${h}" fill="url(#g)"/>`
    + `<text x="${w / 2}" y="${h / 2 - h * 0.04}" font-family="monospace" font-size="${Math.round(h * 0.13)}"`
    + ` fill="#e6edf3" text-anchor="middle">${label}</text>`
    + `<text x="${w / 2}" y="${h / 2 + h * 0.1}" font-family="monospace" font-size="${Math.round(h * 0.08)}"`
    + ` fill="#9fb0c7" text-anchor="middle">${sub}</text></svg>`;
}

/* 详情 */
app.get('/game_detail', (req, res) => {
  const d = buildDetail(String(req.query.tid || ''));
  if (!d || gameState.deleted.has(d.tid)) {
    return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
  }
  res.json(d);
});

/* 删除（异步任务） */
app.post('/game_delete', (req, res) => {
  const tid = String((req.body && req.body.tid) || req.query.tid || '');
  const confirm = !!(req.body && req.body.confirm);
  if (!tid || gameState.deleted.has(tid)) {
    return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
  }
  if (!confirm) {
    return res.status(400).json({ error: 'EINVAL', message: '需要 confirm=true 才允许删除' });
  }
  const special = DELETE_SPECIAL[tid];
  if (special) return res.status(special.status).json(special.body);
  const g = mockGames.find((x) => x.tid === tid);
  const bytes = (g && g.size) || 40e9;
  res.status(202).json({
    job_id: startJob('delete', bytes, 20 + (tid.charCodeAt(7) % 60), { tid }),
    scan_queued: true,
  });
});

app.get('/game_job_status', (req, res) => {
  const id = Number(req.query.job);
  const st = jobStatus(id);
  if (!st) return res.status(404).json({ error: 'ENOENT', message: '没有这个任务' });
  /* 纯读。删除的落地由 startJob 里的定时器负责（见那里的注释）——
   * 读接口不带副作用，才测得出「关掉弹窗、过一会儿再刷新列表」这条路径。 */
  res.json(st);
});

app.get('/game_job_cancel', (req, res) => {
  const j = gameState.jobs[Number(req.query.job)];
  if (!j) return res.status(404).json({ error: 'ENOENT', message: '没有这个任务' });
  j.cancelled = true;
  res.json({ ok: true });
});

/* 挂载 / 卸载 / 卸载安装 */
app.post('/game_mount', (req, res) => {
  res.json({ ok: true, mode: (req.body && req.body.mode) === 'rw' ? 'rw' : 'ro' });
});
app.post('/game_unmount', (req, res) => res.json({ ok: true }));
app.post('/game_uninstall', (req, res) => res.json({ ok: true, scan_queued: true }));

/* 存档 */
app.get('/game_saves', (req, res) => {
  const tid = String(req.query.tid || '');
  const base = mockGames.find((x) => x.tid === tid);
  if (!base || gameState.deleted.has(tid)) {
    return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
  }
  const saves = savesFor(tid);
  const snaps = snapsFor(tid);
  res.json({
    tid,
    /* 与真机同形：PS5 存档在 savedata_prospero 下；一个都没有时是空串
     * （后端 game_saves_root 找不到就回空 —— 前端据此判「还没有存档」）。 */
    local_path: saves.length ? `/user/home/${saves[0].id}/savedata_prospero/${tid}` : '',
    auto: autoFor(tid),
    /* [CN-FORK] v0.5.2.142 引擎运行期状态（只读，不是配置）。 */
    auto_state: autoStateFor(tid),
    saves,
    /* [CN-FORK] v0.5.2.165 「导入/上传落到哪个用户」的默认值与候选项，与后端
     * game_saves_list_json 的两个新字段同形：current_uid = 前台用户，users =
     * 主机上**全部**用户目录（不只是有存档的）。 */
    current_uid: MOCK_CURRENT_UID,
    users: MOCK_UIDS.slice(),
    snap_used: snaps.reduce((a, s) => a + s.size, 0),
    /* 剥掉内部用的 src 指纹字段 —— 真机上快照条目就只有这五个键 */
    snapshots: snaps.map(({ uid, ts, mtime, size, files }) => ({ uid, ts, mtime, size, files })),
  });
});

/* [CN-FORK] v0.5.2.159 被「删除远程存档」删掉的份：<tid>/<uid>。
 * mock 的远程信息本来是无状态硬编码（永远 exists:true），加了这个集合才删得动 ——
 * 不记状态的话前端按了按钮、回读一次又变回「有」，看不出这条链通不通。 */
const remoteGone = new Set();

/* [CN-FORK] v0.5.2.145 远程存档只读元数据：前端「远程游戏」区块展示用。
 * mock 让每个本地用户都有一份远程存档，大小/文件数与本地同形、mtime 更早，
 * 表示「远端确实存着」（真实后端由 SMB 的 smb2_stat + <uid>.meta.json 提供）。 */
app.get('/game_save_remote_info', (req, res) => {
  const tid = String(req.query.tid || '');
  const save = String(req.query.save || '');
  const base = mockGames.find((x) => x.tid === tid);
  if (!base || gameState.deleted.has(tid)) {
    return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
  }
  if (!save) return res.status(400).json({ error: 'EBAD', message: '缺少 save' });
  const local = (savesFor(tid) || []).find((s) => s.id === save);
  if (!local) return res.status(404).json({ error: 'ENOENT', message: '找不到这个用户的存档' });
  if (remoteGone.has(`${tid}/${save}`)) {
    /* 与真机同一形态：SR_ENOTFOUND 那条分支回的就是 configured+exists+mode 三个键 */
    return res.json({ configured: true, exists: false, mode: 'smb' });
  }
  res.json({
    configured: true,
    exists: true,
    mode: 'smb',
    size: local.size,
    files: local.files,
    mtime: '2026-09-28 09:15',
  });
});

/* [CN-FORK] v0.5.2.159 删掉远端那一份。tid / save 从 **body** 取（与真机一致，
 * 不摊在 query 上），只记远程没了 —— 主机上的本地存档与本地快照都不动，
 * 所以这里刻意不去碰 savesFor() / snapsFor()。 */
app.post('/game_save_remote_delete', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  const save = String((req.body && req.body.save) || '');
  if (!tid || !save) {
    return res.status(400).json({ error: 'EINVAL', message: '缺少 tid 或 save' });
  }
  const local = (savesFor(tid) || []).find((s) => s.id === save);
  if (!local) return res.status(404).json({ error: 'ENOENT', message: '找不到这个用户的存档' });
  remoteGone.add(`${tid}/${save}`);
  res.json({ ok: true });
});

app.post('/game_save_auto', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  /* 写 per-game 那一份 —— 不能用 autoFor()：全局生效时它给的是合并副本，写了会丢。 */
  const cur = pgAutoFor(tid);
  const b = req.body || {};
  /* 密码/token 没传 = 保持原值（与前端「留空即不修改」的约定一致）。 */
  ['smb', 'ftp', 'http'].forEach((m) => {
    if (!b[m]) return;
    const next = { ...cur[m], ...b[m] };
    if (b[m].password !== undefined) next.has_password = !!b[m].password;
    if (b[m].token !== undefined) next.token_set = !!b[m].token;
    delete next.password; delete next.token;
    cur[m] = next;
  });
  cur.enabled = !!b.enabled;
  cur.mode = b.mode || cur.mode;
  cur.on_launch = b.on_launch !== false;
  cur.on_exit = b.on_exit !== false;
  /* snap 也是「没传的字段保持原值」—— enabled 只在真的是布尔时才覆盖。 */
  if (b.snap && typeof b.snap === 'object') {
    if (typeof b.snap.enabled === 'boolean') cur.snap.enabled = b.snap.enabled;
    if (b.snap.interval !== undefined) cur.snap.interval = clampSnapInterval(parseInt(b.snap.interval, 10));
    if (b.snap.keep !== undefined) cur.snap.keep = clampSnapKeep(parseInt(b.snap.keep, 10));
  }
  res.json({ ok: true });
});

/* [CN-FORK] v0.5.2.140 本地快照：立即备份 / 还原 / 删除。 */
app.post('/game_save_snap', (req, res) => {
  const b = req.body || {};
  const tid = String(b.tid || '');
  const action = String(b.action || '');
  const base = mockGames.find((x) => x.tid === tid);
  if (!base || gameState.deleted.has(tid)) {
    return res.status(400).json({ error: 'EBAD', message: '主机上找不到这个游戏' });
  }
  const list = savesFor(tid);
  const snaps = snapsFor(tid);

  if (action === 'run') {
    if (!list.length) {
      return res.status(400).json({ error: 'EBAD', message: '主机上还没有这个游戏的存档' });
    }
    const ts = snapStamp(new Date());
    let made = 0;
    list.forEach((s) => {
      /* 内容没变就不新增 —— 与后端同一判据（这里拿 size+files+存档 mtime 当指纹） */
      const newest = snaps.find((x) => x.uid === s.id);
      if (newest && newest.size === s.size && newest.files === s.files && newest.src === s.mtime) return;
      snaps.unshift({ uid: s.id, ts, mtime: s.mtime, size: s.size, files: s.files, src: s.mtime });
      made++;
    });
    /* 保留数裁剪：每个用户只留最近 keep 份。列表新的在前，从尾部砍。 */
    const keep = autoFor(tid).snap.keep; /* 读取路径：按生效值（全局优先）来保留 */
    MOCK_UIDS.forEach((uid) => {
      snaps.filter((x) => x.uid === uid).slice(keep)
        .forEach((x) => snaps.splice(snaps.indexOf(x), 1));
    });
    return res.json({ ok: true, made });
  }

  const uid = String(b.uid || '');
  const ts = String(b.ts || '');
  if (!MOCK_UIDS.includes(uid) || !/^\d{8}-\d{6}$/.test(ts)) {
    return res.status(400).json({ error: 'EBAD', message: '参数不合法' });
  }
  const at = snaps.findIndex((x) => x.uid === uid && x.ts === ts);
  if (at < 0) {
    return res.status(400).json({ error: 'EBAD', message: '找不到这份快照' });
  }
  if (action === 'delete') {
    snaps.splice(at, 1);
    return res.json({ ok: true });
  }
  if (action === 'restore') {
    return res.json({ ok: true });
  }
  return res.status(400).json({ error: 'EBAD', message: '未知的 action' });
});

/* [CN-FORK] v0.5.2.143 立即同步（手动推 / 拉）。与后端 /game_save_sync 同形：
 *   · 三种协议（smb / ftp / http）走同一条分支 —— .142 时 smb 直接回 400，
 *     那是「libcurl 没编 smb」留下的；.143 起 SMB 走 libsmb2，与其它两档平权；
 *   · 推送时主机上没存档 → 400（没什么可推的）；
 *   · 拉取时远端还没有 → **200 + ok:true**，note 里说明。第一次用必然遇到，
 *     后端刻意不把它算失败，mock 也必须一样（否则前端会显示一条真机不会出现的红报错）。 */
app.post('/game_save_sync', (req, res) => {
  const b = req.body || {};
  const tid = String(b.tid || '');
  const action = String(b.action || '');
  const onlyUid = String(b.uid || '');
  const base = mockGames.find((x) => x.tid === tid);
  if (!base || gameState.deleted.has(tid)) {
    return res.status(400).json({ error: 'EBAD', message: '主机上找不到这个游戏' });
  }
  if (action !== 'push' && action !== 'pull') {
    return res.status(400).json({ error: 'EBAD', message: 'action 只能是 push 或 pull' });
  }
  /* .143 起 smb 与 ftp/http 平权，不再单独回 400（见本函数文件头）。
   * autoFor 仍然要调一次：它是惰性建的，不建的话这个游戏就没有 auto 记录，
   * 而下面 `done()` 会把 autoState 挂上去 —— 前端会看到「有状态没配置」。 */
  autoFor(tid);

  const list = savesFor(tid);
  const done = (note, extra) => {
    gameState.autoState[tid] = { action, ok: true, at: mockStamp(new Date()), message: '' };
    res.json({
      ok: true, action, uids: (extra && extra.uids) || 0,
      files: (extra && extra.files) || 0, size: (extra && extra.size) || 0,
      failed: 0, note,
    });
  };

  if (action === 'push') {
    const src = onlyUid ? list.filter((s) => s.id === onlyUid) : list;
    if (!src.length) {
      return res.status(400).json({ error: 'EBAD', message: '主机上还没有这个游戏的存档' });
    }
    let bytes = 0;
    src.forEach((s) => {
      gameState.remoteFiles.add(`${tid}/${s.id}`);
      /* [CN-FORK] v0.5.2.162 后端推的是「这份存档此刻的写入时刻」（meta.json 的
       * src_mtime），拉取前拿它和本地比 —— mock 记下同一个量，才复刻得出
       * 「刚推上去的不会再原样拉回来」。只记 mtime，不记推送时刻。 */
      (gameState.pushedFrom || (gameState.pushedFrom = {}))[`${tid}/${s.id}`] = s.mtime;
      bytes += s.size;
    });
    return done(`已推送 ${src.length} 份（${bytes} 字节）`, { uids: src.length, size: bytes });
  }

  /* 拉取：本地没有这个游戏的存档时，后端会退化成「主机上全部用户目录」逐个去远端试
   * （那是「救回丢失的存档」唯一可行的做法）。mock 用 MOCK_UIDS 代表那些用户目录。 */
  const cands = onlyUid ? [onlyUid] : (list.length ? list.map((s) => s.id) : MOCK_UIDS);
  let n = 0, files = 0, bytes = 0, skipped = 0, unchanged = 0;
  cands.forEach((uid) => {
    if (!gameState.remoteFiles.has(`${tid}/${uid}`)) { skipped++; return; }
    let rec = list.find((s) => s.id === uid);
    if (!rec) {
      /* 远端有、本地没有 → 这就是「拉回来」：补一条本地记录（后端是真的写盘）。 */
      rec = {
        id: uid, name: uid,
        kind: /^(CUSA|PPJB)/.test(tid) ? 'PS4' : 'PS5',
        mtime: mockStamp(new Date()),
        size: 3145728, files: 12,
      };
      list.push(rec);
    }
    /* [CN-FORK] v0.5.2.162 本地这份和推上去的那一份是同一个来源 → 后端不拉。
     * 拉取是整份覆盖，白跑一遍网络之外没有任何区别，所以后端回的是「已跳过」
     * 而不是「已拉取」—— mock 也得这么说，否则在这套预览里看到的文案和真机不一样。 */
    const from = gameState.pushedFrom && gameState.pushedFrom[`${tid}/${uid}`];
    if (from && from === rec.mtime) { unchanged++; return; }
    n++; files += rec.files; bytes += rec.size;
  });
  if (n === 0) {
    /* newer 那条排在 skipped 之前，与后端 note 组装的顺序一致。 */
    if (unchanged > 0)
      return done(`远程没有比本地更新的一份，已跳过拉取（${unchanged} 份未覆盖）`, { uids: 0 });
    return done(skipped > 0 ? '远程还没有这个游戏的存档' : '主机上没有可用的用户目录', { uids: 0 });
  }
  return done(`已拉取 ${n} 份（${files} 个文件 / ${bytes} 字节）`, { uids: n, files, size: bytes });
});

app.post('/game_save_test', (req, res) => {
  const b = req.body || {};
  const m = b.mode || 'smb';
  const host = (b[m] && (b[m].host || b[m].url)) || '';
  /* 故意留一个失败入口：主机填 0.0.0.0 或空就报失败，方便核对「测试连接」的失败样式。 */
  if (!host || host === '0.0.0.0') {
    return res.status(400).json({ error: 'EHOSTUNREACH', message: '连不上这个地址，请检查主机与端口' });
  }
  res.json({ ok: true, latency_ms: 8 + Math.floor(Math.random() * 30) });
});

app.get('/game_save_download', (req, res) => {
  res.setHeader('Content-Type', 'application/zip');
  res.setHeader('Content-Disposition', disposition(`${req.query.tid}-${req.query.save}.zip`));
  res.send(EMPTY_ZIP);
});

/* [CN-FORK] v0.5.2.172 两条下载路由（与后端同形：GET + query，回 zip 二进制）。
 * mock 不真打包，回一个空 zip 就够 —— 前端要验的是「链接指向对、点了真开始下载」。 */
app.get('/game_save_snap_download', (req, res) => {
  const { tid, uid, ts } = req.query;
  res.setHeader('Content-Type', 'application/zip');
  res.setHeader('Content-Disposition', disposition(`${tid}_${uid}_${ts}.zip`));
  res.send(EMPTY_ZIP);
});

app.get('/game_save_remote_download', (req, res) => {
  const { tid, save } = req.query;
  if (remoteGone.has(`${tid}/${save}`)) {
    return res.status(404).json({ error: 'ENOENT', message: '远程还没有这个游戏的存档' });
  }
  res.setHeader('Content-Type', 'application/zip');
  res.setHeader('Content-Disposition', disposition(`${tid}_${save}_remote.zip`));
  res.send(EMPTY_ZIP);
});

app.post('/game_save_upload', express.raw({ type: () => true, limit: '512mb' }), (req, res) => {
  const tid = String(req.query.tid || '');
  const uid = String(req.query.save || '');
  const user = resolveSaveUser(uid);
  if (!user) {
    return res.status(400).json({ error: 'EBAD', message: `主机上没有这个用户（${uid}）` });
  }
  const list = savesFor(tid);
  const size = (req.body && req.body.length) || 1048576;
  /* 整份替换（与后端「旧存档先改名留一手，换成功才删」同语义）。 */
  const rec = {
    id: user,
    name: user,
    kind: /^(CUSA|PPJB)/.test(tid) ? 'PS4' : 'PS5',
    mtime: '2026-10-05 21:00',
    size,
    files: Math.max(1, Math.round(size / 262144)),
  };
  const at = list.findIndex((s) => s.id === user);
  if (at >= 0) list[at] = rec; else list.push(rec);
  res.json({ ok: true, files: rec.files, size });
});

app.get('/game_save_delete', (req, res) => {
  const tid = String(req.query.tid || '');
  const uid = String(req.query.save || '');
  const list = savesFor(tid);
  const at = list.findIndex((s) => s.id === uid);
  if (at < 0) {
    return res.status(404).json({ error: 'ENOENT', message: '找不到这个用户的存档' });
  }
  list.splice(at, 1);
  res.json({ ok: true });
});

/* [CN-FORK] v0.5.2.154 garlic 兼容导入（earthonion/garlic-savemgr 的**加密**导出）。
 * 与后端 game_saves_import_garlic 同形：
 *   query: tid, save(=uid，可空 = 第一个用户), name(=原始文件名，裸镜像时必需)
 *   body : 裸的 `sdimg_<名>` 镜像，或 garlic 的 PS4 包（zip 内 sdimg_<名> + <名>.bin）
 * 语义是**逐文件合并**，不是整份替换 —— 所以这里只记一笔账、不动 saves 列表
 * （后端也一样：合并只覆盖同名的镜像，主机自己的记账原样保留）。
 * `name` 走 query 是关键契约（裸镜像没有后缀、也不是 zip，后端只能靠它决定落盘叫什么），
 * 而这条纯粹是前端拼 query —— mock 不把收到的 name 记下来，回归就完全测不到。 */
/* [CN-FORK] v0.5.2.165 与后端 gs_gx_name_for 同规则：garlic 的 PS5 导出名是
 * `<TID>_<镜像名>`（它 ui.html 里就是 `s.title_id + '_' + s.save_name`，而 save_name
 * 是盘上的 `sdimg_<dir_name>`），而主机只认后半截 —— 老代码把上传名原样写进存档目录，
 * 于是主机要找的 `sdimg_<dir_name>` 压根不存在（用户报的「导入后没生效」）。
 * 开头那段认得出来就剥掉，认不出（用户改过名）原样保留。
 *
 * [CN-FORK] v0.5.2.173 再加一步「改签」：剥掉**本机真实存在的 uid** 前缀
 * （`1a1d3627_sdimg_X` → `sdimg_X`）。与后端同一条原则 —— 只在那个前缀**确实是
 * /user/home 下的一个用户目录名**时才剥，认不出来一律原样返回（说不清 ≠ 要改名）。
 * 返回 `{ stored, resigned }`：`resigned` = 真的剥了 uid 前缀，回归靠它证「改签发生了」。 */
function gxStoredName(name) {
  let s = String(name || '');
  const m = /^[A-Za-z]{4}\d{5}_/.exec(s);
  if (m) s = s.slice(m[0].length);
  for (const u of MOCK_UIDS) {
    if (s.length > u.length && s[u.length] === '_'
        && s.slice(0, u.length).toLowerCase() === u) {
      return { stored: s.slice(u.length + 1), resigned: true };
    }
  }
  return { stored: s, resigned: false };
}

app.post('/game_save_import_garlic', express.raw({ type: () => true, limit: '512mb' }), (req, res) => {
  const tid = String(req.query.tid || '');
  const uid = String(req.query.save || '');
  const name = String(req.query.name || '');
  const body = req.body || Buffer.alloc(0);
  if (!tid) return res.status(400).json({ error: 'EINVAL', message: '缺少 tid' });
  if (!body.length) return res.status(400).json({ error: 'EINVAL', message: '导入的文件是空的' });
  const isZip = body.length >= 4
    && body[0] === 0x50 && body[1] === 0x4b && body[2] === 0x03 && body[3] === 0x04;
  /* 裸镜像必须带文件名 —— 与后端同一判据（后端是在建目录之前就判掉的）。 */
  if (!isZip && !name) {
    return res.status(400).json({
      error: 'EBAD', message: '缺少文件名（这个文件不是 zip，需要知道它叫什么）',
    });
  }
  const user = resolveSaveUser(uid);
  if (!user) {
    return res.status(400).json({ error: 'EBAD', message: `主机上没有这个用户（${uid}）` });
  }
  const files = isZip ? 2 : 1;
  /* [CN-FORK] v0.5.2.173 裸镜像按 gxStoredName 剥前缀（TID + 本机 uid）；zip 里自带名字。
   * `skippedManifest` = garlic 自己的 manifest.json 没被写进主机存档目录（后端同口径）。 */
  const gx = isZip ? { stored: name || '(zip 内自带)', resigned: false } : gxStoredName(name);
  gameState.garlicImports.push({
    tid,
    uid: user,
    /* uid 走的是 query 里那个**原样**值（空串也要看得见）—— 回归要能区分
     * 「前端没传」与「后端替你选了当前用户」这两种情况。 */
    uidQuery: uid,
    name: isZip ? (name || '(zip 内自带)') : name,
    /* 真正会落盘的名字（zip 里自带的名字原样，裸镜像按 gxStoredName 剥前缀）。 */
    stored: gx.stored,
    resigned: gx.resigned,
    skippedManifest: isZip,
    files, size: body.length, isZip,
  });
  res.json({ ok: true, files, uid: user });
});

/* 多媒体 */
app.get('/game_media', (req, res) => {
  const tid = String(req.query.tid || '');
  const base = mockGames.find((x) => x.tid === tid);
  if (!base || gameState.deleted.has(tid)) {
    return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
  }
  /* name（显示名）/ dlname（下载名）在这里现算、不进缓存 —— 游戏名可以改，
   * 缓存进 gameState.media 就把旧名字钉死了（后端也是每次扫库现算）。 */
  const items = mediaFor(tid).map((m) => ({
    ...m,
    name: `${gameNameFor(tid)}-${m.mtime}`,
    dlname: dlNameFor(tid, m.kind, m.mtime),
  }));
  res.json({ tid, items });
});

app.get('/game_media_thumb', (req, res) => {
  const id = String(req.query.id || '');
  const w = Math.max(64, Math.min(1280, Number(req.query.w) || 320));
  const kind = /-v\d+$/.test(id) ? 'video' : 'shot';
  const accent = kind === 'video' ? '#7c3aed' : '#0891b2';
  res.type('image/svg+xml').send(artSvg(kind === 'video' ? 'VIDEO' : 'SHOT', id.slice(-6), w, Math.round(w * 9 / 16), accent));
});

app.get('/game_media_file', (req, res) => {
  const id = String(req.query.id || '');
  const kind = /-v\d+$/.test(id) ? 'video' : 'shot';
  const accent = kind === 'video' ? '#7c3aed' : '#0891b2';
  /* dl=1 → 下发文件名（游戏名称-截图时间.<ext>），与后端 add_disposition 同口径。
   * 预览（<img>/<video>）不带 dl，免得 attachment 影响渲染。 */
  if (String(req.query.dl || '') === '1') {
    const tid = String(req.query.tid || '');
    const m = mediaFor(tid).find((x) => x.id === id);
    const nm = m ? dlNameFor(tid, m.kind, m.mtime) : `${id}.bin`;
    res.setHeader('Content-Disposition', disposition(nm));
  }
  /* 视频这里返回的其实还是一张 SVG —— <video> 解不了，但前端挂了 poster，
   * 所以预览里看到的是封面图 + 原生播放控件，布局与真实情况一致。
   * 主机侧的真实现由后端直接吐文件字节。 */
  res.type('image/svg+xml').send(artSvg(kind === 'video' ? 'VIDEO PREVIEW' : 'SCREENSHOT', id, 1280, 720, accent));
});

app.post('/game_media_pack', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  const ids = req.body && req.body.ids;
  const all = mediaFor(tid);
  const picked = Array.isArray(ids) ? all.filter((x) => ids.includes(x.id)) : all;
  if (picked.length === 0) {
    return res.status(400).json({ error: 'EINVAL', message: '没有选中任何项目' });
  }
  const bytes = picked.reduce((n, x) => n + x.size, 0);
  res.status(202).json({ job_id: startJob('pack', bytes, picked.length, { name: packNameFor(tid) }) });
});

app.get('/game_media_pack_get', (req, res) => {
  const st = jobStatus(Number(req.query.job));
  if (!st) return res.status(404).json({ error: 'ENOENT', message: '没有这个打包任务' });
  if (!st.done) return res.status(409).json({ error: 'EBUSY', message: '打包还没完成' });
  res.setHeader('Content-Type', 'application/zip');
  /* 包名与 job 状态里的 name 同源（后端也是从 aux 取 basename，只有一处算）。 */
  res.setHeader('Content-Disposition',
    disposition(st.name || `media-pack-${req.query.job}.zip`));
  res.send(EMPTY_ZIP);
});

app.get('/game_media_delete', (req, res) => {
  const tid = String(req.query.tid || '');
  const id = String(req.query.id || '');
  const list = mediaFor(tid);
  const at = list.findIndex((x) => x.id === id);
  if (at >= 0) list.splice(at, 1);
  res.json({ ok: true });
});

/* 改名 / 换磁贴背景 */
/* [CN-FORK] v0.5.2.132 返回 persisted + persist_note：名字源（游戏包里的
 * sce_sys/param.json）改成功了吗。分类口径与后端 game_title.c 完全一致 ——
 * fmt 是 image（ffpkg/exfat/ffpfs/ffpfsc）或 pkg 时源不可写，folder/lz4 才可写。
 * 三个分支在 mock 里都能触发，本地预览就能验到前端的提示分支，不必等实机：
 *   CUSA01111 / PPSA02000 → persisted:true
 *   PPSA01234 / PPSA00802 / EMUA90001 / PPSA01500 → 镜像，false
 *   PPJB01010 / PAUS00100 → 系统内置，false
 */
const IMG_FMTS = new Set(['ffpkg', 'exfat', 'ffpfs', 'ffpfsc']);
const FOLDER_FMTS = new Set(['folder', 'lz4']);
app.post('/game_rename', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  const name = String((req.body && req.body.name) || '').trim();
  if (!mockGames.find((x) => x.tid === tid)) {
    return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
  }
  if (!name) return res.status(400).json({ error: 'EINVAL', message: '名称不能为空' });
  gameState.renamed[tid] = name;
  gameState.hostNames[tid] = name;   /* 改名同时写主机索引（真机上是 tile_db_set_name） */
  listSnap = null; listSnapAt = 0;   /* 对齐后端 game_mgr_invalidate()：改名后快照立刻作废 */
  const fmt = mockGames.find((x) => x.tid === tid).fmt || 'pkg';
  let persisted = false;
  let persist_note = '';
  if (IMG_FMTS.has(fmt)) {
    persist_note = '镜像游戏：名字锁在镜像文件内部，无法持久修改';
  } else if (FOLDER_FMTS.has(fmt)) {
    persisted = true;
    persist_note = `已写入源文件 sce_sys/param.json（1 个语言）`;
  } else {
    persist_note = '系统内置安装：没有可写的游戏源';
  }
  /* [CN-FORK] v0.5.2.165 主机磁贴数据库（app.db 平铺列 + appinfo.db）那一层。
   * 后端 tile_db_set_name 的成败现在也回前端（host_ok / host_note）——
   * 以前只进主机日志，于是「库里压根没写进去」和「写进去了但 Shell 还没重绘」
   * 在界面上长得一样，用户看到的就是「改完名 PS5 上没生效」。
   * 真机默认成功；`POST /_host_rename_fail` 可以强制失败，好让回归走到红字分支。 */
  const hostOk = !gameState.hostRenameFail.has(tid);
  const hostNote = hostOk ? '' : '这个游戏还没注册进主机数据库';
  res.json({ ok: true, name, persisted, persist_note, host_ok: hostOk, host_note: hostNote });
});

/* [CN-FORK] v0.5.2.165 测试钩子：强制某个 tid 的「主机磁贴库写入」失败/恢复。
 * 真机没有这条路（失败就是失败），纯粹是为了让前端那条红字提示在本地就能验到。 */
app.post('/_host_rename_fail', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  if (!tid) return res.status(400).json({ error: 'EINVAL', message: '缺少 tid' });
  if (req.body && req.body.fail === false) gameState.hostRenameFail.delete(tid);
  else gameState.hostRenameFail.add(tid);
  res.json({ ok: true, tid, fail: gameState.hostRenameFail.has(tid) });
});

/* [CN-FORK] v0.5.2.137 名字回填。对应真机 POST /game_title_sync（后端 game_title_sync_all）。
 *
 * 语义按后端如实模拟：
 *   scanned   = 有 name.txt 的款数（mock 里 = renamed 里有值的）
 *   applied   = 主机索引处理成功的款数（能定位到游戏就都算成功，真机同口径：
 *               「已写好」与「本来就一致」都计入）
 *   persisted = 名字**源**也写成功的款数 —— 只有 folder / lz4 类可能（镜像与 pkg 写不了源），
 *               真机上这一项就是镜像类为 0 的原因，回归要能区分出来
 *   failed    = mock 没有写失败这条路，恒 0
 * 幂等：后端已经一致的款一次写都不发；这里表现为重复点两次结果一样（hostNames 被覆盖成同值）。 */
app.post('/game_title_sync', (req, res) => {
  let scanned = 0, applied = 0, persisted = 0;
  for (const tid of Object.keys(gameState.renamed)) {
    const g = mockGames.find((x) => x.tid === tid);
    if (!g) continue;                       /* 名字还在但游戏已删：与后端一样跳过 */
    scanned++;
    gameState.hostNames[tid] = gameState.renamed[tid];  /* ← 这条链真正做的事 */
    applied++;
    if (FOLDER_FMTS.has(g.fmt)) persisted++;
  }
  res.json({
    ok: true, scanned, applied, persisted, failed: 0,
    note: `回填 ${scanned} 款：主机索引 ${applied}、名字源 ${persisted}、失败 0`,
  });
});

/* 测试钩子（不在产品契约里）：
 *   GET  /_host_names  → 主机索引里现在的名字（真机上是 app.db 的平铺名字列）
 *   POST /_host_drift  → 模拟「SMP 重扫按游戏源重建 app.db」，把主机索引里的名字全丢掉
 * 有了这两个，回归才能断言「漂了之后点同步，主机索引真的被补回来」——
 * 否则这条链在 mock 里就没有任何可观察的效果，测试等于只断言了按钮存在。 */
app.get('/_host_names', (req, res) => res.json({ host_names: gameState.hostNames }));
app.post('/_host_drift', (req, res) => {
  const n = Object.keys(gameState.hostNames).length;
  gameState.hostNames = {};
  res.json({ ok: true, dropped: n });
});

/* [CN-FORK] v0.5.2.142 自动存档的运行期状态钩子。
 * 后端那侧 `running` 是靠 sceLncUtil 当场探测的，mock 里没法探测 —— 用它摆布，
 * 回归才能断言「游戏在跑 → 界面上出现『游戏运行中』」以及「退出后那一条变成推送」。
 * remote_files 只读，用来验「推送真的把远端写上了」而不是只看响应。 */
app.post('/_set_running', (req, res) => {
  gameState.runningTid = String((req.body && req.body.tid) || '');
  res.json({ ok: true, running: gameState.runningTid });
});
app.get('/_remote_files', (req, res) => res.json({ files: [...gameState.remoteFiles].sort() }));

/* [CN-FORK] v0.5.2.154 garlic 导入的流水账（只读）。
 * 记的是「后端收到了什么」：tid / 落到了哪个 uid / name 是什么 / 判成 zip 还是裸镜像。
 * 回归靠它验两件事：① 前端把 file.name 原样传上来了（裸镜像全靠它）；② 导入**没有**
 * 让存档列表多出一条（合并 ≠ 整份替换）。 */
app.get('/_garlic_imports', (req, res) => res.json({ imports: gameState.garlicImports }));

/* [CN-FORK] v0.5.2.166 背景音乐「两份落点」的只读钩子 + 两个故障注入开关。
 *
 *   GET  /_music_state        → { panel:{tid:{name,bytes,ext}}, host:{tid:{bytes,lufs}} }
 *   POST /_music_host_fail    → {tids:[...]} 让这些 tid 的 /game_music_at9 回 400 EHOST
 *   POST /_tile_group_broken  → {broken:true|false} 让主机固定列表读/写一律失败
 *
 * 为什么非要这两个钩子：用户这一轮报的就是「背景音乐改动没有生效」与「置顶按钮在 PS5 里
 * 没有生效」——两件事的共同点是**主机侧那一半写没写**在界面上看不出来（旧版一律报「已应用」）。
 * 回归要能分别断言「面板那份在」「主机那份也在」，以及「主机那份失败时界面确实报红」，
 * 就得能把两份分开读出来、并且能强制其中一份失败。只断言按钮存在等于没测。 */
app.get('/_music_state', (req, res) => res.json({ panel: gameState.music, host: gameState.musicHost }));
app.post('/_music_host_fail', (req, res) => {
  const tids = (req.body && Array.isArray(req.body.tids)) ? req.body.tids.map(String) : [];
  gameState.musicHostFail = tids;
  res.json({ ok: true, tids });
});
app.post('/_tile_group_broken', (req, res) => {
  gameState.tileGroupBroken = !!(req.body && req.body.broken);
  res.json({ ok: true, broken: gameState.tileGroupBroken });
});

/* ══════════════════════════════════════════════════════════════════════════════
 * [CN-FORK] v0.5.2.112 磁贴背景的 DDS 转换（PNG 解码 + DXT1 编码），在 mock 里**真做一遍**
 *
 * 为什么要在 mock 里真写一份编码器，而不是回个 `{ok:true}` 了事：
 *   这一层唯一的硬契约就是**输出规格** —— 1920×1080 DXT1，总长 128 + 480*270*8 = 1,036,928 字节，
 *   头布局与控制台已验证的 `assets/pic0.dds` 逐字节一致。桩如果不真编码，回归就只能断言
 *   「前端把 PNG 传上来了」，而「规格对不对」这件事会一路拖到实机才暴露。
 *   写一份之后，回归可以直接量长度、查 fourCC，规格漂了立刻红。
 *
 * 两个实现都是**故意从简**的，与仓库既有实现保持同源，不引第三方库：
 *   · PNG 解码：只吃 canvas.toBlob('image/png') 会产出的那一种 —— 8 位、非隔行、
 *     colorType 6(RGBA) 或 2(RGB)。逐行反滤波（5 种 filter）后展成 RGBA。
 *   · DXT1 编码：端点色按**逐通道最大/最小**取（与 `avatar.c` 的 DXT5 同一条简化路子，
 *     也与 `make_pic0.py` 依赖的 Pillow BC1 同量级）。它不做端点搜索、画质一般，
 *     但**规格正确**，而我们这一层要验的正是规格。
 * 故意用 3 色模式（color0 > color1）而不是 4 色：DXT1 的 4 色模式会把两个索引让给
 * 「全黑 / 全白」，游戏封面这类图用 3 色插值更稳。
 * ══════════════════════════════════════════════════════════════════════════════ */

const PNG_SIG = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

/* 反滤波：每行第一个字节是 filter 类型，后面是该行像素。
 * bpp 是一像素的字节数（RGBA=4 / RGB=3），Paeth 与 Average 都要它。 */
function pngUnfilter(raw, w, h, bpp) {
  const stride = w * bpp;
  const out = Buffer.alloc(stride * h);
  for (let y = 0; y < h; y++) {
    const filt = raw[y * (stride + 1)];
    const src = y * (stride + 1) + 1;
    const dst = y * stride;
    const up = dst - stride;
    for (let i = 0; i < stride; i++) {
      const x = raw[src + i];
      const a = i >= bpp ? out[dst + i - bpp] : 0;      // 左侧
      const b = y > 0 ? out[up + i] : 0;                // 上方
      const c = (i >= bpp && y > 0) ? out[up + i - bpp] : 0; // 左上
      let v;
      switch (filt) {
        case 0: v = x; break;
        case 1: v = x + a; break;
        case 2: v = x + b; break;
        case 3: v = x + ((a + b) >> 1); break;
        case 4: {
          const p = a + b - c;
          const pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
          v = x + (pa <= pb && pa <= pc ? a : (pb <= pc ? b : c));
          break;
        }
        default: v = x;
      }
      out[dst + i] = v & 0xff;
    }
  }
  return out;
}

/* PNG → { w, h, rgba }。解不出来就返回 null（调用方如实降级，别把好数据也判成坏）。 */
function decodePng(buf) {
  try {
    if (!buf || buf.length < 8 || !buf.subarray(0, 8).equals(PNG_SIG)) return null;
    let w = 0, h = 0, colorType = 0, bitDepth = 0, interlace = 0;
    const idat = [];
    let p = 8;
    while (p + 8 <= buf.length) {
      const len = buf.readUInt32BE(p);
      const type = buf.toString('ascii', p + 4, p + 8);
      const data = buf.subarray(p + 8, p + 8 + len);
      if (type === 'IHDR') {
        w = data.readUInt32BE(0); h = data.readUInt32BE(4);
        bitDepth = data[8]; colorType = data[9]; interlace = data[12];
      } else if (type === 'IDAT') {
        idat.push(data);
      } else if (type === 'IEND') {
        break;
      }
      p += 12 + len;
    }
    if (!w || !h) return null;
    /* 只认 8 位、非隔行、RGB/RGBA —— 别的形态 canvas 不会产出，不猜。 */
    if (bitDepth !== 8 || interlace !== 0 || (colorType !== 6 && colorType !== 2)) return null;
    const bpp = colorType === 6 ? 4 : 3;
    const raw = zlib.inflateSync(Buffer.concat(idat));
    if (raw.length < h * (w * bpp + 1)) return null;
    const px = pngUnfilter(raw, w, h, bpp);
    if (bpp === 4) return { w, h, rgba: px };
    /* RGB → RGBA，alpha 一律 255 */
    const rgba = Buffer.alloc(w * h * 4);
    for (let i = 0, j = 0; i < w * h; i++, j += 3) {
      rgba[i * 4] = px[j]; rgba[i * 4 + 1] = px[j + 1]; rgba[i * 4 + 2] = px[j + 2]; rgba[i * 4 + 3] = 255;
    }
    return { w, h, rgba };
  } catch {
    return null;
  }
}

function rgb888to565(r, g, b) {
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
}
function rgb565to888(c) {
  return [((c >> 11) & 0x1f) << 3, ((c >> 5) & 0x3f) << 2, (c & 0x1f) << 3];
}

/* 一个 4×4 块 → 8 字节 DXT1。与 avatar.c 的 color 部分同一路子（逐通道 min/max 取端点）。 */
function compressDxt1Block(px, w, bx, by, rgba, out, off) {
  const min = [255, 255, 255], max = [0, 0, 0];
  for (let y = 0; y < 4; y++) {
    const py = Math.min(by * 4 + y, px.h - 1);   // 右侧/底部的残缺块按边缘像素平铺，不是补零
    for (let x = 0; x < 4; x++) {
      const pxi = Math.min(bx * 4 + x, px.w - 1);
      const i = (py * px.w + pxi) * 4;
      for (let c = 0; c < 3; c++) {
        const v = rgba[i + c];
        if (v < min[c]) min[c] = v;
        if (v > max[c]) max[c] = v;
      }
    }
  }
  let c0 = rgb888to565(max[0], max[1], max[2]);
  let c1 = rgb888to565(min[0], min[1], min[2]);
  if (c0 < c1) { const t = c0; c0 = c1; c1 = t; }
  else if (c0 === c1 && c0 < 0xffff) c0++;    // 保证 c0 > c1，走 3 色插值模式
  const p0 = rgb565to888(c0), p1 = rgb565to888(c1);
  const pal = [p0, p1, [0, 0, 0], [0, 0, 0]];
  for (let c = 0; c < 3; c++) {
    pal[2][c] = Math.floor((2 * p0[c] + p1[c]) / 3);
    pal[3][c] = Math.floor((p0[c] + 2 * p1[c]) / 3);
  }
  let idx = 0;
  for (let y = 0; y < 4; y++) {
    const py = Math.min(by * 4 + y, px.h - 1);
    for (let x = 0; x < 4; x++) {
      const pxi = Math.min(bx * 4 + x, px.w - 1);
      const i = (py * px.w + pxi) * 4;
      let best = 0, bestD = Infinity;
      for (let j = 0; j < 4; j++) {
        const dr = rgba[i] - pal[j][0], dg = rgba[i + 1] - pal[j][1], db = rgba[i + 2] - pal[j][2];
        const d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = j; }
      }
      idx |= best << ((y * 4 + x) * 2);
    }
  }
  out.writeUInt16LE(c0, off);
  out.writeUInt16LE(c1, off + 2);
  out.writeUInt32LE(idx >>> 0, off + 4);
}

/* DDS 头 —— 字段布局与 `make_pic0.py` 的 build_bc1_header 逐字节一致
 * （pixel-format 块在 76、dwFlags 0x000A1007、线性长度写整个压缩体、mipMapCount 1、
 * bitCount 0、reserved1[0] 写 1）。那张 pic0.dds 是控制台已验证能被读的参考，
 * 所以这里不做任何「顺手改进」：多一个变量，实机出问题时就要多排一天。 */
function buildBc1Header(w, h) {
  const linear = ((w + 3) >> 2) * ((h + 3) >> 2) * 8;
  const head = Buffer.alloc(128);
  head.write('DDS ', 0, 'ascii');
  head.writeUInt32LE(124, 4);
  head.writeUInt32LE(0x000a1007, 8);
  head.writeUInt32LE(h, 12);
  head.writeUInt32LE(w, 16);
  head.writeUInt32LE(linear, 20);
  head.writeUInt32LE(1, 24);            // mipMapCount
  head.writeUInt32LE(1, 28);            // reserved1[0]（参考图写 1，照抄）
  head.writeUInt32LE(32, 76);           // pf dwSize
  head.writeUInt32LE(4, 80);            // DDPF_FOURCC
  head.write('DXT1', 84, 'ascii');
  head.writeUInt32LE(0x1000, 108);      // DDSCAPS_TEXTURE
  return head;
}

function encodeDxt1Dds(png) {
  const bw = (png.w + 3) >> 2, bh = (png.h + 3) >> 2;
  const out = Buffer.alloc(128 + bw * bh * 8);
  buildBc1Header(png.w, png.h).copy(out, 0);
  let off = 128;
  for (let by = 0; by < bh; by++) {
    for (let bx = 0; bx < bw; bx++) {
      compressDxt1Block(png, png.w, bx, by, png.rgba, out, off);
      off += 8;
    }
  }
  return out;
}

/* ══════════════════════════════════════════════════════════════════════════════
 * [CN-FORK] v0.5.2.112 磁贴两层图：图标（封面格，1:1 PNG）与背景（整卡底图，16:9）
 *
 * 两套端点是刻意分开的（契约见 utils/gameApi.js 顶部）—— 用户要能「只换背景保住原封面」，
 * 合成一层就做不到。实现上两份几乎一样，所以用一张表驱动，将来加第三层只加一行。
 *
 * 存储与呈现分开：上传的 PNG 原样留着供 <img> 预览（浏览器显示不了 DDS），
 * 另外真转一份 DDS 留在 /game_tile_bg_dds 供核对 —— 这也正好把「后端该转成什么规格」
 * 变成了 mock 里可执行的事实。
 * ══════════════════════════════════════════════════════════════════════════════ */
const TILE_IMAGE_KINDS = {
  icon: { bucket: () => gameState.icon, path: 'game_tile_icon', accent: '#1d4ed8' },
  bg: { bucket: () => gameState.bg, path: 'game_tile_background', accent: '#b45309' },
};

Object.keys(TILE_IMAGE_KINDS).forEach((kind) => {
  const K = TILE_IMAGE_KINDS[kind];
  app.post(`/${K.path}`, express.raw({ type: () => true, limit: '64mb' }), (req, res) => {
    const tid = String(req.query.tid || '');
    if (!mockGames.find((x) => x.tid === tid)) {
      return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
    }
    const bytes = req.body || Buffer.alloc(0);
    const bucket = K.bucket();
    bucket[tid] = (bucket[tid] || 0) + 1;
    listSnap = null; listSnapAt = 0;   /* 对齐后端 game_mgr_invalidate() */
    const png = decodePng(bytes);
    const store = kind === 'icon' ? gameState.iconBytes : gameState.bgBytes;
    store[tid] = bytes;
    let ddsBytes = 0;
    if (kind === 'bg') {
      /* 背景这一层要真转 DDS。转不动就把上一次的结果清掉，别留一份对不上的旧文件
       * ——「接口说成功、下载下来是别的规格」比直接失败更难查。 */
      if (png) {
        const dds = encodeDxt1Dds(png);
        gameState.bgDds[tid] = dds;
        ddsBytes = dds.length;
      } else {
        delete gameState.bgDds[tid];
      }
      console.log(`[Mock] ${K.path} tid=${tid} png=${bytes.length}B`
        + (png ? ` ${png.w}x${png.h} -> dds=${ddsBytes}B` : ' (PNG 解不出来，未生成 DDS)'));
    } else {
      console.log(`[Mock] ${K.path} tid=${tid} png=${bytes.length}B`
        + (png ? ` ${png.w}x${png.h}` : ''));
    }
    res.json({ ok: true, bytes: bytes.length, dds_bytes: ddsBytes });
  });

  app.get(`/${K.path}_reset`, (req, res) => {
    const tid = String(req.query.tid || '');
    delete K.bucket()[tid];
    listSnap = null; listSnapAt = 0;   /* 对齐后端 game_mgr_invalidate() */
    const store = kind === 'icon' ? gameState.iconBytes : gameState.bgBytes;
    delete store[tid];
    if (kind === 'bg') delete gameState.bgDds[tid];
    res.json({ ok: true });
  });
});

/* 图标：自定义过就回它自己的 PNG 字节；没设过回那张带 TID 的占位 SVG（与 /game_icon 同一份数据源，
 * 保证「设过 / 没设过」两种状态在界面上一眼可辨）。 */
app.get('/game_tile_icon', (req, res) => {
  const tid = String(req.query.tid || '');
  if (!gameState.icon[tid]) return res.status(404).type('text/plain').send('not found');
  const bytes = gameState.iconBytes[tid];
  if (bytes && bytes.length) {
    res.type('image/png');
    return res.send(bytes);
  }
  res.type('image/svg+xml').send(artSvg(tid, `custom icon #${gameState.icon[tid]}`, 512, 512, TILE_IMAGE_KINDS.icon.accent));
});

/* 背景：这里回的是 **PNG 预览**（浏览器 <img> 显示不了 DDS）。真实后端要么留着上传的原图，
 * 要么把 DDS 转回 PNG，两条路都行 —— 前端只要求「这一路能显示」。 */
app.get('/game_tile_bg', (req, res) => {
  const tid = String(req.query.tid || '');
  if (!gameState.bg[tid]) return res.status(404).type('text/plain').send('not found');
  const bytes = gameState.bgBytes[tid];
  if (bytes && bytes.length) {
    res.type('image/png');
    return res.send(bytes);
  }
  res.type('image/svg+xml').send(artSvg(tid, `custom bg #${gameState.bg[tid]}`, 512, 512, TILE_IMAGE_KINDS.bg.accent));
});

/* DDS 原始字节：给下载核对与回归验规格用（1920×1080 DXT1 → 1,036,928 字节）。 */
app.get('/game_tile_bg_dds', (req, res) => {
  const tid = String(req.query.tid || '');
  const dds = gameState.bgDds[tid];
  if (!gameState.bg[tid] || !dds) return res.status(404).type('text/plain').send('not found');
  res.type('application/octet-stream');
  res.send(dds);
});

/* ══════════════════════════════════════════════════════════════════════════════
 * [CN-FORK] v0.5.2.163 背景音乐（POST 上传 / GET 读取 / GET 恢复默认）
 *
 * 与后端 game_music.c 同形：落 <TID>.<ext>，上限 30MB（前端软提示），只认 mp3/ogg/wav。
 * GET 回的是一段**真能解码**的 8kHz 单声道 WAV（不是随便一段字节）——
 * 这样 <audio> 预览在本地预览里不会变成「加载失败」，回归也能验到「设过之后能播」。
 * ══════════════════════════════════════════════════════════════════════════════ */
const MUSIC_MAX = 30 * 1024 * 1024;
const MUSIC_EXT = { mp3: 'audio/mpeg', ogg: 'audio/ogg', wav: 'audio/wav' };

function tinyWav() {
  const rate = 8000, n = 800; /* 0.1 秒静音 */
  const buf = Buffer.alloc(44 + n);
  buf.write('RIFF', 0); buf.writeUInt32LE(36 + n, 4); buf.write('WAVE', 8);
  buf.write('fmt ', 12); buf.writeUInt32LE(16, 16); buf.writeUInt16LE(1, 20);
  buf.writeUInt16LE(1, 22); buf.writeUInt32LE(rate, 24); buf.writeUInt32LE(rate, 28);
  buf.writeUInt16LE(1, 32); buf.writeUInt16LE(8, 34);
  buf.write('data', 36); buf.writeUInt32LE(n, 40);
  buf.fill(128, 44);
  return buf;
}

app.post('/game_music', express.raw({ type: () => true, limit: '64mb' }), (req, res) => {
  const tid = String(req.query.tid || '');
  const name = String(req.query.name || '');
  if (!mockGames.find((x) => x.tid === tid)) {
    return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
  }
  const body = req.body || Buffer.alloc(0);
  if (!body.length) return res.status(400).json({ error: 'EBAD', message: '音频文件是空的' });
  const ext = (name.split('.').pop() || '').toLowerCase();
  if (!MUSIC_EXT[ext]) {
    return res.status(400).json({ error: 'EBAD', message: '音频不合法（仅支持 MP3 / OGG / WAV）' });
  }
  if (body.length > MUSIC_MAX) {
    return res.status(400).json({ error: 'EBAD', message: '音频文件过大（30MB 上限）' });
  }
  gameState.music[tid] = { name, bytes: body.length, ext };
  console.log(`[Mock] game_music tid=${tid} name=${name} ${body.length}B -> .${ext}`);
  res.json({ ok: true, bytes: body.length, name });
});

app.get('/game_music', (req, res) => {
  const tid = String(req.query.tid || '');
  const m = gameState.music[tid];
  if (!m) return res.status(404).type('text/plain').send('not found');
  res.type(MUSIC_EXT[m.ext] || 'application/octet-stream');
  res.send(tinyWav());
});

app.get('/game_music_reset', (req, res) => {
  const tid = String(req.query.tid || '');
  delete gameState.music[tid];
  /* [CN-FORK] v0.5.2.166 主机那份（sce_sys/snd0.at9）一起还原 ——
   * 后端在同一条 /game_music_reset 里就做了这件事（见 include/game_music.h），
   * mock 不跟上的话「恢复默认」在本地预览里会看起来没清掉主机那份。 */
  delete gameState.musicHost[tid];
  res.json({ ok: true });
});

/* ══════════════════════════════════════════════════════════════════════════════
 * [CN-FORK] v0.5.2.166 把编好的 ATRAC9 写进主机 <sce_sys>/snd0.at9
 *   POST /game_music_at9?tid=&lufs=   裸 body（.at9 字节）→ {ok,bytes,host,message}
 *
 * 与 /game_music 是**两条不同的路**，别再合并：
 *   /game_music      = 用户原始音频（mp3/ogg/wav），只落面板目录，给 <audio> 试听；
 *   /game_music_at9  = 浏览器里刚编出来的 .at9，只写主机 sce_sys/snd0.at9。
 * 校验里有一条真机规格：ATRAC9 是 RIFF/WAVE 容器（'RIFF' + 'WAVE' + fmt 的 0xFFFE），
 * 而且主机上限 2 MiB。mock 只验头部与大小 —— 那两条正好是前端最可能搞错的地方。
 * ══════════════════════════════════════════════════════════════════════════════ */
const AT9_LIMIT = 2 * 1024 * 1024;

function looksLikeAt9(buf) {
  if (!buf || buf.length < 168) return false;
  if (buf.toString('ascii', 0, 4) !== 'RIFF') return false;
  if (buf.toString('ascii', 8, 12) !== 'WAVE') return false;
  return true;
}

app.post('/game_music_at9', express.raw({ type: () => true, limit: '16mb' }), (req, res) => {
  const tid = String(req.query.tid || '');
  const lufs = String(req.query.lufs || '');
  if (!mockGames.find((x) => x.tid === tid)) {
    return res.status(404).json({ error: 'ENOENT', message: '主机上找不到这个游戏' });
  }
  const body = req.body || Buffer.alloc(0);
  if (!body.length) return res.status(400).json({ error: 'EBAD', message: '音频是空的' });
  if (!looksLikeAt9(body)) {
    return res.status(400).json({ error: 'EBAD', message: '这不是合法的 ATRAC9（RIFF/WAVE 头对不上）' });
  }
  if (body.length > AT9_LIMIT) {
    return res.status(400).json({ error: 'EBAD', message: '超过主机上限 2 MB' });
  }
  if (gameState.musicHostFail.includes(tid)) {
    return res.status(400).json({ error: 'EHOST', message: '主机端没写上（测试强制）' });
  }
  gameState.musicHost[tid] = { bytes: body.length, lufs };
  console.log(`[Mock] game_music_at9 tid=${tid} ${body.length}B lufs=${lufs || '-'} -> sce_sys/snd0.at9`);
  res.json({ ok: true, bytes: body.length, host: true, message: '已写入主机 sce_sys/snd0.at9' });
});

/* ══════════════════════════════════════════════════════════════════════════════
 * [CN-FORK] v0.5.2.164 游戏磁贴的收藏 / 置顶（面板侧列表排序）
 *   GET  /game_prefs              → {"prefs":{...},"order":[...]}
 *   POST /game_prefs_set {tid,fav?,pin?} → {ok,tid,fav,pin}
 * 与后端 game_prefs.c 同一套三态语义：只拨一颗按钮时另一项**原样保留**。
 * 响应回的是**改完之后**的最终状态（不是入参）—— 回入参会把「不动」的 -1 说成 false。
 * [CN-FORK] v0.5.2.166 响应多一个 `order`（面板侧手工顺序，由 /tile_group_order 写）。
 * ══════════════════════════════════════════════════════════════════════════════ */
app.get('/game_prefs', (req, res) => {
  res.set('Cache-Control', 'no-store');
  res.json({ prefs: gameState.prefs, order: gameState.order });
});

app.post('/game_prefs_set', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  if (!/^[A-Z0-9]{6,16}$/.test(tid)) {
    return res.status(400).json({ error: 'EBAD', message: '游戏编号不合法' });
  }
  const hasFav = req.body && typeof req.body.fav === 'boolean';
  const hasPin = req.body && typeof req.body.pin === 'boolean';
  if (!hasFav && !hasPin) {
    return res.status(400).json({ error: 'EINVAL', message: 'fav / pin 至少给一个' });
  }
  const cur = gameState.prefs[tid] || { fav: false, pin: false };
  const next = {
    fav: hasFav ? req.body.fav === true : !!cur.fav,
    pin: hasPin ? req.body.pin === true : !!cur.pin,
  };
  if (next.fav || next.pin) gameState.prefs[tid] = next;
  else delete gameState.prefs[tid];
  logs.push(`[PLDMGR] game_prefs: ${tid} fav=${next.fav ? 1 : 0} pin=${next.pin ? 1 : 0}`);
  res.json({ ok: true, tid, fav: next.fav, pin: next.pin });
});

/* ══════════════════════════════════════════════════════════════════════════════
 * [CN-FORK] v0.5.2.166 主机「主屏固定列表」（app.db 的 tbl_group_<uid>，MyHome 组）
 *   GET  /tile_group                  → {ok,group,accounts,items:[{itemId,idx}],note?}
 *   POST /tile_group_pin {tid,pinned}  → {ok,tid,pinned,accounts,idx,message}
 *   POST /tile_group_order {order:[]}  → {ok,moved,host_ok,message}
 *
 * 语义照 include/tile_group.h 的契约（真机取证 + 写哪些账号 + 改完什么时候看得见）：
 *   · pin   = 把 tid 移到最前、其余依次后移、重新编号 1..N；取消 = 删掉该条 + 重编号；
 *             幂等（已在最前 / 本来就不在 → accounts=0，一条都不写）。
 *   · order = **刻意保守**：只有「在 order 里**且**当前就在 MyHome 组里」的条目会动，
 *             它们**自己占的那几个 idx 槽位**不变，只是按 order 给的先后在它们之间重新分配；
 *             少于 2 条命中就什么都不做。没被提到的条目一个字节都不动。
 *             mock 必须照着这条实现，否则回归会「本地全绿、真机把没列到的游戏也固定进主屏」。
 *   · 面板侧的 order（gameState.order）是**全量**保存的：那份管的是面板列表顺序，
 *     与主机固定列表是两回事（主机只管它自己组里那几个）。
 * ══════════════════════════════════════════════════════════════════════════════ */
app.get('/tile_group', (req, res) => {
  res.set('Cache-Control', 'no-store');
  if (gameState.tileGroupBroken) {
    return res.json({ ok: false, group: 'MyHome', accounts: 0, items: [], note: '主机 app.db 里没有 tbl_group 表（测试强制）' });
  }
  res.json({ ok: true, group: gameState.tileGroup.group, accounts: gameState.tileGroup.accounts, items: gameState.tileGroup.items });
});

app.post('/tile_group_pin', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  const pinned = !!(req.body && req.body.pinned);
  if (!/^[A-Z0-9]{6,16}$/.test(tid)) {
    return res.status(400).json({ error: 'EBAD', message: '游戏编号不合法' });
  }
  if (gameState.tileGroupBroken) {
    return res.status(400).json({ error: 'EHOST', message: '主机 app.db 里没有 tbl_group 表（测试强制）' });
  }
  const items = gameState.tileGroup.items;
  const at = items.findIndex((x) => x.itemId === tid);
  if (pinned) {
    /* 已在最前 → 幂等，一条都不写（后端同样返回 accounts=0）。 */
    if (at === 0) return res.json({ ok: true, tid, pinned: true, accounts: 0, idx: items[0].idx, message: '已经在最前' });
    if (at > 0) items.splice(at, 1);
    items.unshift({ itemId: tid, idx: 1 });
    items.forEach((x, i) => { x.idx = i + 1; });
    console.log(`[Mock] tile_group_pin ${tid} -> 第 1 位（${items.length} 条）`);
    return res.json({ ok: true, tid, pinned: true, accounts: gameState.tileGroup.accounts, idx: 1, message: '已写入主机固定列表' });
  }
  if (at < 0) return res.json({ ok: true, tid, pinned: false, accounts: 0, idx: 0, message: '本来就不在固定列表里' });
  items.splice(at, 1);
  items.forEach((x, i) => { x.idx = i + 1; });
  console.log(`[Mock] tile_group_pin ${tid} -> 取消置顶（剩 ${items.length} 条）`);
  res.json({ ok: true, tid, pinned: false, accounts: gameState.tileGroup.accounts, idx: 0, message: '已从主机固定列表移除' });
});

app.post('/tile_group_order', (req, res) => {
  const order = (req.body && Array.isArray(req.body.order)) ? req.body.order : null;
  if (!order) return res.status(400).json({ error: 'EINVAL', message: '缺少 order 数组' });
  for (const x of order) {
    if (typeof x !== 'string' || !/^[A-Z0-9]{6,16}$/.test(x)) {
      return res.status(400).json({ error: 'EBAD', message: '游戏编号不合法' });
    }
  }
  /* 面板侧：全量存下（管面板列表顺序）。 */
  gameState.order = order.slice();

  if (gameState.tileGroupBroken) {
    return res.json({ ok: true, moved: 0, host_ok: false, message: '主机 app.db 里没有 tbl_group 表（测试强制）' });
  }

  /* 主机侧：**只重排槽位，不增不删**（逐行对齐 src/tile_group.c 的 tg_order_cb）。
   *
   * ⚠ 槽位池 = **被移动的那几条自己占的 idx**（不是整组的 idx）。
   * 抄错这一步的后果不是「结果差一点」，而是**idx 撞号**：真机上两条磁贴会同时拿
   * idx=1，主屏顺序随机。所以这里必须与 C 那份逐字对齐：
   *   · 选出「在组里 **且** 出现在 order 里」的条目，记下它的名次 rank 与**原来的 idx**；
   *   · 槽位序列 = 这些 orig 升序（真机是按 idx 升序读出表的，天然升序）；
   *   · 少于 2 条 → 直接返回，没有「换位」可言（C 里的 `if (ns < 2) return 0;`）；
   *   · 按 rank 排好后，第 i 名拿第 i 个槽位；只有真的换了位置才计一条 moved。 */
  const items = gameState.tileGroup.items;
  const rank = new Map();
  order.forEach((t, i) => { if (!rank.has(t)) rank.set(t, i); });
  const members = items.filter((x) => rank.has(x.itemId));
  if (members.length < 2) {
    console.log(`[Mock] tile_group_order ${order.length} 项 -> 组里只命中 ${members.length} 条，不动`);
    return res.json({ ok: true, moved: 0, host_ok: true, message: '顺序没有变化' });
  }
  const slots = members.map((x) => x.idx).sort((a, b) => a - b);
  const moving = members.slice().sort((a, b) => rank.get(a.itemId) - rank.get(b.itemId));
  let moved = 0;
  moving.forEach((x, i) => {
    if (x.idx !== slots[i]) moved += 1;
    x.idx = slots[i];
  });
  console.log(`[Mock] tile_group_order ${order.length} 项 -> 主机重排 ${moved} 条`);
  res.json({ ok: true, moved, host_ok: true, message: moved > 0 ? `主机固定列表已重排 ${moved} 条` : '顺序没有变化' });
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
 * 避免对「已在跑但只监听 127.0.0.1」的程序重复拉起叠出第二个实例。未拉起的 ShadowMountPlus 不在表内。
 * v0.5.2.98 补齐 memory / is_daemon：后端 process_mgr.c 每条都带这两个字段，mock 漏了会让
 * 前端 p.memory.toFixed(1) 直接抛 TypeError（勾「显示所有系统进程」即崩）。 */
const mockProcs = ['elfldr.elf', 'NayePlmTools.elf', 'pkgmgr.elf', 'dbst.elf', 'kstuff.elf'];
const mockMemMiB = [128.4, 96.2, 210.7, 44.1, 12.3];
app.get('/processes_list', (req, res) => {
  res.json({
    processes: mockProcs.map((name, i) => ({
      pid: 89 + i,
      name,
      memory: mockMemMiB[i],
      is_daemon: name !== 'elfldr.elf',
    })),
  });
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

/* [CN-FORK] v0.5.2.158 「个性化软件」mock。
 * 数据照真机 _ftp_probe/app.db 里那 19 行 NPXS 内置软件抄，hidden 的初值也照它抄。
 *
 * [CN-FORK] v0.5.2.160 加上 `controllable` —— 与 src/app_visibility.c 里 AV_FIXED 同一张表：
 * 「两个内容保存库」真机实测写 visible=0 主屏照旧画，「编辑 / 光碟占位」是系统自己
 * 决定什么时候画的条目。mock 必须照抄这份边界，否则前端在这套预览里会给它们画开关、
 * 上真机又变成另一回事。
 * [CN-FORK] v0.5.2.161 用户真机口径纠正了名单：「所有应用程序、欢迎 是可以编辑显示和隐藏的」
 * —— `.160` 把这两条错关进固定名单。改判据只认「写了 0、再看主屏」，别拿出厂值反推。 */
const MOCK_FIXED_APPS = {
  NPXS40071: '主屏固定入口：实测把它写成隐藏，主屏照旧画出来',
  NPXS40139: '主屏固定入口：实测把它写成隐藏，主屏照旧画出来',
  NPXS40145: '主屏自己的编辑入口，不是一张可隐藏的软件磁贴',
  NPXS40205: '主屏自己的编辑入口，不是一张可隐藏的软件磁贴',
  NPXS40140: '光碟占位条目：插不插碟由系统决定，面板改不动',
  NPXS40144: '光碟占位条目：插不插碟由系统决定，面板改不动',
  NPXS40148: '光碟占位条目：插不插碟由系统决定，面板改不动',
  NPXS40149: '光碟占位条目：插不插碟由系统决定，面板改不动',
  NPXS40150: '光碟占位条目：插不插碟由系统决定，面板改不动',
};

const mockSystemApps = [
  { tid: 'NPXS40037', name: 'PlayStation Plus', hidden: true },
  { tid: 'NPXS40047', name: 'PlayStation Store', hidden: false },
  { tid: 'NPXS40053', name: '探索', hidden: true },
  { tid: 'NPXS40054', name: 'Music Home', hidden: true },
  { tid: 'NPXS40056', name: '所有应用程序', hidden: false },
  { tid: 'NPXS40063', name: '欢迎', hidden: true },
  { tid: 'NPXS40071', name: '游戏内容保存库', hidden: false },
  { tid: 'NPXS40074', name: 'Remote Play', hidden: true },
  { tid: 'NPXS40075', name: '媒体相册', hidden: false },
  { tid: 'NPXS40099', name: '分享游玩', hidden: true },
  { tid: 'NPXS40106', name: 'Game Streaming', hidden: true },
  { tid: 'NPXS40139', name: '应用程序内容保存库', hidden: false },
  { tid: 'NPXS40140', name: 'Disc Player', hidden: true },
  { tid: 'NPXS40144', name: '不兼容的光碟', hidden: true },
  { tid: 'NPXS40145', name: '编辑', hidden: true },
  { tid: 'NPXS40148', name: '不可读的光碟', hidden: true },
  { tid: 'NPXS40149', name: 'PS5光碟', hidden: true },
  { tid: 'NPXS40150', name: 'PS4光碟', hidden: true },
  { tid: 'NPXS40205', name: '编辑', hidden: true },
];

app.get('/system_apps', (req, res) => {
  res.json({
    /* 与真机同一形状：不可控的那几条带 controllable:false + reason（一句人话），
     * 前端据此不给开关。v0.5.2.164 起每条多一个 pinned（面板侧的置顶开关）。 */
    apps: mockSystemApps.map((a) => ({
      ...a,
      pinned: !!gameState.appPins[a.tid],
      ...(MOCK_FIXED_APPS[a.tid]
        ? { controllable: false, reason: MOCK_FIXED_APPS[a.tid] }
        : { controllable: true }),
    })),
    count: mockSystemApps.length,
    accounts: 1,
    error: '',
  });
});

app.post('/system_app_visible', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  const hidden = req.body && req.body.hidden === true;
  const app = mockSystemApps.find((x) => x.tid === tid);
  if (!app) return res.status(400).json({ error: 'EINVAL', message: '主机上没有这个内置软件（可能已被卸载）' });
  /* [CN-FORK] v0.5.2.160 与后端同一取舍：固定条目**不回成功**（回成功就是谎报），
   * 也不改这份内存状态 —— 改了 mock 就会显示「已隐藏」，正是真机上最误导的那一幕。 */
  if (MOCK_FIXED_APPS[tid]) {
    return res.status(400).json({ error: 'EFIXED', message: MOCK_FIXED_APPS[tid] });
  }
  app.hidden = hidden;
  logs.push(`[PLDMGR] app_vis: ${tid} -> visible=${hidden ? 0 : 1}（1 张账号表）`);
  res.json({ ok: true, tid, hidden, message: '' });
});

/* [CN-FORK] v0.5.2.164 「个性化软件」的置顶开关。与 /system_app_visible 是**两条路**：
 * 那边写 app.db（这里用 app.hidden 模拟），这边只写本面板的 appPins。
 * 刻意**不**碰 app.hidden —— 真机上置顶也不动 app.db 那一列（见 include/game_prefs.h）。 */
app.post('/system_app_pin', (req, res) => {
  const tid = String((req.body && req.body.tid) || '');
  if (typeof (req.body && req.body.pinned) !== 'boolean') {
    return res.status(400).json({ error: 'EINVAL', message: '缺少 pinned（true=置顶 / false=取消置顶）' });
  }
  const pinned = req.body.pinned;
  const app = mockSystemApps.find((x) => x.tid === tid);
  if (!app) return res.status(400).json({ error: 'EBAD', message: '只能置顶主机内置软件' });
  if (pinned) gameState.appPins[tid] = true;
  else delete gameState.appPins[tid];
  logs.push(`[PLDMGR] app_pin: ${tid} -> pinned=${pinned ? 1 : 0}`);
  res.json({ ok: true, tid, pinned, message: '' });
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

/* 回归脚本用的重置端点：把可变状态全部清零，保证每次跑都是干净开局 */
app.post('/reset', (req, res) => {
  gameState.deleted.clear();
  gameState.renamed = {};
  gameState.hostNames = {};   /* v0.5.2.137 主机索引那一份也要清，否则上一节的漂移状态会串场 */
  gameState.icon = {};
  gameState.bg = {};
  gameState.iconBytes = {};
  gameState.bgBytes = {};
  gameState.bgDds = {};
  /* [CN-FORK] v0.5.2.163/164 背景音乐 / 收藏置顶 / 个性化软件置顶也一并清零。
   * 不清的话：上一节设过的音乐会让下一节「还没设过时显示默认」假红；
   * 收藏/置顶的残留会让「收藏的排到最前」这条断言在第二次跑时假绿（本来就排前面）。 */
  gameState.music = {};
  gameState.prefs = {};
  gameState.appPins = {};
  /* [CN-FORK] v0.5.2.166 主机那份 .at9、面板手工顺序、主机固定列表也一起清 ——
   * 不清的话：上一节设过的音乐会串到下一节的「主机侧到底写没写」断言（假绿），
   * 上一节的拖拽顺序会让「第一次进页面按默认顺序」假红。 */
  gameState.musicHost = {};
  gameState.musicHostFail = [];
  gameState.order = [];
  /* 夹具从 tileGroupFixture() 取，别再抄一份（抄一份就会漂移，见那个函数的注释）。 */
  gameState.tileGroup = tileGroupFixture();
  gameState.tileGroupBroken = false;
  gameState.saves = {};
  gameState.media = {};
  gameState.auto = {};
  /* [CN-FORK] v0.5.2.142 快照原来漏在 reset 之外 —— 于是同一个 mock 进程跑第二遍 game112，
   * 5b 那节「还没做过快照时列表是空的」「立即备份生成 2 份」全红，而 5 节的
   * `dlg(page).locator('li').count()` 还会多算上残留的快照行（2 条存档变成 3 条）。
   * 快照是 v0.5.2.140 加的，加的时候没同步改这里。 */
  gameState.snaps = {};
  /* [CN-FORK] v0.5.2.142 自动存档的运行期状态与「远端已有的文件」也要清 ——
   * 上一节的推送/拉取留下的远端痕迹会串场，让下一节的「第一次用远端还是空的」假绿。 */
  gameState.autoState = {};
  gameState.runningTid = '';
  gameState.remoteFiles.clear();
  /* [CN-FORK] v0.5.2.154 garlic 导入的流水账也清 —— 不清的话下一节读到的还是
   * 上一节导入过的那笔，断言「这次导入传了正确的 name」就变成假绿。 */
  gameState.garlicImports = [];
  /* [CN-FORK] v0.5.2.165 强制「主机磁贴库写失败」的开关也清 —— 留着会让后面
   * 任何一次改名都弹红字，那些正向断言（「已写入主机磁贴数据库」）就假红。 */
  gameState.hostRenameFail.clear();
  /* [CN-FORK] v0.5.2.142 把 /loadpayload: 起过的程序桩（10101）一并关掉。
   * 不关的话，第二遍 game112 的 1a 节「前置：10101 必须真的没人监听」必红 ——
   * 而那条前置正是「先探后开」整条链路的立足点。之前只能靠重启 mock 进程绕开。 */
  for (const srv of progStubs.values()) { try { srv.close() } catch { /* 已经关了 */ } }
  progStubs.clear();
  gameState.jobs = {};
  gameState.nextJob = 1;
  /* [CN-FORK] v0.5.2.144 全局存档配置（设置页「存档配置」写入 ARCHIVE_GLOBAL_CONFIG）。
   * /set_config 会把它存进 mockConfig；不清的话上一节设的全局远程会串到下一节的
   * 单游戏弹窗（显示「来自全局设置」而不是「本游戏已配置」）→ 假红。 */
  delete mockConfig.ARCHIVE_GLOBAL_CONFIG;
  listSnap = null;
  listSnapAt = 0;
  /* 预热状态也要清：否则 reset 之后 folder 游戏已经是「预热过」的，
   * 测试再想看「冷启动没有体积标签 → 几秒后补齐」这条时序就看不到了。 */
  warmSizes.clear();
  if (warmTimer) { clearTimeout(warmTimer); warmTimer = null; }
  res.json({ ok: true });
});

app.listen(PORT, '127.0.0.1', () => {
  console.log(`\x1b[36m%s\x1b[0m`, `--- Payload Manager Mock Backend ---`);
  console.log(`Running at http://localhost:${PORT}`);
  console.log(`Proxy your Vite requests to this port to test the frontend locally.`);
});
