// SPDX-License-Identifier: GPL-3.0-or-later
//
// ATRAC9 RIFF/WAVE container reader and writer for the browser.
//
// Ported from blackbearreloaded/ps5-at9-converter (GPL-3.0-or-later),
// ps5at9/at9file.py. The layout matches Sony's at9tool output: fmt (52 bytes),
// fact (12), smpl (60), data (whole superframes), in that order.
//
// Zero dependencies: no npm packages, no Node APIs, no DOM.

import { ATRAC9_GUID } from './tables.js';

export const ENCODER_DELAY = 256;
const SPEAKER_MASKS = { 1: 0x4, 2: 0x3 };

const WAVE_FORMAT_EXTENSIBLE = 0xfffe;

/** Little-endian DataView over a byte array (accepts Uint8Array/Array). */
function view(bytes) {
  const u8 = bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes);
  return new DataView(u8.buffer, u8.byteOffset, u8.byteLength);
}

function ascii(u8, pos, len) {
  let s = '';
  for (let i = 0; i < len; i++) s += String.fromCharCode(u8[pos + i]);
  return s;
}

function bytesEqual(u8, pos, ref) {
  for (let i = 0; i < ref.length; i++) if (u8[pos + i] !== ref[i]) return false;
  return true;
}

/**
 * Return the complete .at9 file bytes.
 *
 * `config` carries the stream parameters: sampleRate, superframeBytes,
 * superframeSamples and the 4-byte `data` configuration word. `loop = true`
 * writes the whole-track loop at9tool's -wholeloop produces: start = encoder
 * delay, end = sample_count + encoder delay - 1.
 */
export function buildAt9(config, channels, sampleCount, data, loop = true, encoderDelay = ENCODER_DELAY) {
  const payload = data instanceof Uint8Array ? data : Uint8Array.from(data);
  if (payload.length % config.superframeBytes) throw new Error('data must contain whole superframes');
  const byteRate = Math.floor((config.superframeBytes * config.sampleRate) / config.superframeSamples);

  // fmt: 24 bytes of fields + ATRAC9 GUID + version + config word + reserved.
  const fmt = new Uint8Array(52);
  const fv = new DataView(fmt.buffer);
  fv.setUint16(0, WAVE_FORMAT_EXTENSIBLE, true);
  fv.setUint16(2, channels, true);
  fv.setUint32(4, config.sampleRate, true);
  fv.setUint32(8, byteRate, true);
  fv.setUint16(12, config.superframeBytes, true);
  fv.setUint16(14, 0, true); // wBitsPerSample
  fv.setUint16(16, 34, true); // cbSize
  fv.setUint16(18, config.superframeSamples, true); // wSamplesPerBlock
  fv.setUint32(20, SPEAKER_MASKS[channels] || 0, true); // dwChannelMask
  fmt.set(ATRAC9_GUID, 24);
  fv.setUint32(40, 1, true); // version
  fmt.set(config.data, 44);
  fv.setUint32(48, 0, true); // reserved

  const fact = new Uint8Array(12);
  const ctv = new DataView(fact.buffer);
  ctv.setUint32(0, sampleCount, true);
  ctv.setUint32(4, encoderDelay, true);
  ctv.setUint32(8, encoderDelay, true);

  const chunks = [
    ['fmt ', fmt],
    ['fact', fact],
  ];
  if (loop) {
    const smpl = new Uint8Array(60);
    const sv = new DataView(smpl.buffer);
    const period = Math.round(1e9 / config.sampleRate);
    const head = [0, 0, period, 60, 0, 0, 0, 1, 24];
    for (let i = 0; i < head.length; i++) sv.setUint32(i * 4, head[i], true);
    const loopRec = [0, 0, encoderDelay, sampleCount + encoderDelay - 1, 0, 0];
    for (let i = 0; i < loopRec.length; i++) sv.setUint32(36 + i * 4, loopRec[i], true);
    chunks.push(['smpl', smpl]);
  }
  chunks.push(['data', payload]);

  let bodyLen = 4; // "WAVE"
  for (const [, payloadChunk] of chunks) bodyLen += 8 + payloadChunk.length + (payloadChunk.length & 1);
  const out = new Uint8Array(8 + bodyLen);
  const ov = new DataView(out.buffer);
  out.set([0x52, 0x49, 0x46, 0x46], 0); // RIFF
  ov.setUint32(4, bodyLen, true);
  out.set([0x57, 0x41, 0x56, 0x45], 8); // WAVE
  let pos = 12;
  for (const [id, payloadChunk] of chunks) {
    for (let i = 0; i < 4; i++) out[pos + i] = id.charCodeAt(i);
    ov.setUint32(pos + 4, payloadChunk.length, true);
    out.set(payloadChunk, pos + 8);
    pos += 8 + payloadChunk.length + (payloadChunk.length & 1);
  }
  return out;
}

/** Parse a .at9 file into its chunks and stream parameters. */
export function readAt9(bytes) {
  const u8 = bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes);
  if (u8.length < 12 || ascii(u8, 0, 4) !== 'RIFF' || ascii(u8, 8, 4) !== 'WAVE') {
    throw new Error('not a RIFF/WAVE file');
  }
  const dv = view(u8);
  const info = {
    channels: 0,
    sampleRate: 0,
    byteRate: 0,
    blockAlign: 0,
    samplesPerBlock: 0,
    version: 0,
    sampleCount: 0,
    overlapDelay: 0,
    encoderDelay: 0,
    loops: [],
    config: null,
    data: new Uint8Array(0),
    haveFmt: false,
  };
  let pos = 12;
  while (pos + 8 <= u8.length) {
    const id = ascii(u8, pos, 4);
    const size = dv.getUint32(pos + 4, true);
    const body = pos + 8;
    if (id === 'fmt ') {
      const tag = dv.getUint16(body, true);
      info.channels = dv.getUint16(body + 2, true);
      info.sampleRate = dv.getUint32(body + 4, true);
      info.byteRate = dv.getUint32(body + 8, true);
      info.blockAlign = dv.getUint16(body + 12, true);
      if (tag !== WAVE_FORMAT_EXTENSIBLE || size < 52 || !bytesEqual(u8, body + 24, ATRAC9_GUID)) {
        throw new Error('fmt chunk is not ATRAC9 WAVE_FORMAT_EXTENSIBLE');
      }
      info.samplesPerBlock = dv.getUint16(body + 18, true);
      info.version = dv.getUint32(body + 40, true);
      info.config = u8.slice(body + 44, body + 48);
      info.haveFmt = true;
    } else if (id === 'fact') {
      info.sampleCount = dv.getUint32(body, true);
      if (size >= 12) {
        info.overlapDelay = dv.getUint32(body + 4, true);
        info.encoderDelay = dv.getUint32(body + 8, true);
      }
    } else if (id === 'smpl' && size >= 36) {
      const count = dv.getUint32(body + 28, true);
      for (let i = 0; i < count; i++) {
        const off = 36 + 24 * i;
        if (off + 24 <= size) {
          info.loops.push([
            dv.getUint32(body + off + 4, true),
            dv.getUint32(body + off + 8, true),
            dv.getUint32(body + off + 12, true),
            dv.getUint32(body + off + 20, true),
          ]);
        }
      }
    } else if (id === 'data') {
      info.data = u8.slice(body, body + size);
    }
    pos += 8 + size + (size & 1);
  }
  if (!info.haveFmt) throw new Error('missing fmt chunk');
  return info;
}

/** True when `bytes` is a RIFF/WAVE ATRAC9 file (WAVE_FORMAT_EXTENSIBLE + GUID). */
export function isAt9(bytes) {
  try {
    readAt9(bytes);
    return true;
  } catch (e) {
    return false;
  }
}

/** Compact summary for UI hints: sample rate, channels, duration, byte size. */
export function parseAt9Info(bytes) {
  const u8 = bytes instanceof Uint8Array ? bytes : Uint8Array.from(bytes);
  const info = readAt9(u8);
  const config = info.config;
  let superframeIndex = 0;
  let frameBytes = 0;
  if (config && config.length === 4) {
    const word = ((config[0] << 24) | (config[1] << 16) | (config[2] << 8) | config[3]) >>> 0;
    superframeIndex = (word >> 3) & 3;
    frameBytes = ((word >> 5) & 0x7ff) + 1;
  }
  const superframeBytes = frameBytes << superframeIndex;
  const samplesPerSuperframe = info.samplesPerBlock || 1024;
  const frames = superframeBytes ? Math.floor(info.data.length / superframeBytes) : 0;
  return {
    sampleRate: info.sampleRate,
    channels: info.channels,
    sampleCount: info.sampleCount,
    durationSec: info.sampleRate ? (info.sampleCount || frames * samplesPerSuperframe) / info.sampleRate : 0,
    byteCount: u8.length,
    dataBytes: info.data.length,
    superframeBytes,
    loop: info.loops.length > 0,
    encoderDelay: info.encoderDelay,
  };
}
