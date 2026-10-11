// SPDX-License-Identifier: GPL-3.0-or-later
//
// Public API for the browser ATRAC9 encoder that produces PS5 snd0.at9 files.
//
// Ported from blackbearreloaded/ps5-at9-converter (GPL-3.0-or-later); the
// pipeline mirrors ps5at9/cli.py run(): map to stereo, resample to 48 kHz,
// take the excerpt, normalise loudness, limit the true peak, apply fades,
// encode and wrap in the RIFF/WAVE container.
//
// Zero dependencies: no npm packages, no Node APIs, no DOM.

import {
  toStereo,
  resample,
  integratedLoudness,
  limitTruePeak,
  truePeakDb,
  applyFades,
} from './dsp.js';
import { Encoder, maxSamplesFor, STEREO_BITRATES } from './encoder.js';
import { buildAt9, isAt9, parseAt9Info, readAt9, ENCODER_DELAY } from './container.js';

export { isAt9, parseAt9Info, readAt9, ENCODER_DELAY };

/** Sony's 2 MiB ceiling for sce_sys/snd0.at9. */
export const PS5_SIZE_LIMIT = 2 * 1024 * 1024;

const RATE = 48000;
const DEFAULT_LOUDNESS = -28.0; // matches loudnessSnd0
const DEFAULT_TRUE_PEAK = -2.0;

/**
 * Encode PCM into a complete .at9 file.
 *
 * @param {object} opts
 * @param {number} opts.sampleRate        sample rate of channelData
 * @param {number} opts.channels          number of channels in channelData
 * @param {Array<Float32Array|Float64Array>} opts.channelData  PCM in [-1, 1]
 * @param {number} [opts.bitrateKbps=192] one of 72/96/120/144/168/192
 * @param {number|'max'} [opts.maxDurationSec=15]  excerpt length; 'max' picks
 *        the longest excerpt that fits the 2 MiB limit
 * @param {number} [opts.targetLufs=-28]  integrated loudness target
 * @param {number} [opts.truePeakCeilingDb=-2]  true-peak ceiling
 * @param {number} [opts.fadeInSec=0.01]  fade-in length
 * @param {number} [opts.fadeOutSec=0.01] fade-out length
 * @param {boolean} [opts.normalize=true]  loudness-normalise
 * @param {'full'|'fast'} [opts.searchMode='full']  gradient search effort
 * @param {(done:number,total:number)=>void} [opts.onProgress]
 * @returns {{bytes:Uint8Array, sampleRate:number, durationSec:number,
 *            lufs:number, truePeakDb:number, bitrateKbps:number}}
 */
export function encodeAt9(opts) {
  const {
    sampleRate,
    channels,
    channelData,
    bitrateKbps = 192,
    maxDurationSec = 15,
    targetLufs = DEFAULT_LOUDNESS,
    truePeakCeilingDb = DEFAULT_TRUE_PEAK,
    fadeInSec = 0.01,
    fadeOutSec = 0.01,
    normalize = true,
    searchMode = 'full',
    onProgress,
  } = opts || {};

  if (!channelData || channelData.length === 0) throw new Error('channelData is empty');
  if (channels !== undefined && channels !== channelData.length) {
    throw new Error('channels does not match channelData length');
  }
  if (!STEREO_BITRATES.includes(bitrateKbps)) {
    throw new Error('bitrateKbps must be one of ' + STEREO_BITRATES.join(','));
  }

  // 1. Map to stereo and resample to 48 kHz (the encoder only makes 48 kHz streams).
  let pcm = resample(toStereo(channelData), sampleRate, RATE);

  // 2. Choose the excerpt.
  const available = pcm[0].length;
  let count;
  if (maxDurationSec === 'max') {
    count = Math.min(available, maxSamplesFor(bitrateKbps, PS5_SIZE_LIMIT, true));
  } else {
    count = Math.min(available, Math.round(maxDurationSec * RATE));
  }
  if (count <= 0) throw new Error('the selected excerpt is empty');
  pcm = pcm.map((c) => c.subarray(0, count));

  // 3. Loudness normalisation, true-peak limiting, fades.
  const sourceLufs = integratedLoudness(pcm, RATE);
  if (normalize && Number.isFinite(sourceLufs)) {
    const gain = 10.0 ** ((targetLufs - sourceLufs) / 20.0);
    pcm = pcm.map((c) => {
      const out = new Float64Array(c.length);
      for (let i = 0; i < c.length; i++) out[i] = c[i] * gain;
      return out;
    });
  }
  pcm = limitTruePeak(pcm, truePeakCeilingDb, RATE).channels;
  pcm = applyFades(pcm, RATE, fadeInSec, fadeOutSec);
  const finalLufs = integratedLoudness(pcm, RATE);
  const peakDb = truePeakDb(pcm);

  // 4. Encode (float PCM in 16-bit scale) and wrap in the container.
  const encoder = new Encoder(2, bitrateKbps);
  const scaled = pcm.map((c) => {
    const out = new Float64Array(c.length);
    for (let i = 0; i < c.length; i++) out[i] = c[i] * 32768.0;
    return out;
  });
  const data = encoder.encode(scaled, onProgress, searchMode);
  const bytes = buildAt9(
    {
      sampleRate: RATE,
      superframeBytes: encoder.superframeBytes,
      superframeSamples: 1024,
      data: encoder.config,
    },
    2,
    count,
    data,
  );

  return {
    bytes,
    sampleRate: RATE,
    durationSec: count / RATE,
    lufs: finalLufs,
    truePeakDb: peakDb,
    bitrateKbps,
  };
}
