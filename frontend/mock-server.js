import express from 'express';
import cors from 'cors';
import { fileURLToPath } from 'url';

const app = express();
app.use(cors());
app.use(express.json());

// Log all requests to terminal
app.use((req, res, next) => {
  console.log(`[Mock] ${req.method} ${req.url}`);
  next();
});

const PORT = 8081;

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
let readHardwareEnabled = true; // [CN-FORK] 模拟 READ_HARDWARE_INFO 开关持久化

let mockSources = [
  { id: 'default', name: 'Official Repository', url: 'https://itsplk.github.io/ps5-payloads-mirror/payloads.json', removable: false },
  { id: 'source_community', name: 'Community Payloads', url: 'https://example.com/community-payloads.json', removable: true }
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
  res.send('0.5.1.56'); // [CN-FORK] 与 pldmgr.h MENU_VERSION 保持一致
});

// [CN-FORK] 当前游戏图标 mock：返回构建产物 icon.png（实机由 payload 读游戏 sce_sys/icon0.png）
app.get('/game_icon', (req, res) => {
  res.setHeader('Cache-Control', 'public, max-age=3600');
  res.sendFile(fileURLToPath(new URL('./dist/icon.png', import.meta.url)), (err) => {
    if (err) res.status(404).json({ error: 'no game icon' });
  });
});

// [CN-FORK] PKG Manager 内置集成 mock：状态/启动/上传（实机由 pkgmanager.c 探活 8844 + elfldr 拉起）
let pkgRunning = false;
let pkgInstalled = '-';
app.get('/pkgmanager_status', (req, res) => {
  res.json({
    running: pkgRunning,
    port: 8844,
    embedded_version: '1.4.0-CN',
    installed_version: pkgInstalled,
    file: pkgInstalled !== '-',
  });
});
app.get('/pkgmanager_launch', (req, res) => {
  pkgRunning = true;
  if (pkgInstalled === '-') pkgInstalled = '1.4.0-CN';
  res.json({ ok: true, message: 'OK' });
});
// [CN-FORK] 关闭 PKG Manager mock（实机由 pkgmanager.c 按进程名 SIGKILL）
app.get('/pkgmanager_stop', (req, res) => {
  if (!pkgRunning) {
    return res.json({ ok: true, message: 'PKG Manager was not running' });
  }
  pkgRunning = false;
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
const quicklinks = [
  { name: 'PKG Manager', url: 'http://192.168.1.214:8844' },
  { name: 'NAS 面板', url: 'http://192.168.1.10:5000' },
];
app.get('/quicklinks', (req, res) => {
  res.json({ entries: quicklinks.map((e, i) => ({ ...e, alive: i === 0 })) });
});
app.get('/quicklinks_add', (req, res) => {
  const { name, url } = req.query;
  if (!name || !/^https?:\/\//.test(url || '')) {
    return res.status(400).json({ ok: false, message: '参数不合法' });
  }
  if (quicklinks.some((e) => e.url === url)) {
    return res.status(400).json({ ok: false, message: '该 URL 已存在' });
  }
  quicklinks.push({ name, url });
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

app.get('/list_payloads', (req, res) => {
  res.json({
    payloads: [
      "/data/nayeplmtools/goldhen_v2.4b17.elf",
      "/data/nayeplmtools/etaHEN_1.8.elf",
      "/data/nayeplmtools/kstuff.elf",
      "/mnt/usb0/nayeplmtools/linux_loader.elf"
    ],
    meta: {
      // payloads with no entry here = local/direct upload (no badge shown)
      "kstuff.elf": {
        source_name: "Community Payloads",
        install_source: "repository",
        install_source_detail: "https://example.com/community-payloads.json"
      }
    }
  });
});

app.get('/autoload_status', (req, res) => {
  res.json(autoloadStatus);
});

app.get('/get_config', (req, res) => {
  res.json({
    AUTOLOAD_ENABLED: true,
    AUTOLOAD_LIST: "goldhen_v2.4b17.elf,etaHEN_1.8.elf",
    LAST_REPOSITORY_UPDATE: lastRepositoryUpdate,
    AUTO_INSTALL_APP: true,
    MULTI_SOURCES_ENABLED: true,
    READ_HARDWARE_INFO: readHardwareEnabled
  });
});

// [CN-FORK] 硬件信息端点，模拟 stats.c 的 JSON 结构（内存/显存/存储单位 MB）
// 关闭 READ_HARDWARE_INFO 时返回 {enabled:false}，与后端行为一致
app.get('/system_stats', (req, res) => {
  if (!readHardwareEnabled) return res.json({ enabled: false });
  const jitter = (base, range) => Math.round(base + Math.sin(Date.now() / 2000) * range);
  res.json({
    cpu: { usage: jitter(24, 8), temp: jitter(52, 4) },
    gpu: { temp: jitter(48, 4) },
    ssd: { temp: jitter(44, 3) },
    fan: { duty: jitter(42, 8), max: 100 },
    ram: { total: 16384, used: jitter(8400, 600) },
    vram: { total: 12288, used: jitter(6250, 500) },
    fps: 58 + Math.round(Math.sin(Date.now() / 1500) * 4),
    game: { name: '血源诅咒 The Old Hunters', tid: 'PPSA00991' },
    uptime: `2h 34m ${Math.floor(Date.now() / 1000) % 60}s`,
    model: 'CFI-1209A 01', // [CN-FORK] 实机官方主机信息页同源（inst0.dat offset 0x14）
    serial: 'F32B018RF11266783', // [CN-FORK] inst0.dat offset 0x40，实机验证值
    firmware: '12.20', // [CN-FORK] api_version 0x12200005：(0x12 BCD=12).(0x20 BCD=20)，低 16 位不上界面
    storage: [
      { label: 'Internal storage', total: 825000, used: 420000, path: '/user' },
      { label: 'NVME SSD', total: 1000000, used: 128500, path: '/mnt/ext0' }
    ]
  });
});

app.get('/repository_payloads', (req, res) => {
  // Return source-grouped format (multi-source mode always on in mock)
  res.json({
    sources: [
      {
        id: 'default',
        name: 'Official Repository',
        last_update: lastRepositoryUpdate,
        error: false,
        payloads: remoteRepository.map(p => ({ ...p, source_id: 'default', source_name: 'Official Repository' }))
      },
      {
        id: 'source_community',
        name: 'Community Payloads',
        last_update: lastRepositoryUpdate,
        error: false,
        payloads: communityRepository.map(p => ({ ...p, source_id: 'source_community', source_name: 'Community Payloads' }))
      }
    ]
  });
});

app.get('/repository_refresh', (req, res) => {
  lastRepositoryUpdate = Math.floor(Date.now() / 1000);
  logs.push(`[PLDMGR] Repository manually refreshed`);
  res.json({
    sources: [
      {
        id: 'default',
        name: 'Official Repository',
        last_update: lastRepositoryUpdate,
        error: false,
        payloads: remoteRepository.map(p => ({ ...p, source_id: 'default', source_name: 'Official Repository' }))
      },
      {
        id: 'source_community',
        name: 'Community Payloads',
        last_update: lastRepositoryUpdate,
        error: false,
        payloads: communityRepository.map(p => ({ ...p, source_id: 'source_community', source_name: 'Community Payloads' }))
      }
    ]
  });
});

app.get('/sources_list', (req, res) => {
  res.json({ sources: mockSources });
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
  if (req.body && typeof req.body.READ_HARDWARE_INFO === 'boolean') {
    readHardwareEnabled = req.body.READ_HARDWARE_INFO; // [CN-FORK]
  }
  res.send('OK');
});

app.get(/^\/loadpayload:(.*)/, (req, res) => {
  const path = req.params[0];
  logs.push(`[PLDMGR] Executing payload: ${path}`);
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
