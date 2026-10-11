// SPDX-License-Identifier: GPL-3.0-or-later
//
// Web Worker entry point for the browser ATRAC9 encoder.
//
// Ported from blackbearreloaded/ps5-at9-converter (GPL-3.0-or-later); this file
// is new glue that runs the ported encoder off the main thread.
//
// Message in:  { id, sampleRate, channels, channelData, bitrateKbps, maxDurationSec,
//                searchMode }
// Progress:    { id, progress }   (progress is 0..1)
// Message out: { id, ok: true, bytes, info }  or  { id, ok: false, error }
//
// [CN-FORK] v0.5.2.166 `searchMode` 是给调用方留的：面板背景音乐走 'fast'
// （磁贴聚焦音听不出那点差别，但编码时间少一半以上），不传仍是编码器的默认 'full'。
//
// No DOM APIs are imported or used here: only the Worker globals postMessage /
// onmessage and the pure-JS encoder modules.

import { encodeAt9, parseAt9Info } from './index.js';

self.onmessage = (event) => {
  const msg = event.data || {};
  const id = msg.id;
  const report = (payload) => self.postMessage(payload);
  try {
    const result = encodeAt9({
      sampleRate: msg.sampleRate,
      channels: msg.channels,
      channelData: msg.channelData,
      bitrateKbps: msg.bitrateKbps,
      maxDurationSec: msg.maxDurationSec,
      searchMode: msg.searchMode,
      onProgress: (done, total) => report({ id, progress: total ? done / total : 0 }),
    });
    const bytes = result.bytes;
    report({
      id,
      ok: true,
      bytes,
      info: {
        ...parseAt9Info(bytes),
        durationSec: result.durationSec,
        lufs: result.lufs,
        truePeakDb: result.truePeakDb,
        bitrateKbps: result.bitrateKbps,
      },
    }, [bytes.buffer]);
  } catch (err) {
    report({ id, ok: false, error: String((err && err.message) || err) });
  }
};
