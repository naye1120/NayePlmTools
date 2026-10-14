// SPDX-License-Identifier: GPL-3.0-or-later
//
// ATRAC9 encoder (48 kHz mono/stereo) for the browser.
//
// Ported from blackbearreloaded/ps5-at9-converter (GPL-3.0-or-later),
// ps5at9/atrac9_encoder.py, plus the decoder-derived integer rules it relies
// on from ps5at9/atrac9.py (gradient_curve, precisions, codebook_sets,
// mdct_windows). The Python ProcessPoolExecutor is intentionally NOT ported:
// superframes are encoded sequentially here.
//
// Why the encoder recomputes the decoder's own integer rules: an ATRAC9
// decoder derives precisions, codebook sets and scale-factor predictions from
// a few transmitted parameters. By reproducing those exact integer decisions
// the encoder knows the exact bit cost and exact quantisation noise of every
// choice, so it can pick parameters that both fit the frame budget and stay
// legal. The rules from docs/atrac9-profile.md that this code depends on:
//   * gradient start unit is >= 1 and < end unit, end unit <= 31, and the two
//     values differ (mode 0 never sends equal start/end values here because
//     end_unit is fixed at 31 and start_unit <= 30);
//   * precisions stay <= 15 (P_CAP caps the search, and _write_frame throws if
//     a fine precision is ever selected);
//   * scale factors stay in 0..31 (tight scale factors, clipped);
//   * some spectrum symbols have no Huffman code; _fixMinimum nudges units
//     0..11 so a codable symbol always exists, and the writer asserts the
//     chosen symbol has a non-zero bit length;
//   * band parameters are sent only in the first frame of a superframe and
//     scale factors are predicted only within the superframe, so every
//     superframe decodes on its own (safe for looping).
//
// Performance trade-off: the default path is the faithful full search (about
// 1,200 gradient candidates x every boundary 0..15 per frame). On weak devices
// a caller may pass searchMode: 'fast', which evaluates every second candidate
// and every second boundary. That only changes which legal parameter set is
// chosen -- the size accounting, the bit writer and the validity rules are
// untouched, so a downgraded stream is still a valid stream, just possibly a
// little larger / lower quality. searchMode 'full' is the default.

import {
  QUANT_UNIT_TO_COEFF_INDEX as QIDX,
  QUANT_UNIT_TO_COEFF_COUNT as QCNT,
  QUANT_UNIT_TO_CODEBOOK_INDEX as CBI,
  QUANT_STEP as STEP,
  BAND_TO_QUANT_UNITS,
  SF_WEIGHTS,
  GRADIENT_CURVES,
  SPECTRUM,
  SF_UNSIGNED,
  SF_SIGNED,
  spectrumBook,
} from './tables.js';

export const N = 256;
export const UNITS = 30;
export const FRAMES_PER_SUPERFRAME = 4;
export const SAMPLE_RATE = 48000;
export const SAMPLE_RATE_INDEX = 7;

export const STEREO_BITRATES = [72, 96, 120, 144, 168, 192];
export const MONO_BITRATES = [36, 48, 60, 72, 84, 96, 120, 144];

// Highest band coded at each per-channel bit rate (kb/s). Band 16 is 18 kHz.
const MAX_BAND_BY_CHANNEL_RATE = [[96, 16], [84, 16], [72, 15], [60, 14], [48, 13], [36, 12]];

const INF = Infinity;

// Coefficient -> unit index (np.repeat(arange(UNITS), QCNT)).
const COEFF_UNIT = new Int32Array(N);
for (let u = 0, p = 0; u < UNITS; u++) for (let j = 0; j < QCNT[u]; j++) COEFF_UNIT[p++] = u;

const INV_STEP = new Float64Array(16);
for (let i = 0; i < 16; i++) INV_STEP[i] = 1.0 / STEP[i];
const MAXQ = new Int32Array(16);
for (let i = 0; i < 16; i++) MAXQ[i] = (1 << i) - 1;

class Overflow extends Error {}

// Scratch buffers reused across frames/superframes (keyed by length + slot) so
// the hot search loop does not allocate.
const _scratch = new Map();
function getScratch(len, slot) {
  const key = len * 4 + slot;
  let a = _scratch.get(key);
  if (!a) {
    a = new Float64Array(len);
    _scratch.set(key, a);
  }
  return a;
}

// -- small helpers -------------------------------------------------------------

/** Python's round(): half to even, matching np.rint. */
function rint(x) {
  const f = Math.floor(x);
  const d = x - f;
  if (d < 0.5) return f;
  if (d > 0.5) return f + 1;
  return f % 2 === 0 ? f : f + 1;
}

function _signExtend(value, bits) {
  const sign = 1 << (bits - 1);
  return (value & (sign - 1)) - (value & sign);
}

export function frameBytesFor(bitrateKbps) {
  const value = Math.floor((bitrateKbps * 1000 * N) / (8 * SAMPLE_RATE));
  if (value * 8 * SAMPLE_RATE !== bitrateKbps * 1000 * N) {
    throw new Error('unsupported bit rate ' + bitrateKbps + ' kb/s');
  }
  return value;
}

/** Largest excerpt (samples) that fits `limitBytes` at `bitrateKbps`. */
export function maxSamplesFor(bitrateKbps, limitBytes, loop = true) {
  const superframeBytes = frameBytesFor(bitrateKbps) * FRAMES_PER_SUPERFRAME;
  const headerSize = 12 + 60 + 20 + (loop ? 68 : 0) + 8;
  const superframes = Math.floor((limitBytes - headerSize) / superframeBytes);
  return superframes * FRAMES_PER_SUPERFRAME * N - N;
}

// -- decoder-derived integer rules --------------------------------------------

/** The 31-entry allocation gradient, exactly as the decoder derives it. */
export function gradientCurve(mode, startUnit, endUnit, startValue, endValue, unitCount) {
  const grad = new Int32Array(31).fill(startValue);
  const stop = Math.min(unitCount, 30);
  for (let i = endUnit; i <= stop; i++) grad[i] = endValue;
  const units = endUnit - startUnit;
  const values = endValue - startValue;
  if (units > 0 && values !== 0) {
    const curve = GRADIENT_CURVES[units - 1];
    if (values < 0) {
      const scale = (-values - 1) / 31.0;
      for (let i = startUnit; i < endUnit; i++) {
        grad[i] = startValue - 1 - Math.trunc(curve[i - startUnit] * scale);
      }
    } else {
      const scale = (values - 1) / 31.0;
      for (let i = startUnit; i < endUnit; i++) {
        grad[i] = startValue + 1 + Math.trunc(curve[i - startUnit] * scale);
      }
    }
  }
  return grad;
}

/** Extra precision granted to units next to a large scale-factor step. */
function precisionMask(sf, units) {
  const mask = new Int32Array(UNITS);
  for (let i = 1; i < units; i++) {
    const delta = sf[i] - sf[i - 1];
    if (delta > 1) mask[i] += Math.min(delta - 1, 5);
    else if (delta < -1) mask[i - 1] += Math.min(-delta - 1, 5);
  }
  return mask;
}

/** (coarse, fine) word-length precisions for one channel. */
export function precisions(sf, grad, mode, boundary, unitCount) {
  const coarse = new Int32Array(30);
  if (mode) {
    const mask = precisionMask(sf, unitCount);
    for (let i = 0; i < unitCount; i++) {
      let p = sf[i] + mask[i] - grad[i];
      if (p > 0) p = mode === 1 ? Math.floor(p / 2) : mode === 2 ? Math.floor((3 * p) / 8) : Math.floor(p / 4);
      coarse[i] = p;
    }
  } else {
    for (let i = 0; i < unitCount; i++) coarse[i] = sf[i] - grad[i];
  }
  const fine = new Int32Array(30);
  for (let i = 0; i < unitCount; i++) {
    if (coarse[i] < 1) coarse[i] = 1;
    if (i < boundary) coarse[i] += 1;
    if (coarse[i] > 15) {
      fine[i] = coarse[i] - 15;
      coarse[i] = 15;
    }
  }
  return { coarse, fine };
}

/** Codebook set (0/1) per unit, as the decoder picks it from the scale factors. */
export function codebookSets(sf, codedUnits, highSampleRate) {
  const sets = new Int32Array(30);
  if (codedUnits <= 1 || highSampleRate) return sets;
  const s = new Int32Array(codedUnits + 1);
  for (let i = 0; i < codedUnits; i++) s[i] = sf[i];
  s[codedUnits] = sf[codedUnits - 1];
  let avg = 0;
  if (codedUnits > 12) {
    let sum = 0;
    for (let i = 0; i < 12; i++) sum += s[i];
    avg = Math.floor((sum + 6) / 12);
  }
  for (let i = 8; i < codedUnits; i++) {
    const lo = Math.min(s[i - 1], s[i + 1]);
    if (s[i] - lo >= 3 || 2 * s[i] - s[i - 1] - s[i + 1] >= 3) sets[i] = 1;
  }
  for (let i = 12; i < codedUnits; i++) {
    if (!sets[i]) {
      const lo = Math.min(s[i - 1], s[i + 1]);
      if (s[i] - lo >= 2 && s[i] >= avg - (QCNT[i] === 16 ? 1 : 0)) sets[i] = 1;
    }
  }
  return sets;
}

/** Analysis (encoder) and synthesis (decoder) half-windows of length n. */
export function mdctWindows(frameSamples) {
  const analysis = new Float64Array(frameSamples);
  for (let n = 0; n < frameSamples; n++) {
    analysis[n] = (Math.sin(((n + 0.5) / frameSamples - 0.5) * Math.PI) + 1.0) * 0.5;
  }
  const synthesis = new Float64Array(frameSamples);
  for (let n = 0; n < frameSamples; n++) {
    const rev = analysis[frameSamples - 1 - n];
    synthesis[n] = analysis[n] / (rev * rev + analysis[n] * analysis[n]);
  }
  return { analysis, synthesis };
}

// -- FFT (psychoacoustic model only) ------------------------------------------

const _fftRe = new Float64Array(512);
const _fftIm = new Float64Array(512);
const _twiddles = new Map();

function twiddles(n) {
  let tw = _twiddles.get(n);
  if (tw) return tw;
  const re = [];
  const im = [];
  for (let size = 2; size <= n; size <<= 1) {
    const half = size >> 1;
    const wr = new Float64Array(half);
    const wi = new Float64Array(half);
    for (let k = 0; k < half; k++) {
      const ang = (-2 * Math.PI * k) / size;
      wr[k] = Math.cos(ang);
      wi[k] = Math.sin(ang);
    }
    re.push(wr);
    im.push(wi);
  }
  tw = { re, im };
  _twiddles.set(n, tw);
  return tw;
}

/** |rfft(input)|^2 for a real input of length n (power of two) into out[0..n/2]. */
function powerSpectrum(input, n, out) {
  const tw = twiddles(n);
  const re = _fftRe;
  const im = _fftIm;
  const levels = Math.log2(n) | 0;
  for (let i = 0; i < n; i++) {
    let j = 0;
    for (let b = 0; b < levels; b++) j = (j << 1) | ((i >> b) & 1);
    re[j] = input[i];
    im[j] = 0;
  }
  let li = 0;
  for (let size = 2; size <= n; size <<= 1, li++) {
    const half = size >> 1;
    const wr = tw.re[li];
    const wi = tw.im[li];
    for (let i = 0; i < n; i += size) {
      for (let k = 0; k < half; k++) {
        const a = i + k;
        const b = a + half;
        const tr = wr[k] * re[b] - wi[k] * im[b];
        const ti = wr[k] * im[b] + wi[k] * re[b];
        re[b] = re[a] - tr;
        im[b] = im[a] - ti;
        re[a] += tr;
        im[a] += ti;
      }
    }
  }
  for (let k = 0; k <= n / 2; k++) out[k] = re[k] * re[k] + im[k] * im[k];
}

// -- spectrum Huffman books ----------------------------------------------------

/** Per word length: code/length lookup plus the grouping of coefficients. */
class SpectrumBooks {
  constructor(wl) {
    this.wl = wl;
    this.mask = (1 << wl) - 1;
    const size = 1 << (wl === 2 ? 4 * wl : wl <= 4 ? 2 * wl : wl);
    this.size = size;
    this.bits = new Int32Array(2 * 4 * size);
    this.codes = new Int32Array(2 * 4 * size);
    const vcp = [0, 0, 0, 0];
    for (let s = 0; s < 2; s++) {
      for (let c = 0; c < 4; c++) {
        const book = spectrumBook(s, wl, c);
        if (!book) continue;
        const base = (s * 4 + c) * size;
        for (let i = 0; i < book.bits.length; i++) {
          this.bits[base + i] = book.bits[i];
          this.codes[base + i] = book.codes[i];
        }
        vcp[c] = book.valueCountPower;
      }
    }
    this.vcp = vcp;
    const rows = [];
    const units = [];
    for (let u = 0; u < UNITS; u++) {
      const vc = 1 << vcp[CBI[u]];
      const groups = Math.floor(QCNT[u] / vc);
      for (let g = 0; g < groups; g++) {
        const start = QIDX[u] + g * vc;
        const row = new Int32Array(4).fill(N);
        for (let j = 0; j < vc; j++) row[j] = start + j;
        rows.push(row);
        units.push(u);
      }
    }
    this.nGroups = rows.length;
    this.idx = new Int32Array(this.nGroups * 4);
    this.unitOf = new Int32Array(this.nGroups);
    this.cbiOf = new Int32Array(this.nGroups);
    for (let g = 0; g < this.nGroups; g++) {
      this.idx.set(rows[g], g * 4);
      this.unitOf[g] = units[g];
      this.cbiOf[g] = CBI[units[g]];
    }
    this.shift = [0, wl, 2 * wl, 3 * wl];
    this.groupStart = new Int32Array(UNITS + 1);
    let gi = 0;
    for (let u = 0; u < UNITS; u++) {
      this.groupStart[u] = gi;
      while (gi < this.nGroups && this.unitOf[gi] === u) gi++;
    }
    this.groupStart[UNITS] = this.nGroups;
  }

  bitAt(set, cbi, sym) {
    return this.bits[(set * 4 + cbi) * this.size + sym];
  }

  codeAt(set, cbi, sym) {
    return this.codes[(set * 4 + cbi) * this.size + sym];
  }
}

const BOOKS = {};
for (let wl = 2; wl <= 7; wl++) BOOKS[wl] = new SpectrumBooks(wl);

// -- gradient candidates -------------------------------------------------------

const D_MIN = -40;
const D_MAX = 50;
const D_SPAN = D_MAX - D_MIN + 1;
const P_CAP = 17; // precisions above 15 would need fine bits: never chosen

function modePrecision(mode, d) {
  let v = d;
  if (mode === 1) v = d > 0 ? Math.floor(d / 2) : d;
  else if (mode === 2) v = d > 0 ? Math.floor((3 * d) / 8) : d;
  else if (mode === 3) v = d > 0 ? Math.floor(d / 4) : d;
  return Math.max(1, Math.min(P_CAP, v));
}

// _P_OF_D[mode][d + 40] and the +1 variant, as in the Python lookup tables.
const P_OF_D = [];
const P1_OF_D = [];
for (let mode = 0; mode < 4; mode++) {
  const a = new Int32Array(D_SPAN);
  const b = new Int32Array(D_SPAN);
  for (let i = 0; i < D_SPAN; i++) {
    const d = D_MIN + i;
    a[i] = modePrecision(mode, d);
    b[i] = Math.min(a[i] + 1, P_CAP);
  }
  P_OF_D.push(a);
  P1_OF_D.push(b);
}

const _gradRowCache = new Map();
function gradRow(m, s, a, units) {
  const key = (m * 31 + s) * 31 + a;
  let row = _gradRowCache.get(key);
  if (!row) {
    row = gradientCurve(m, s, 31, a, 31, 30).slice(0, UNITS);
    _gradRowCache.set(key, row);
  }
  return units < UNITS ? row.subarray(0, units) : row;
}

class Candidates {
  constructor(units) {
    const keys = [];
    const seen = new Set();
    const add = (m, s, a) => {
      const k = (m * 31 + s) * 31 + a;
      if (!seen.has(k)) {
        seen.add(k);
        keys.push([m, s, a]);
      }
    };
    for (const m of [1, 2, 3]) {
      const lo = Math.max(1, units - 12);
      const hi = Math.min(30, units + 2);
      for (let s = lo; s <= hi; s++) for (let a = 0; a < 27; a++) add(m, s, a);
    }
    for (let a = 0; a < 31; a++) {
      add(1, 1, a);
      for (const s of [Math.max(1, units - 8), Math.max(1, units - 4), Math.min(30, units)]) add(0, s, a);
    }
    keys.sort((x, y) => x[0] - y[0] || x[1] - y[1] || x[2] - y[2]);
    this.count = keys.length;
    this.units = units;
    this.params = keys.map(([m, s, a]) => [m, s, 31, a, 31]);
    this.mode = new Int32Array(this.count);
    this.mode0 = new Uint8Array(this.count);
    this.grad = new Int32Array(this.count * units);
    this.offset = new Int32Array(this.count * units);
    this.headerBits = new Int32Array(this.count);
    for (let c = 0; c < this.count; c++) {
      const [m, s, a] = keys[c];
      this.mode[c] = m;
      this.mode0[c] = m === 0 ? 1 : 0;
      const row = gradRow(m, s, a, units);
      this.grad.set(row, c * units);
      for (let u = 0; u < units; u++) {
        this.offset[c * units + u] = (m * units + u) * D_SPAN - D_MIN;
      }
      this.headerBits[c] = m === 0 ? 2 + 6 + 6 + 5 + 5 + 4 : 2 + 5 + 5 + 4;
    }
  }
}

const _candCache = new Map();
function candidatesFor(units) {
  let c = _candCache.get(units);
  if (!c) {
    c = new Candidates(units);
    _candCache.set(units, c);
  }
  return c;
}

// -- psychoacoustics -----------------------------------------------------------

function bark(f) {
  return 13.0 * Math.atan(0.00076 * f) + 3.5 * Math.atan((f / 7500.0) ** 2);
}

function athDb(f) {
  const k = Math.max(f, 20.0) / 1000.0;
  return 3.64 * k ** -0.8 - 6.5 * Math.exp(-0.6 * (k - 3.3) ** 2) + 1e-3 * k ** 4;
}

/** Per-unit masking thresholds, in the MDCT coefficient energy domain. */
class Psycho {
  constructor() {
    this.FULL_SCALE_SPL = 110.0;
    this.SLOPE_BELOW = 27.0;
    this.SLOPE_ABOVE = 12.0;
    this.NOISE_OFFSET = 5.5;
    this.TONE_OFFSET = 14.5;
    this.TONE_OFFSET_MAX = 25.0;
    this.FLATNESS_TONAL = -25.0;
    const binHz = SAMPLE_RATE / 2.0 / N;
    const lo = new Float64Array(UNITS);
    const hi = new Float64Array(UNITS);
    for (let u = 0; u < UNITS; u++) {
      lo[u] = QIDX[u] * binHz;
      hi[u] = QIDX[u + 1] * binHz;
    }
    const z = new Float64Array(UNITS);
    for (let u = 0; u < UNITS; u++) z[u] = bark((lo[u] + hi[u]) / 2.0);
    this.z = z;
    this.spread = new Float64Array(UNITS * UNITS); // [maskee, masker]
    for (let i = 0; i < UNITS; i++) {
      for (let j = 0; j < UNITS; j++) {
        const dz = z[i] - z[j];
        const atten = dz < 0 ? this.SLOPE_BELOW * -dz : this.SLOPE_ABOVE * dz;
        this.spread[i * UNITS + j] = 10.0 ** (-atten / 10.0);
      }
    }
    this.ath = new Float64Array(UNITS);
    for (let u = 0; u < UNITS; u++) {
      let minAth = Infinity;
      for (let j = 0; j < 9; j++) {
        const f = lo[u] + (j * (hi[u] - lo[u])) / 8.0;
        const v = athDb(f);
        if (v < minAth) minAth = v;
      }
      this.ath[u] = 32768.0 ** 2 * 10.0 ** ((minAth - this.FULL_SCALE_SPL) / 10.0);
    }
    this.bins = [];
    for (let u = 0; u < UNITS; u++) {
      this.bins.push([Math.max(QIDX[u] - 1, 0), Math.min(QIDX[u + 1] + 2, N + 1)]);
    }
  }

  /** energy: (frames, UNITS); power: (frames, N+1). Returns thresholds (frames, UNITS). */
  thresholds(energy, power, frames) {
    const out = new Float64Array(frames * UNITS);
    const ln10 = Math.log(10.0);
    for (let f = 0; f < frames; f++) {
      for (let u = 0; u < UNITS; u++) {
        const a = this.bins[u][0];
        const b = this.bins[u][1];
        let logSum = 0.0;
        let pSum = 0.0;
        for (let i = a; i < b; i++) {
          const p = power[f * (N + 1) + i];
          logSum += Math.log(p + 1e-12);
          pSum += p;
        }
        const cnt = b - a;
        const gm = logSum / cnt;
        const am = Math.log(pSum / cnt + 1e-12);
        const sfmDb = (10.0 / ln10) * (gm - am);
        let tonal = sfmDb / this.FLATNESS_TONAL;
        tonal = Math.max(0.0, Math.min(1.0, tonal));
        const offset =
          tonal * Math.min(this.TONE_OFFSET + this.z[u], this.TONE_OFFSET_MAX) +
          (1.0 - tonal) * this.NOISE_OFFSET;
        let masked = 0.0;
        for (let m = 0; m < UNITS; m++) masked += energy[f * UNITS + m] * this.spread[u * UNITS + m];
        masked *= 10.0 ** (-offset / 10.0);
        out[f * UNITS + u] = Math.max(masked, this.ath[u]);
      }
    }
    return out;
  }
}

let _psycho = null;
function psycho() {
  if (_psycho === null) _psycho = new Psycho();
  return _psycho;
}

// -- bit writer ----------------------------------------------------------------

/** MSB-first bit writer. All writes are <= 16 bits, so a JS number accumulator is exact. */
class Bits {
  constructor() {
    this.out = [];
    this.acc = 0;
    this.n = 0;
  }

  put(value, bits) {
    if (bits <= 0) return;
    const v = value & ((1 << bits) - 1);
    this.acc = (this.acc << bits) | v;
    this.n += bits;
    while (this.n >= 8) {
      this.n -= 8;
      this.out.push((this.acc >>> this.n) & 0xff);
    }
    this.acc &= (1 << this.n) - 1;
  }

  /** Left-align the tail with zero padding, exactly like _Bits.to_bytes. */
  toBytes() {
    const out = this.out.slice();
    if (this.n > 0) out.push((this.acc << (8 - this.n)) & 0xff);
    return Uint8Array.from(out);
  }
}

// -- scale factor coding -------------------------------------------------------

function sfClc(sf) {
  const units = sf.length;
  let lo = Infinity;
  let hi = -Infinity;
  for (let i = 0; i < units; i++) {
    if (sf[i] < lo) lo = sf[i];
    if (sf[i] > hi) hi = sf[i];
  }
  for (const length of [2, 3, 4]) {
    if (hi - lo < 1 << length) return [2 + 2 + 5 + units * length, 1, ['clc', length, lo]];
  }
  return [2 + 2 + units * 5, 1, ['clc', 5, 0]];
}

function sfDeltaOffset(sf, mode) {
  const units = sf.length;
  let best = null;
  const u = new Int32Array(units);
  for (const length of [3, 4, 5, 6]) {
    const mask = (1 << length) - 1;
    const bitsTab = SF_UNSIGNED[length].bits;
    for (let w = 0; w < 8; w++) {
      const weights = SF_WEIGHTS[w];
      let minT = Infinity;
      for (let i = 0; i < units; i++) {
        const t = sf[i] + weights[i];
        if (t < minT) minT = t;
      }
      const base = Math.max(0, Math.min(31, minT));
      let uMin = Infinity;
      let uMax = -Infinity;
      for (let i = 0; i < units; i++) {
        const v = sf[i] + weights[i] - base;
        u[i] = v;
        if (v < uMin) uMin = v;
        if (v > uMax) uMax = v;
      }
      if (uMin < 0 || uMax >= 1 << length) continue;
      let sum = 0;
      for (let i = 1; i < units; i++) sum += bitsTab[(u[i] - u[i - 1]) & mask];
      const bits = sum + (2 + 3 + 5 + 2 + length);
      if (best === null || bits < best[0]) best = [bits, mode, ['delta', w, base, length]];
    }
  }
  return best;
}

function sfDistance(sf, baseline, baselineLen, mode) {
  const units = sf.length;
  const count = Math.min(units, baselineLen);
  let lo = 0;
  let hi = 0;
  if (count) {
    lo = Infinity;
    hi = -Infinity;
    for (let i = 0; i < count; i++) {
      const d = sf[i] - baseline[i];
      if (d < lo) lo = d;
      if (d > hi) hi = d;
    }
  }
  let best = null;
  for (const length of [2, 3, 4, 5]) {
    const half = 1 << (length - 1);
    if (lo < -half || hi >= half) continue;
    const mask = (1 << length) - 1;
    const tab = SF_SIGNED[length].bits;
    let sum = 0;
    let ok = true;
    for (let i = 0; i < count; i++) {
      const sym = (sf[i] - baseline[i]) & mask;
      const b = tab[sym];
      if (!b) {
        ok = false;
        break;
      }
      sum += b;
    }
    if (count && !ok) continue;
    const bits = 2 + 2 + sum + (units - count) * 5;
    if (best === null || bits < best[0]) best = [bits, mode, ['distance', length, baseline, count]];
  }
  return best;
}

function sfDeltaBaseline(sf, baseline, baselineLen, mode) {
  const units = sf.length;
  const count = Math.min(units, baselineLen);
  if (count < 1) return null;
  let rMin = Infinity;
  let rMax = -Infinity;
  const r = new Int32Array(count);
  for (let i = 0; i < count; i++) {
    r[i] = sf[i] - baseline[i];
    if (r[i] < rMin) rMin = r[i];
    if (r[i] > rMax) rMax = r[i];
  }
  const base = Math.min(Math.max(rMin, -16), 15);
  const u = new Int32Array(count);
  let uMax = -Infinity;
  for (let i = 0; i < count; i++) {
    u[i] = r[i] - base;
    if (u[i] < 0) return null;
    if (u[i] > uMax) uMax = u[i];
  }
  let best = null;
  for (const length of [1, 2, 3, 4]) {
    if (uMax >= 1 << length) continue;
    const mask = (1 << length) - 1;
    const tab = SF_UNSIGNED[length].bits;
    let sum = 0;
    for (let i = 1; i < count; i++) sum += tab[(u[i] - u[i - 1]) & mask];
    const bits = 2 + 5 + 2 + length + sum + (units - count) * 5;
    if (best === null || bits < best[0]) best = [bits, mode, ['offset', length, base, baseline, count]];
  }
  return best;
}

function chooseSfCoding(sf, channel, first, prevSf, prevUnits, ch0Sf) {
  const options = [];
  if (channel === 0) {
    options.push(sfDeltaOffset(sf, 0), sfClc(sf));
    if (!first) {
      options.push(sfDistance(sf, prevSf, prevUnits, 2), sfDeltaBaseline(sf, prevSf, prevUnits, 3));
    }
  } else {
    options.push(
      sfDeltaOffset(sf, 0),
      sfDistance(sf, ch0Sf, sf.length, 1),
      sfDeltaBaseline(sf, ch0Sf, sf.length, 2),
    );
    if (!first) options.push(sfDistance(sf, prevSf, prevUnits, 3));
  }
  let best = null;
  for (const o of options) if (o !== null && (best === null || o[0] < best[0])) best = o;
  return best;
}

function writeSf(out, sf, coding) {
  const [, mode, spec] = coding;
  const units = sf.length;
  out.put(mode, 2);
  const kind = spec[0];
  if (kind === 'clc') {
    const [, length, base] = spec;
    out.put(length - 2, 2);
    if (length < 5) out.put(base, 5);
    for (let i = 0; i < units; i++) out.put(sf[i] - base, length);
  } else if (kind === 'delta') {
    const [, w, base, length] = spec;
    const weights = SF_WEIGHTS[w];
    const mask = (1 << length) - 1;
    const u = new Int32Array(units);
    for (let i = 0; i < units; i++) u[i] = sf[i] + weights[i] - base;
    out.put(w, 3);
    out.put(base, 5);
    out.put(length - 3, 2);
    out.put(u[0], length);
    const bitsTab = SF_UNSIGNED[length].bits;
    const codesTab = SF_UNSIGNED[length].codes;
    for (let i = 1; i < units; i++) {
      const d = (u[i] - u[i - 1]) & mask;
      out.put(codesTab[d], bitsTab[d]);
    }
  } else if (kind === 'distance') {
    const [, length, baseline, count] = spec;
    const mask = (1 << length) - 1;
    out.put(length - 2, 2);
    const bitsTab = SF_SIGNED[length].bits;
    const codesTab = SF_SIGNED[length].codes;
    for (let i = 0; i < count; i++) {
      const sym = (sf[i] - baseline[i]) & mask;
      out.put(codesTab[sym], bitsTab[sym]);
    }
    for (let i = count; i < units; i++) out.put(sf[i], 5);
  } else {
    const [, length, base, baseline, count] = spec;
    const mask = (1 << length) - 1;
    out.put(base + 16, 5);
    out.put(length - 1, 2);
    const u = new Int32Array(count);
    for (let i = 0; i < count; i++) u[i] = sf[i] - baseline[i] - base;
    out.put(u[0], length);
    const bitsTab = SF_UNSIGNED[length].bits;
    const codesTab = SF_UNSIGNED[length].codes;
    for (let i = 1; i < count; i++) {
      const d = (u[i] - u[i - 1]) & mask;
      out.put(codesTab[d], bitsTab[d]);
    }
    for (let i = count; i < units; i++) out.put(sf[i], 5);
  }
}

// -- per channel-frame analysis ------------------------------------------------

/** Enforce the codebook rules for units 0-11 (see atrac9_encoder._fix_minimum). */
function fixMinimum(q, xn, _units) {
  // q is (15, N) flat; q[p-1] is precision p (word length p+1).
  for (let p = 1; p <= 3; p++) {
    const need = 1 << (p - 1);
    const row = (p - 1) * N;
    for (let g = 0; g < 8; g++) {
      const i0 = g * 2;
      const a = q[row + i0];
      const b = q[row + i0 + 1];
      if (Math.max(Math.abs(a), Math.abs(b)) < need) {
        const j = Math.abs(xn[i0]) >= Math.abs(xn[i0 + 1]) ? 0 : 1;
        const sign = xn[i0 + j] >= 0 ? 1 : -1;
        q[row + i0 + j] = sign * need;
      }
    }
  }
  for (let g = 0; g < 4; g++) {
    const i0 = 16 + g * 4;
    let any = false;
    for (let j = 0; j < 4; j++) if (q[i0 + j] !== 0) any = true;
    if (!any) {
      let jBest = 0;
      let bestAbs = -1;
      for (let j = 0; j < 4; j++) {
        const a = Math.abs(xn[i0 + j]);
        if (a > bestAbs) {
          bestAbs = a;
          jBest = j;
        }
      }
      q[i0 + jBest] = xn[i0 + jBest] >= 0 ? 1 : -1;
    }
  }
}

/** Quantisation results for every precision of one channel in one frame. */
class ChannelFrame {
  constructor(x, sf, units, weight) {
    this.sf = sf;
    this.units = units;
    this.sets = codebookSets(sf, units, false);
    this.mask = precisionMask(sf, units);
    // scale = 2^(sf - 15) per coefficient; xn = x / scale.
    const xn = new Float64Array(N);
    for (let i = 0; i < N; i++) xn[i] = x[i] / 2 ** (sf[COEFF_UNIT[i]] - 15.0);
    // q[p-1] = rint(xn / STEP[p]) clipped to +-MAXQ[p].
    const q = new Int32Array(15 * N);
    for (let p = 1; p <= 15; p++) {
      const inv = INV_STEP[p];
      const lim = MAXQ[p];
      const row = (p - 1) * N;
      for (let i = 0; i < N; i++) {
        let v = rint(xn[i] * inv);
        if (v > lim) v = lim;
        else if (v < -lim) v = -lim;
        q[row + i] = v;
      }
    }
    fixMinimum(q, xn, units);
    // noise[u] = sum over the unit's coefficients of (xn - q*STEP)^2, * weight.
    const noise = new Float64Array(15 * units);
    for (let p = 1; p <= 15; p++) {
      const row = (p - 1) * N;
      const st = STEP[p];
      const outRow = (p - 1) * units;
      for (let u = 0; u < units; u++) {
        let acc = 0.0;
        for (let i = QIDX[u]; i < QIDX[u + 1]; i++) {
          const e = (xn[i] - q[row + i] * st) * 2 ** (sf[COEFF_UNIT[i]] - 15.0);
          acc += e * e;
        }
        noise[outRow + u] = acc * weight[u];
      }
    }
    this.q = q;
    this.noise = noise;
    // Huffman cost per precision and unit for precisions 1..6 (word lengths 2..7).
    const cost = new Float64Array(15 * units);
    this.sym = {};
    for (let p = 1; p <= 6; p++) {
      const book = BOOKS[p + 1];
      const row = (p - 1) * N;
      const syms = new Int32Array(book.nGroups);
      for (let g = 0; g < book.nGroups; g++) {
        let sym = 0;
        for (let j = 0; j < 4; j++) {
          const ci = book.idx[g * 4 + j];
          const v = (ci < N ? q[row + ci] : 0) & book.mask;
          sym |= v << book.shift[j];
        }
        syms[g] = sym;
      }
      this.sym[p] = syms;
      const costRow = (p - 1) * units;
      for (let g = 0; g < book.nGroups; g++) {
        const u = book.unitOf[g];
        if (u >= units) continue;
        cost[costRow + u] += book.bitAt(this.sets[u], book.cbiOf[g], syms[g]);
      }
    }
    // Precisions 7..15 use fixed-length codes: wl = p + 1 bits per coefficient.
    for (let p = 7; p <= 15; p++) {
      const costRow = (p - 1) * units;
      for (let u = 0; u < units; u++) cost[costRow + u] = QCNT[u] * (p + 1);
    }
    this.cost = cost;
  }
}

// -- encoder -------------------------------------------------------------------

export class Encoder {
  constructor(channels = 2, bitrate = 192) {
    if (channels !== 1 && channels !== 2) throw new Error('ATRAC9 encoder supports mono or stereo');
    const allowed = channels === 2 ? STEREO_BITRATES : MONO_BITRATES;
    if (!allowed.includes(bitrate)) {
      throw new Error('bit rate must be one of ' + allowed.join(',') + ' kb/s for ' + channels + ' channel(s)');
    }
    this.channels = channels;
    this.bitrate = bitrate;
    this.frameBytes = frameBytesFor(bitrate);
    this.superframeBytes = this.frameBytes * FRAMES_PER_SUPERFRAME;
    const perChannel = Math.floor(bitrate / channels);
    this.maxBand = 12;
    for (const [rate, band] of MAX_BAND_BY_CHANNEL_RATE) {
      if (perChannel >= rate) {
        this.maxBand = band;
        break;
      }
    }
    // Analysis MDCT basis: basis[n][k] = cos(pi/N (k+0.5)(n+0.5+N/2)) * window[n] * 2/N.
    const { analysis } = mdctWindows(N);
    const window = new Float64Array(2 * N);
    for (let n = 0; n < N; n++) window[n] = analysis[n];
    for (let n = 0; n < N; n++) window[N + n] = analysis[N - 1 - n];
    this.basis = new Float64Array(2 * N * N);
    for (let n = 0; n < 2 * N; n++) {
      const nn = n + 0.5 + N / 2.0;
      const w = window[n] * (2.0 / N);
      for (let k = 0; k < N; k++) {
        this.basis[n * N + k] = Math.cos((Math.PI / N) * (k + 0.5) * nn) * w;
      }
    }
    this.hann = new Float64Array(2 * N);
    for (let i = 0; i < 2 * N; i++) this.hann[i] = 0.5 * (1.0 - Math.cos((2 * Math.PI * i) / (2 * N - 1)));
    this.psycho = psycho();
    // Config word: FE marker, sample rate 7, channel config, frame size, 4 frames.
    const channelConfig = channels === 2 ? 2 : 0;
    const word = ((0xfe << 24) | (SAMPLE_RATE_INDEX << 20) | (channelConfig << 17) | ((this.frameBytes - 1) << 5) | (2 << 3)) >>> 0;
    this.config = Uint8Array.from([(word >>> 24) & 0xff, (word >>> 16) & 0xff, (word >>> 8) & 0xff, word & 0xff]);
  }

  /**
   * Encode float PCM (channels, samples) in 16-bit scale. Returns the data chunk
   * bytes (whole superframes). `onProgress(done, total)` is called per superframe.
   */
  encode(channelData, onProgress, searchMode = 'full') {
    const channels = this.channels;
    if (channelData.length !== channels) throw new Error('expected ' + channels + ' channel(s)');
    const pcm = channelData.map((c) => (c instanceof Float64Array ? c : Float64Array.from(c)));
    const samples = pcm[0].length;
    const superframes = Math.ceil((samples + N) / (N * FRAMES_PER_SUPERFRAME));
    const frames = superframes * FRAMES_PER_SUPERFRAME;
    const paddedLen = (frames + 1) * N;
    const padded = pcm.map((c) => {
      const p = new Float64Array(paddedLen);
      p.set(c, N);
      return p;
    });
    // spectra[ch][frame*N + k]
    const spectra = [];
    const energy = [];
    const power = [];
    const powerScratch = new Float64Array(N + 1);
    for (let c = 0; c < channels; c++) {
      const sp = new Float64Array(frames * N);
      const en = new Float64Array(frames * UNITS);
      const pw = new Float64Array(frames * (N + 1));
      const block = new Float64Array(2 * N);
      const win = new Float64Array(2 * N);
      for (let f = 0; f < frames; f++) {
        const base = f * N;
        for (let i = 0; i < 2 * N; i++) block[i] = padded[c][base + i];
        // MDCT: spectra[k] = sum_n block[n] * basis[n*N + k]
        const rowOut = f * N;
        for (let k = 0; k < N; k++) sp[rowOut + k] = 0;
        for (let n = 0; n < 2 * N; n++) {
          const bn = block[n];
          if (bn === 0) continue;
          const off = n * N;
          for (let k = 0; k < N; k++) sp[rowOut + k] += bn * this.basis[off + k];
        }
        for (let i = 0; i < 2 * N; i++) win[i] = block[i] * this.hann[i];
        powerSpectrum(win, 2 * N, powerScratch);
        pw.set(powerScratch, f * (N + 1));
        for (let u = 0; u < UNITS; u++) {
          let e = 0.0;
          for (let i = QIDX[u]; i < QIDX[u + 1]; i++) {
            const v = sp[rowOut + i];
            e += v * v;
          }
          en[f * UNITS + u] = e;
        }
      }
      spectra.push(sp);
      energy.push(en);
      power.push(pw);
    }
    // Masking thresholds per (channel, frame).
    const masks = [];
    for (let c = 0; c < channels; c++) masks.push(this.psycho.thresholds(energy[c], power[c], frames));
    // Tight scale factors: smallest sf with 2^(sf-15) >= unit peak.
    const sfs = [];
    for (let c = 0; c < channels; c++) {
      const sf = new Int32Array(frames * UNITS);
      for (let f = 0; f < frames; f++) {
        for (let u = 0; u < UNITS; u++) {
          let peak = 0.0;
          for (let i = QIDX[u]; i < QIDX[u + 1]; i++) {
            const a = Math.abs(spectra[c][f * N + i]);
            if (a > peak) peak = a;
          }
          let v = 0;
          if (peak > 0) v = Math.ceil(Math.log2(Math.max(peak, 1e-300))) + 15;
          sf[f * UNITS + u] = Math.max(0, Math.min(31, v));
        }
      }
      sfs.push(sf);
    }
    const bands = this._chooseBands(energy, masks, superframes, frames);
    // Encode superframes sequentially (no ProcessPoolExecutor in the browser).
    const out = [];
    for (let s = 0; s < superframes; s++) {
      const rows = [];
      for (let c = 0; c < channels; c++) {
        rows.push({
          spectra: spectra[c].subarray(s * FRAMES_PER_SUPERFRAME * N, (s + 1) * FRAMES_PER_SUPERFRAME * N),
          sfs: sfs[c].subarray(s * FRAMES_PER_SUPERFRAME * UNITS, (s + 1) * FRAMES_PER_SUPERFRAME * UNITS),
          masks: masks[c].subarray(s * FRAMES_PER_SUPERFRAME * UNITS, (s + 1) * FRAMES_PER_SUPERFRAME * UNITS),
        });
      }
      let band = bands[s];
      for (;;) {
        try {
          out.push(this._superframe(rows, band, searchMode));
          break;
        } catch (e) {
          if (!(e instanceof Overflow)) throw e;
          if (band <= 3) throw new Error('superframe cannot be encoded within its size');
          band -= 1; // narrow the coded bandwidth until the superframe fits
        }
      }
      if (onProgress) onProgress(s + 1, superframes);
    }
    let total = 0;
    for (const b of out) total += b.length;
    const data = new Uint8Array(total);
    let pos = 0;
    for (const b of out) {
      data.set(b, pos);
      pos += b.length;
    }
    return data;
  }

  _chooseBands(energy, masks, superframes, _frames) {
    const audible = [];
    for (let s = 0; s < superframes; s++) {
      const top = new Int32Array(UNITS);
      for (let u = 0; u < UNITS; u++) {
        let any = false;
        for (let f = s * FRAMES_PER_SUPERFRAME; f < (s + 1) * FRAMES_PER_SUPERFRAME && !any; f++) {
          for (let c = 0; c < this.channels; c++) {
            if (energy[c][f * UNITS + u] > masks[c][f * UNITS + u]) {
              any = true;
              break;
            }
          }
        }
        top[u] = any ? 1 : 0;
      }
      // Highest audible unit + 1 (0 when silent).
      let t = 0;
      for (let u = UNITS - 1; u >= 0; u--) {
        if (top[u]) {
          t = u + 1;
          break;
        }
      }
      audible.push(t);
    }
    const need = audible.map((t) => {
      for (let b = 3; b <= 18; b++) if (BAND_TO_QUANT_UNITS[b] >= t) return b;
      return 18;
    });
    const smooth = need.slice();
    for (const d of [1, 2]) {
      for (let i = d; i < smooth.length; i++) smooth[i] = Math.max(smooth[i], need[i - d]);
      for (let i = 0; i < smooth.length - d; i++) smooth[i] = Math.max(smooth[i], need[i + d]);
    }
    return smooth.map((b) => Math.max(3, Math.min(this.maxBand, b)));
  }

  _superframe(rows, band, searchMode) {
    const units = BAND_TO_QUANT_UNITS[band];
    const cands = candidatesFor(units);
    const stereo = this.channels === 2;
    const coarse = searchMode === 'fast';
    const frames = [];
    let prevSf = new Array(this.channels).fill(null);
    for (let f = 0; f < FRAMES_PER_SUPERFRAME; f++) {
      const first = f === 0;
      let fixed = 2 + (first ? (stereo ? 9 : 5) : 0) + (stereo ? 2 : 0) + 1;
      const chans = [];
      for (let c = 0; c < this.channels; c++) {
        const sf = new Int32Array(UNITS);
        for (let u = 0; u < units; u++) sf[u] = rows[c].sfs[f * UNITS + u];
        const maskSlice = new Float64Array(UNITS);
        for (let u = 0; u < UNITS; u++) maskSlice[u] = 1.0 / rows[c].masks[f * UNITS + u];
        const x = rows[c].spectra.subarray(f * N, (f + 1) * N);
        const cf = new ChannelFrame(x, sf, units, maskSlice);
        cf.coding = chooseSfCoding(
          sf.subarray(0, units),
          c,
          first,
          prevSf[c],
          units,
          c ? chans[0].sf.subarray(0, units) : null,
        );
        fixed += cf.coding[0];
        chans.push(cf);
      }
      prevSf = chans.map((cf) => cf.sf);
      const opts = this._frameOptions(chans, cands, fixed, units, coarse);
      frames.push({ chans, ...opts });
    }
    const choice = this._allocate(frames);
    const parts = [];
    let len = 0;
    for (let f = 0; f < FRAMES_PER_SUPERFRAME; f++) {
      const data = this._writeFrame(frames[f], choice[f], f === 0, band, units, cands);
      parts.push(data);
      len += data.length;
    }
    if (len > this.superframeBytes) throw new Error('internal error: superframe overflow');
    const data = new Uint8Array(this.superframeBytes).fill(0x01); // Sony rejects any other tail
    let pos = 0;
    for (const p of parts) {
      data.set(p, pos);
      pos += p.length;
    }
    return data;
  }

  _frameOptions(chans, cands, fixedBits, units, coarse) {
    const kmax = Math.min(15, units);
    const nCand = cands.count;
    const nk = kmax + 1;
    // Scratch buffers reused across frames to keep the GC quiet.
    const bits = getScratch(nCand * nk, 0);
    const dist = getScratch(nCand * nk, 1);
    const dc = getScratch(kmax, 2);
    const dn = getScratch(kmax, 3);
    const candStride = coarse ? 2 : 1;
    const kStep = coarse ? 2 : 1;
    for (let ci = 0; ci < nCand; ci++) {
      const mode = cands.mode[ci];
      const m0 = cands.mode0[ci];
      const pOfD = P_OF_D[mode];
      const p1OfD = P1_OF_D[mode];
      const rowBase = ci * nk;
      if (ci % candStride !== 0) {
        for (let k = 0; k < nk; k++) {
          bits[rowBase + k] = INF;
          dist[rowBase + k] = INF;
        }
        continue;
      }
      dc.fill(0);
      dn.fill(0);
      let b0 = 0.0;
      let d0 = 0.0;
      for (let c = 0; c < chans.length; c++) {
        const cf = chans[c];
        const cost = cf.cost;
        const noise = cf.noise;
        for (let u = 0; u < units; u++) {
          const base = m0 ? cf.sf[u] : cf.sf[u] + cf.mask[u];
          let d = base - cands.grad[ci * units + u];
          if (d < D_MIN) d = D_MIN;
          else if (d > D_MAX) d = D_MAX;
          const di = d + 40;
          const p0 = pOfD[di];
          let cv = INF;
          let nv = INF;
          if (p0 >= 1 && p0 <= 15) {
            cv = cost[(p0 - 1) * units + u];
            nv = noise[(p0 - 1) * units + u];
          }
          b0 += cv;
          d0 += nv;
          if (u < kmax) {
            const p1 = p1OfD[di];
            let cv1 = INF;
            let nv1 = INF;
            if (p1 >= 1 && p1 <= 15) {
              cv1 = cost[(p1 - 1) * units + u];
              nv1 = noise[(p1 - 1) * units + u];
            }
            dc[u] += cv1 - cv;
            dn[u] += nv1 - nv;
          }
        }
      }
      bits[rowBase] = b0 + fixedBits + cands.headerBits[ci];
      dist[rowBase] = d0;
      let bAcc = b0 + fixedBits + cands.headerBits[ci];
      let dAcc = d0;
      for (let k = 1; k < nk; k++) {
        bAcc += dc[k - 1];
        dAcc += dn[k - 1];
        if (k % kStep !== 0) {
          bits[rowBase + k] = INF;
          dist[rowBase + k] = INF;
          continue;
        }
        bits[rowBase + k] = bAcc;
        dist[rowBase + k] = dAcc;
      }
    }
    const cap = this.superframeBytes;
    // Keep entries that fit, then group by exact frame size and take the lowest
    // weighted noise in each group (a counting sort avoids allocating objects).
    const counts = new Int32Array(cap + 1);
    let m = 0;
    for (let i = 0; i < nCand * nk; i++) {
      const nb = Math.ceil(bits[i] / 8.0);
      if (nb <= cap) {
        counts[nb]++;
        m++;
      }
    }
    if (m === 0) throw new Overflow();
    const start = new Int32Array(cap + 2);
    let acc = 0;
    for (let s = 0; s <= cap; s++) {
      start[s] = acc;
      acc += counts[s];
    }
    start[cap + 1] = acc;
    const fill = start.slice();
    const order = new Int32Array(m);
    for (let i = 0; i < nCand * nk; i++) {
      const nb = Math.ceil(bits[i] / 8.0);
      if (nb <= cap) order[fill[nb]++] = i;
    }
    const sizes = [];
    const dists = [];
    const flats = [];
    let minSoFar = Infinity;
    for (let s = 0; s <= cap; s++) {
      const a = start[s];
      const b = start[s + 1];
      if (a === b) continue;
      let bestD = dist[order[a]];
      let bestI = order[a];
      for (let j = a + 1; j < b; j++) {
        const dd = dist[order[j]];
        if (dd < bestD) {
          bestD = dd;
          bestI = order[j];
        }
      }
      if (bestD < minSoFar) {
        sizes.push(s);
        dists.push(bestD);
        flats.push(bestI);
        minSoFar = bestD;
      }
    }
    return {
      sizes: Int32Array.from(sizes),
      dists: Float64Array.from(dists),
      flats: Int32Array.from(flats),
      kmax,
    };
  }

  _allocate(frames) {
    const cap = this.superframeBytes;
    let dp = new Float64Array(cap + 1).fill(INF);
    dp[0] = 0.0;
    const back = [];
    for (const frame of frames) {
      const sizes = frame.sizes;
      const dists = frame.dists;
      const next = new Float64Array(cap + 1).fill(INF);
      const arg = new Int32Array(cap + 1).fill(-1);
      for (let i = 0; i < sizes.length; i++) {
        const size = sizes[i];
        const d = dists[i];
        const limit = cap - size;
        for (let pos = 0; pos <= limit; pos++) {
          const prev = dp[pos];
          if (prev === INF) continue;
          const c = prev + d;
          if (c < next[pos + size]) {
            next[pos + size] = c;
            arg[pos + size] = i;
          }
        }
      }
      back.push(arg);
      dp = next;
    }
    let any = false;
    for (let i = 0; i <= cap; i++) if (dp[i] !== INF) any = true;
    if (!any) throw new Overflow();
    let pos = 0;
    let best = INF;
    for (let i = 0; i <= cap; i++) {
      if (dp[i] < best) {
        best = dp[i];
        pos = i;
      }
    }
    const choice = new Array(frames.length).fill(0);
    for (let f = frames.length - 1; f >= 0; f--) {
      const idx = back[f][pos];
      choice[f] = idx;
      pos -= frames[f].sizes[idx];
    }
    return choice;
  }

  _writeFrame(frame, index, first, band, units, cands) {
    const { chans, sizes, flats, kmax } = frame;
    const flat = flats[index];
    const cand = Math.floor(flat / (kmax + 1));
    const boundary = flat % (kmax + 1);
    const [mode, startUnit, endUnit, startValue, endValue] = cands.params[cand];
    const out = new Bits();
    out.put(first ? 0 : 1, 1);
    out.put(first ? 0 : 1, 1);
    if (first) {
      out.put(band - 3, 4);
      if (this.channels === 2) out.put(band - 3, 4);
      out.put(0, 1);
    }
    out.put(mode, 2);
    if (mode) {
      out.put(startUnit, 5);
      out.put(startValue, 5);
    } else {
      out.put(startUnit, 6);
      out.put(endUnit - 1, 6);
      out.put(startValue, 5);
      out.put(endValue, 5);
    }
    out.put(boundary, 4);
    if (this.channels === 2) {
      out.put(0, 1); // primary channel 0
      out.put(0, 1); // no intensity-stereo signs
    }
    out.put(0, 1); // no band-extension data
    const grad = gradientCurve(mode, startUnit, endUnit, startValue, endValue, units);
    for (const cf of chans) {
      const { coarse, fine } = precisions(cf.sf, grad, mode, boundary, units);
      for (let u = 0; u < units; u++) {
        if (fine[u]) throw new Error('internal error: fine precision selected');
      }
      writeSf(out, cf.sf.subarray(0, units), cf.coding);
      for (let u = 0; u < units; u++) {
        const p = coarse[u];
        const wl = p + 1;
        if (wl <= 7) {
          const book = BOOKS[wl];
          const g0 = book.groupStart[u];
          const g1 = book.groupStart[u + 1];
          const syms = cf.sym[p];
          const s = cf.sets[u];
          for (let g = g0; g < g1; g++) {
            const len = book.bitAt(s, CBI[u], syms[g]);
            if (!len) throw new Error('internal error: invalid spectrum symbol');
            out.put(book.codeAt(s, CBI[u], syms[g]), len);
          }
        } else {
          const row = (p - 1) * N;
          for (let i = QIDX[u]; i < QIDX[u + 1]; i++) out.put(cf.q[row + i], wl);
        }
      }
    }
    const data = out.toBytes();
    if (data.length !== sizes[index]) {
      throw new Error('internal error: frame size mismatch (' + data.length + ' != ' + sizes[index] + ')');
    }
    return data;
  }
}
