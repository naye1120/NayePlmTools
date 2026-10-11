// SPDX-License-Identifier: GPL-3.0-or-later
//
// ATRAC9 audio preparation for the browser encoder.
//
// Ported from blackbearreloaded/ps5-at9-converter (GPL-3.0-or-later),
// ps5at9/dsp.py. Covers channel mapping, windowed-sinc resampling, BS.1770
// integrated loudness, true-peak measurement, true-peak limiting and fades.
//
// Zero dependencies: no npm packages, no Node APIs, no DOM. Everything works
// on plain Float64Array channel buffers.
//
// Faithfulness notes:
//  * The Python loudness path K-weights the signal by convolving it with the
//    truncated (-200 dB) impulse response of the two BS.1770 biquads. Convolving
//    with a biquad's impulse response is identical to running that biquad, so
//    this port just runs the two biquads directly (shelf then high-pass). That
//    is algebraically the same filter and avoids needing an FFT.
//  * The Kaiser window uses the modified Bessel function I0, evaluated by its
//    power series (the Python code uses numpy.i0). beta stays below ~11 here, so
//    the series converges in a few dozen terms.

const PI = Math.PI;

/** Channels of `channelData` as Float64Array. Accepts Float32Array input too. */
function toFloat64(channels) {
  return channels.map((c) => (c instanceof Float64Array ? c : Float64Array.from(c)));
}

// -- channels ------------------------------------------------------------------

/**
 * Map mono or multichannel audio to stereo (ITU-R BS.775 downmix for 5.1).
 * `channels` is an array of per-channel Float arrays; returns [left, right].
 */
export function toStereo(channelData) {
  const pcm = toFloat64(channelData);
  const ch = pcm.length;
  if (ch === 2) return pcm;
  if (ch === 1) return [pcm[0].slice(), pcm[0].slice()];
  if (ch === 6) {
    const k = 1.0 / Math.sqrt(2.0);
    const n = pcm[0].length;
    const left = new Float64Array(n);
    const right = new Float64Array(n);
    const denom = 1.0 + 2.0 * k;
    for (let i = 0; i < n; i++) {
      left[i] = (pcm[0][i] + k * pcm[2][i] + k * pcm[4][i]) / denom;
      right[i] = (pcm[1][i] + k * pcm[2][i] + k * pcm[5][i]) / denom;
    }
    return [left, right];
  }
  return [pcm[0], pcm[1]];
}

// -- resampling ----------------------------------------------------------------

/** Modified Bessel function I0 by power series; matches numpy.i0 to ~1e-15. */
function besselI0(x) {
  let sum = 1.0;
  let term = 1.0;
  const y = (x * x) / 4.0;
  for (let k = 1; k < 1000; k++) {
    term *= y / (k * k);
    sum += term;
    if (term <= sum * 1e-16) break;
  }
  return sum;
}

/** np.sinc(x) = sin(pi x) / (pi x), with sinc(0) = 1. */
function sinc(x) {
  if (x === 0) return 1.0;
  const a = PI * x;
  return Math.sin(a) / a;
}

function gcd(a, b) {
  a = Math.abs(a);
  b = Math.abs(b);
  while (b) {
    const t = a % b;
    a = b;
    b = t;
  }
  return a;
}

/**
 * Polyphase table h[phase][tap] and half width for rate_in*up/down conversion.
 * Direct port of dsp._kaiser_lowpass_phases (same Kaiser design, same tap count).
 */
function kaiserLowpassPhases(up, rateIn, passHz, stopHz, attenDb = 100.0) {
  const beta = 0.1102 * (attenDb - 8.7);
  const width = (stopHz - passHz) / rateIn; // normalised to input rate
  const taps = Math.ceil((attenDb - 8.0) / (2.285 * 2.0 * PI * width));
  const half = Math.max(8, Math.floor(taps / 2) + 1);
  const cutoff = (passHz + stopHz) / 2.0 / rateIn; // cycles per input sample
  const width2 = 2 * half; // taps per phase: k = -half+1 .. half
  const h = [];
  const i0beta = besselI0(beta);
  for (let p = 0; p < up; p++) {
    const phase = p / up;
    const row = new Float64Array(width2);
    let sum = 0.0;
    for (let j = 0; j < width2; j++) {
      const k = -half + 1 + j;
      const t = k - phase; // input sample offset from the output time
      let v = 2.0 * cutoff * sinc(2.0 * cutoff * t);
      const r = t / half;
      const arg = Math.sqrt(Math.max(0.0, Math.min(1.0, 1.0 - r * r)));
      v *= besselI0(beta * arg) / i0beta;
      row[j] = v;
      sum += v;
    }
    for (let j = 0; j < width2; j++) row[j] /= sum;
    h.push(row);
  }
  return { h, half };
}

/**
 * High-quality windowed-sinc sample-rate conversion of channelData to rateOut.
 * Returns an array of Float64Array channels, or the input unchanged when the
 * rates already match (as dsp.resample does).
 */
export function resample(channelData, rateIn, rateOut = 48000) {
  if (rateIn === rateOut) return toFloat64(channelData);
  const pcm = toFloat64(channelData);
  const g = gcd(rateIn, rateOut);
  const up = rateOut / g;
  const down = rateIn / g;
  const nyquist = Math.min(rateIn, rateOut) / 2.0;
  const passHz = Math.min(20000.0, 0.9 * nyquist);
  const { h, half } = kaiserLowpassPhases(up, rateIn, passHz, nyquist);
  const width = 2 * half;
  const nIn = pcm[0].length;
  const nOut = Math.ceil((nIn * up) / down);
  const padLen = nIn + 2 * half + 2;
  const channels = pcm.map((src) => {
    const pad = new Float64Array(padLen);
    pad.set(src, half);
    const out = new Float64Array(nOut);
    for (let n = 0; n < nOut; n++) {
      const pos = n * down;
      const base = Math.floor(pos / up);
      const phase = pos % up;
      const coeff = h[phase];
      const start = base + half - half + 1; // base + (-half+1) + half
      let acc = 0.0;
      for (let j = 0; j < width; j++) acc += pad[start + j] * coeff[j];
      out[n] = acc;
    }
    return out;
  });
  return channels;
}

// -- loudness (ITU-R BS.1770-4 / EBU R128) --------------------------------------

const K_SHELF_B = [1.53512485958697, -2.69169618940638, 1.19839281085285];
const K_SHELF_A = [1.0, -1.69065929318241, 0.73248077421585];
const K_HP_B = [1.0, -2.0, 1.0];
const K_HP_A = [1.0, -1.99004745483398, 0.99007225036621];

/** Run one biquad (a0 = 1) over `src`, returning a new buffer. */
function biquad(src, b, a) {
  const n = src.length;
  const out = new Float64Array(n);
  let x1 = 0.0;
  let x2 = 0.0;
  let y1 = 0.0;
  let y2 = 0.0;
  for (let i = 0; i < n; i++) {
    const x0 = src[i];
    const y0 = b[0] * x0 + b[1] * x1 + b[2] * x2 - a[1] * y1 - a[2] * y2;
    out[i] = y0;
    x2 = x1;
    x1 = x0;
    y2 = y1;
    y1 = y0;
  }
  return out;
}

/** 48 kHz K-weighting: shelf then high-pass, matching dsp._k_weighting. */
function kWeighting(channelData) {
  return channelData.map((c) => biquad(biquad(c, K_SHELF_B, K_SHELF_A), K_HP_B, K_HP_A));
}

/** Integrated loudness in LUFS (full scale = 1.0). rate must be 48000. */
export function integratedLoudness(channelData, rate = 48000) {
  if (rate !== 48000) throw new Error('loudness is measured at 48 kHz');
  const weighted = kWeighting(toFloat64(channelData));
  const block = Math.trunc(0.4 * rate);
  const step = Math.trunc(0.1 * rate);
  const n = weighted[0].length;
  if (n < block) {
    let power = 0.0;
    for (const c of weighted) {
      let acc = 0.0;
      for (let i = 0; i < n; i++) acc += c[i] * c[i];
      power += acc / n;
    }
    return power > 0 ? -0.691 + 10.0 * Math.log10(power) : -Infinity;
  }
  // Per-channel cumulative sums of the squared weighted signal.
  const csums = weighted.map((c) => {
    const cs = new Float64Array(n + 1);
    let acc = 0.0;
    for (let i = 0; i < n; i++) {
      acc += c[i] * c[i];
      cs[i + 1] = acc;
    }
    return cs;
  });
  const count = Math.floor((n - block) / step) + 1;
  const z = new Float64Array(count);
  for (let s = 0; s < count; s++) {
    const start = s * step;
    let acc = 0.0;
    for (const cs of csums) acc += (cs[start + block] - cs[start]) / block;
    z[s] = acc;
  }
  const gated = [];
  for (let s = 0; s < count; s++) {
    const lk = -0.691 + 10.0 * Math.log10(z[s]);
    if (lk > -70.0) gated.push(z[s]);
  }
  if (gated.length === 0) return -Infinity;
  let mean = 0.0;
  for (const v of gated) mean += v;
  mean /= gated.length;
  const relative = -0.691 + 10.0 * Math.log10(mean) - 10.0;
  let sum = 0.0;
  let kept = 0;
  for (let s = 0; s < count; s++) {
    const lk = -0.691 + 10.0 * Math.log10(z[s]);
    if (lk > -70.0 && lk > relative) {
      sum += z[s];
      kept++;
    }
  }
  return -0.691 + 10.0 * Math.log10(sum / kept);
}

// -- true peak -----------------------------------------------------------------

let _over4 = null;

/** 4x oversampling table for the true-peak meter (dsp._oversample4). */
function oversample4Table() {
  if (_over4 === null) _over4 = kaiserLowpassPhases(4, 48000, 18000.0, 24000.0, 60.0);
  return _over4;
}

/** Per-sample true peak: max |x| over 4x oversampled points, max over channels. */
export function truePeakEnvelope(channelData) {
  const pcm = toFloat64(channelData);
  const { h, half } = oversample4Table();
  const width = 2 * half;
  let env = null;
  for (const src of pcm) {
    const n = src.length;
    const pad = new Float64Array(n + 2 * half + 2);
    pad.set(src, half);
    const e = new Float64Array(n);
    for (let i = 0; i < n; i++) {
      const start = i + 1; // i + (-half+1) + half
      let best = 0.0;
      for (let p = 0; p < 4; p++) {
        const coeff = h[p];
        let acc = 0.0;
        for (let j = 0; j < width; j++) acc += pad[start + j] * coeff[j];
        const a = Math.abs(acc);
        if (a > best) best = a;
      }
      e[i] = best;
    }
    if (env === null) {
      env = e;
    } else {
      for (let i = 0; i < n; i++) if (e[i] > env[i]) env[i] = e[i];
    }
  }
  return env;
}

/** True-peak level in dBTP of the whole signal. */
export function truePeakDb(channelData) {
  const pcm = toFloat64(channelData);
  if (pcm.length === 0 || pcm[0].length === 0) return 0.0;
  const env = truePeakEnvelope(pcm);
  let peak = 0.0;
  for (let i = 0; i < env.length; i++) if (env[i] > peak) peak = env[i];
  return peak > 0 ? 20.0 * Math.log10(peak) : -Infinity;
}

/**
 * Minimum over the centred window [n - width, n + width], in O(n) using the
 * block prefix/suffix decomposition of dsp._sliding_min (block size = window).
 */
function slidingMin(x, width) {
  if (width <= 0) return x.slice();
  const w = 2 * width + 1;
  const n = x.length;
  const padLen = n + 2 * width + w;
  const pad = new Float64Array(padLen);
  for (let i = 0; i < width; i++) pad[i] = x[0];
  pad.set(x, width);
  for (let i = width + n; i < padLen; i++) pad[i] = x[n - 1];
  const blocks = Math.ceil(padLen / w);
  const total = blocks * w;
  const padded = new Float64Array(total);
  padded.set(pad);
  for (let i = padLen; i < total; i++) padded[i] = x[n - 1];
  const prefix = new Float64Array(total);
  const suffix = new Float64Array(total);
  for (let b = 0; b < blocks; b++) {
    const base = b * w;
    let acc = Infinity;
    for (let j = 0; j < w; j++) {
      if (padded[base + j] < acc) acc = padded[base + j];
      prefix[base + j] = acc;
    }
    acc = Infinity;
    for (let j = w - 1; j >= 0; j--) {
      if (padded[base + j] < acc) acc = padded[base + j];
      suffix[base + j] = acc;
    }
  }
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    const a = suffix[i];
    const b = prefix[i + w - 1];
    out[i] = a < b ? a : b;
  }
  return out;
}

/** Mean over the centred window [n - width, n + width] (dsp._moving_average). */
function movingAverage(x, width) {
  if (width <= 0) return x.slice();
  const n = x.length;
  const w = 2 * width + 1;
  const pad = new Float64Array(n + 2 * width);
  for (let i = 0; i < width; i++) pad[i] = x[0];
  pad.set(x, width);
  for (let i = width + n; i < n + 2 * width; i++) pad[i] = x[n - 1];
  const c = new Float64Array(n + 2 * width + 1);
  let acc = 0.0;
  for (let i = 0; i < n + 2 * width; i++) {
    acc += pad[i];
    c[i + 1] = acc;
  }
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) out[i] = (c[i + w] - c[i]) / w;
  return out;
}

/**
 * Smooth look-ahead gain reduction so the true peak stays at or below ceilingDb.
 * Returns { channels, limited }. Faithful port of dsp.limit_true_peak.
 */
export function limitTruePeak(channelData, ceilingDb, rate = 48000, attack = 0.002, release = 0.05) {
  let pcm = toFloat64(channelData).map((c) => c.slice());
  let env = truePeakEnvelope(pcm);
  let envMax = 0.0;
  for (let i = 0; i < env.length; i++) if (env[i] > envMax) envMax = env[i];
  const ceiling = 10.0 ** (ceilingDb / 20.0);
  if (envMax <= ceiling) return { channels: pcm, limited: false };
  let target = ceilingDb;
  const a = Math.max(1, Math.trunc(attack * rate));
  const r = Math.max(1, Math.trunc(release * rate));
  for (let iter = 0; iter < 8; iter++) {
    const need = new Float64Array(env.length);
    const gainLimit = 10.0 ** (target / 20.0);
    for (let i = 0; i < env.length; i++) {
      need[i] = Math.min(1.0, gainLimit / Math.max(env[i], 1e-12));
    }
    let gain = movingAverage(slidingMin(need, a), Math.floor(a / 2));
    gain = movingAverage(slidingMin(gain, r), Math.floor(r / 2));
    pcm = pcm.map((c) => {
      const out = new Float64Array(c.length);
      for (let i = 0; i < c.length; i++) out[i] = c[i] * gain[i];
      return out;
    });
    env = truePeakEnvelope(pcm);
    envMax = 0.0;
    for (let i = 0; i < env.length; i++) if (env[i] > envMax) envMax = env[i];
    if (envMax <= ceiling) break;
    target -= 0.05;
  }
  return { channels: pcm, limited: true };
}

// -- fades ---------------------------------------------------------------------

/** Apply raised-cosine fade-in / fade-out in place-free fashion (dsp.apply_fades). */
export function applyFades(channelData, rate, fadeIn, fadeOut) {
  const pcm = toFloat64(channelData).map((c) => c.slice());
  const n = pcm[0].length;
  const fi = Math.min(n, Math.round(fadeIn * rate));
  const fo = Math.min(n, Math.round(fadeOut * rate));
  if (fi > 0) {
    for (const c of pcm) {
      for (let i = 0; i < fi; i++) c[i] *= 0.5 - 0.5 * Math.cos((PI * (i + 0.5)) / fi);
    }
  }
  if (fo > 0) {
    for (const c of pcm) {
      for (let i = 0; i < fo; i++) {
        c[n - fo + i] *= 0.5 + 0.5 * Math.cos((PI * (i + 0.5)) / fo);
      }
    }
  }
  return pcm;
}
