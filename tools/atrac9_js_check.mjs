#!/usr/bin/env node
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Offline check for the browser ATRAC9 encoder port.
//
// Ported from blackbearreloaded/ps5-at9-converter (GPL-3.0-or-later).
//
// It generates test PCM, encodes it with the JS modules, then uses the Python
// reference project (ps5-at9-converter) as the oracle:
//   * the Python decoder decodes the JS .at9 back to PCM and we measure the SNR;
//   * the Python encoder encodes the same PCM and we compare container headers,
//     data-chunk bytes, sizes and SNR.
//
// The Python helper is written to the OS temp directory at run time, so this
// script adds no extra files to the repository.
//
// Run:  node tools/atrac9_js_check.mjs
// Exit: 0 when every case passes, 1 otherwise.

import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import { tmpdir } from 'node:os';
import { mkdirSync, writeFileSync, readFileSync, existsSync } from 'node:fs';
import { spawn } from 'node:child_process';

import { encodeAt9 } from '../frontend/src/utils/atrac9/index.js';
import { toStereo, resample } from '../frontend/src/utils/atrac9/dsp.js';
import { isAt9 } from '../frontend/src/utils/atrac9/container.js';

const HERE = dirname(fileURLToPath(import.meta.url));
const REF_DIR = resolve(HERE, '..', '..', '_ref', 'ps5-at9-converter');
const PYTHON =
  process.env.AT9_PYTHON || 'C:/Users/wugua/.workbuddy-ai/binaries/python/envs/at9/Scripts/python.exe';
const WORK = join(tmpdir(), 'at9-js-check');
const SNR_PASS_DB = 40.0;
let PY_PATH = '';

const PY_HELPER = String.raw`
# Temporary helper written by tools/atrac9_js_check.mjs.
import sys, json, os, struct
import numpy as np

REF = os.environ["AT9_REF_DIR"]
sys.path.insert(0, REF)
from ps5at9 import atrac9_encoder as E, at9file, atrac9  # noqa: E402

def read_planar(path, channels, samples):
    a = np.fromfile(path, dtype="<f8")
    return np.ascontiguousarray(a.reshape(channels, samples))

def header_json(raw):
    info = at9file.read_at9(raw)
    chunks = []
    pos = 12
    while pos + 8 <= len(raw):
        cid = raw[pos:pos+4].decode("latin-1")
        size = struct.unpack_from("<I", raw, pos+4)[0]
        chunks.append([cid, size])
        pos += 8 + size + (size & 1)
    return {
        "chunks": chunks,
        "channels": info.channels,
        "sampleRate": info.sample_rate,
        "byteRate": info.byte_rate,
        "blockAlign": info.block_align,
        "samplesPerBlock": info.samples_per_block,
        "version": info.version,
        "sampleCount": info.sample_count,
        "overlapDelay": info.overlap_delay,
        "encoderDelay": info.encoder_delay,
        "loops": info.loops,
        "config": info.config.data.hex(),
        "dataBytes": len(info.data),
        "fileBytes": len(raw),
    }

cmd = sys.argv[1]
if cmd == "encode":
    inp = sys.argv[2]
    ch = int(sys.argv[3])
    n = int(sys.argv[4])
    br = int(sys.argv[5])
    out = sys.argv[6]
    pcm = read_planar(inp, ch, n) * 32768.0
    enc = E.Encoder(ch, br)
    data = enc.encode(pcm, workers=1)
    blob = at9file.build_at9(enc.config, ch, n, data)
    open(out, "wb").write(blob)
    print(json.dumps({"fileBytes": len(blob), "dataBytes": len(data), "header": header_json(blob)}))
elif cmd == "roundtrip":
    at9_path = sys.argv[2]
    ref_path = sys.argv[3]
    ch = int(sys.argv[4])
    n = int(sys.argv[5])
    raw = open(at9_path, "rb").read()
    info = at9file.read_at9(raw)
    dec = atrac9.Decoder(info.config).decode(info.data)
    if dec.shape[1] < 256 + n:
        print(json.dumps({"ok": False, "error": "decoded stream too short"}))
        sys.exit(0)
    out = dec[:, 256:256+n]
    ref = read_planar(ref_path, ch, n) * 32768.0
    err = ref - out
    denom = float(np.sum(err ** 2))
    snr = 10.0 * np.log10(float(np.sum(ref ** 2)) / denom) if denom > 0 else float("inf")
    print(json.dumps({
        "ok": True,
        "snr": snr if np.isfinite(snr) else None,
        "peakErr": float(np.max(np.abs(err))),
        "decodedMax": float(np.max(np.abs(out))),
        "sampleCount": info.sample_count,
    }))
elif cmd == "header":
    print(json.dumps(header_json(open(sys.argv[2], "rb").read())))
`;

/**
 * Run the Python helper and resolve with its stdout. Uses the async spawn
 * because some sandboxed Windows environments reject the synchronous variants.
 */
function py(args) {
  return new Promise((resolvePromise, reject) => {
    const child = spawn(PYTHON, [PY_PATH, ...args], {
      env: { ...process.env, AT9_REF_DIR: REF_DIR },
    });
    let out = '';
    let err = '';
    child.stdout.on('data', (d) => (out += d));
    child.stderr.on('data', (d) => (err += d));
    child.on('error', reject);
    child.on('close', (code) => {
      if (code === 0) resolvePromise(out.trim());
      else reject(new Error('python exited ' + code + ': ' + err.trim()));
    });
  });
}

function writePlanar(path, channels) {
  const n = channels[0].length;
  const buf = Buffer.alloc(channels.length * n * 8);
  let off = 0;
  for (const c of channels) {
    for (let i = 0; i < n; i++) {
      buf.writeDoubleLE(c[i], off);
      off += 8;
    }
  }
  writeFileSync(path, buf);
}

// -- test signals --------------------------------------------------------------

function sine(rate, n, freq, amp, phase) {
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) out[i] = amp * Math.sin((2 * Math.PI * freq * i) / rate + phase);
  return out;
}

function noise(n, amp, seed) {
  const out = new Float64Array(n);
  let s = seed >>> 0;
  for (let i = 0; i < n; i++) {
    s = (Math.imul(s, 1103515245) + 12345) & 0x7fffffff;
    out[i] = ((s / 0x7fffffff) * 2 - 1) * amp;
  }
  return out;
}

const CASES = [
  {
    name: 'sine1k-stereo-48k',
    rate: 48000,
    channels: 2,
    snrGate: 40,
    gen: () => [sine(48000, 48000, 1000, 0.5, 0), sine(48000, 48000, 1000, 0.5, Math.PI / 2)],
  },
  {
    // Full-band white noise is not a 40 dB signal for ATRAC9 at 192 kb/s; the
    // Python reference reaches the same ~5 dB, so this case has its own gate.
    name: 'noise-stereo-48k',
    rate: 48000,
    channels: 2,
    snrGate: 3,
    gen: () => [noise(48000, 0.2, 1), noise(48000, 0.2, 99991)],
  },
  {
    name: 'silence-stereo-48k',
    rate: 48000,
    channels: 2,
    snrGate: null,
    gen: () => [new Float64Array(48000), new Float64Array(48000)],
  },
  {
    name: 'sine1k-mono-48k',
    rate: 48000,
    channels: 1,
    snrGate: 40,
    gen: () => [sine(48000, 48000, 1000, 0.5, 0)],
  },
  {
    name: 'sine1k-stereo-44k1',
    rate: 44100,
    channels: 2,
    snrGate: 40,
    gen: () => [
      sine(44100, 44100, 1000, 0.5, 0),
      sine(44100, 44100, 1000, 0.5, Math.PI / 2),
    ],
  },
];

// -- run -----------------------------------------------------------------------

const line = (...parts) => console.log(...parts);

async function run() {
  if (!existsSync(REF_DIR)) {
    line('RESULT: FAIL reference project not found at ' + REF_DIR);
    process.exit(1);
  }
  mkdirSync(WORK, { recursive: true });
  PY_PATH = join(WORK, 'at9_py_tool.py');
  writeFileSync(PY_PATH, PY_HELPER);

  line('ATRAC9 JS encoder check');
  line('  python : ' + PYTHON);
  line('  ref    : ' + REF_DIR);
  line('  work   : ' + WORK);
  line('');

  let failures = 0;
  let minSnr = Infinity;

  for (const c of CASES) {
    try {
      const channelData = c.gen();
      const pcm48 = resample(toStereo(channelData), c.rate, 48000);
      const outN = pcm48[0].length;
      const pcmPath = join(WORK, c.name + '.f64');
      writePlanar(pcmPath, pcm48);

      const jsPath = join(WORK, c.name + '.js.at9');
      const pyPathOut = join(WORK, c.name + '.py.at9');

      const t0 = Date.now();
      const result = encodeAt9({
        sampleRate: c.rate,
        channels: c.channels,
        channelData,
        bitrateKbps: 192,
        maxDurationSec: outN / 48000,
        normalize: false,
        fadeInSec: 0,
        fadeOutSec: 0,
        truePeakCeilingDb: 0,
        searchMode: 'full',
      });
      const jsMs = Date.now() - t0;
      writeFileSync(jsPath, Buffer.from(result.bytes));

      const pyRes = JSON.parse(await py(['encode', pcmPath, '2', String(outN), '192', pyPathOut]));
      const rt = JSON.parse(await py(['roundtrip', jsPath, pcmPath, '2', String(outN)]));
      const rtPy = JSON.parse(await py(['roundtrip', pyPathOut, pcmPath, '2', String(outN)]));

      const jsBytes = readFileSync(jsPath);
      const pyBytes = readFileSync(pyPathOut);
      let headerDiff = -1;
      for (let i = 0; i < 168; i++) if (jsBytes[i] !== pyBytes[i]) { headerDiff = i; break; }
      const nData = Math.min(jsBytes.length, pyBytes.length) - 168;
      let dataDiff = -1;
      for (let i = 0; i < nData; i++) if (jsBytes[168 + i] !== pyBytes[168 + i]) { dataDiff = i; break; }

      const snr = rt.ok && rt.snr !== null ? rt.snr : null;
      if (c.snrGate === null) {
        // Silence: no SNR, only the residual level matters.
      } else if (snr !== null && c.snrGate >= 40) {
        minSnr = Math.min(minSnr, snr);
      }
      const qualityOk =
        c.snrGate === null
          ? rt.ok && rt.decodedMax < 32.8
          : snr !== null && snr >= c.snrGate;
      const pass =
        rt.ok && qualityOk && headerDiff < 0 && dataDiff < 0 && jsBytes.length === pyBytes.length;
      if (!pass) failures++;

      line('CASE ' + c.name + (pass ? '  PASS' : '  FAIL'));
      line(
        '  js: ' + jsBytes.length + ' B (' + jsMs + ' ms)  py: ' + pyBytes.length + ' B' +
          '  header ' + (headerDiff < 0 ? 'identical' : 'DIFF@' + headerDiff) +
          '  data ' + (dataDiff < 0 ? 'identical' : 'DIFF@' + dataDiff),
      );
      line(
        '  decode ok=' + rt.ok + '  snr(js)=' + (snr === null ? 'n/a (silence)' : snr.toFixed(2) + ' dB') +
          '  snr(py)=' + (rtPy.snr === null || !rtPy.ok ? 'n/a' : rtPy.snr.toFixed(2) + ' dB') +
          '  peakErr=' + (rt.ok ? rt.peakErr.toFixed(3) : 'n/a') +
          '  isAt9=' + isAt9(jsBytes),
      );
    } catch (e) {
      failures++;
      line('CASE ' + c.name + '  FAIL');
      line('  error: ' + e.message);
    }
  }

  // 15 s stereo timing: full search vs the coarse fallback. Both outputs are
  // decoded with the Python decoder to prove the downgrade stays a legal stream.
  line('');
  line('Timing: 15 s stereo 48 kHz, 192 kb/s');
  const n15 = 15 * 48000;
  const ch15 = [sine(48000, n15, 440, 0.4, 0), sine(48000, n15, 660, 0.4, 1.0)];
  const pcm15Path = join(WORK, 'timing15.f64');
  writePlanar(pcm15Path, ch15);
  for (const mode of ['full', 'fast']) {
    const outPath = join(WORK, 'timing15.' + mode + '.at9');
    const t0 = Date.now();
    const r = encodeAt9({
      sampleRate: 48000,
      channels: 2,
      channelData: ch15,
      bitrateKbps: 192,
      maxDurationSec: 15,
      normalize: false,
      fadeInSec: 0,
      fadeOutSec: 0,
      truePeakCeilingDb: 0,
      searchMode: mode,
    });
    const ms = Date.now() - t0;
    writeFileSync(outPath, Buffer.from(r.bytes));
    let snrText = 'n/a';
    try {
      const rt = JSON.parse(await py(['roundtrip', outPath, pcm15Path, '2', String(n15)]));
      snrText = rt.ok && rt.snr !== null ? rt.snr.toFixed(2) + ' dB' : 'decode failed';
      if (!rt.ok || !(rt.snr === null || rt.snr > 0)) failures++;
    } catch (e) {
      snrText = 'error: ' + e.message;
      failures++;
    }
    line('  searchMode=' + mode + ': ' + ms + ' ms, ' + r.bytes.length + ' bytes, decode snr=' + snrText);
  }

  line('');
  const ok = failures === 0 && minSnr >= SNR_PASS_DB;
  if (ok) {
    line('RESULT: PASS snr=' + minSnr.toFixed(2) + ' dB (>=40 dB 视为可用)');
  } else {
    line(
      'RESULT: FAIL ' + failures + ' case(s) failed, snr=' +
        (Number.isFinite(minSnr) ? minSnr.toFixed(2) + ' dB' : 'n/a'),
    );
  }
  process.exit(ok ? 0 : 1);
}

run();
