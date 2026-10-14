import test from 'node:test';
import assert from 'node:assert/strict';
import { compareVersions, slug } from '../src/utils/versions.js';

test('slug correctly formats names into valid slot slugs', () => {
  assert.equal(slug('kstuff-lite'), 'kstuff-lite');
  assert.equal(slug('kstuff_lite'), 'kstuff_lite');
  assert.equal(slug('Kstuff Lite v1.0'), 'kstuff-lite-v1.0');
  assert.equal(slug('ELF Loader! NG'), 'elf-loader-ng');
  assert.equal(slug('...payload...'), 'payload');
  assert.equal(slug('---'), 'payload');
  assert.equal(slug(''), 'payload');
  assert.equal(slug(null), 'payload');
});

test('compareVersions handles basic versions and prefixes', () => {
  assert.equal(compareVersions('1.0', '1.0'), 0);
  assert.equal(compareVersions('v1.0', '1.0'), 0);
  assert.equal(compareVersions('V1.0.0', '1.0.0'), 0);
  assert.equal(compareVersions('1.2', '1.1'), 1);
  assert.equal(compareVersions('1.1', '1.2'), -1);
});

test('compareVersions handles numeric runs vs leading zeros', () => {
  // Digits compare numerically (without leading zeros)
  assert.equal(compareVersions('1.01', '1.1'), 0);
  assert.equal(compareVersions('1.11', '1.2'), 1);
  assert.equal(compareVersions('1.2', '1.11'), -1);
  assert.equal(compareVersions('20240901', '20231231'), 1);
  assert.equal(compareVersions('20231231', '20240901'), -1);
});

test('compareVersions pre-release rules: 1.7 > 1.7beta3', () => {
  // When one version runs out first: a next token of letters means pre-release -> lower
  assert.equal(compareVersions('1.7', '1.7beta3'), 1);
  assert.equal(compareVersions('1.7beta3', '1.7'), -1);
  assert.equal(compareVersions('1.0', '1.0-alpha'), 1);
  assert.equal(compareVersions('1.0-alpha', '1.0'), -1);
  assert.equal(compareVersions('1.7beta3', '1.7beta4'), -1);
  assert.equal(compareVersions('1.7beta4', '1.7beta3'), 1);
  assert.equal(compareVersions('1.7rc1', '1.7beta2'), 1);
});

test('compareVersions sub-version rules: 1.0.1 > 1.0', () => {
  // When one version runs out first: digits mean higher
  assert.equal(compareVersions('1.0.1', '1.0'), 1);
  assert.equal(compareVersions('1.0', '1.0.1'), -1);
  assert.equal(compareVersions('1.0.0.1', '1.0.0'), 1);
});

test('compareVersions digit > letter at the same position', () => {
  assert.equal(compareVersions('1.1', '1.a'), 1);
  assert.equal(compareVersions('1.a', '1.1'), -1);
});

test('compareVersions tie-breaker on equal versions with last_update', () => {
  assert.equal(compareVersions('1.0', '1.0', '2026-10-11', '2026-09-01'), 1);
  assert.equal(compareVersions('1.0', '1.0', '2026-09-01', '2026-10-11'), -1);
  assert.equal(compareVersions('1.0', '1.0', '2026-10-11', '2026-10-11'), 0);
  assert.equal(compareVersions('1.0', '1.0', null, null), 0);
});

test('compareVersions handles null/empty/undefined safely', () => {
  assert.equal(compareVersions(null, null), 0);
  assert.equal(compareVersions('', ''), 0);
  assert.equal(compareVersions('1.0', null), 1);
  assert.equal(compareVersions(null, '1.0'), -1);
  assert.equal(compareVersions('1.0', ''), 1);
  assert.equal(compareVersions('', '1.0'), -1);
});
