import test from 'node:test'
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { formatSyncTime } from '../src/utils/syncTime.js'

test('console sync time uses local fields even when locale formatting is broken', () => {
  for (const [zone, expected] of [
    ['Europe/Warsaw', ['2026-10-13 14:00:00', '2026-01-13 13:00:00']],
    ['America/Los_Angeles', ['2026-10-13 05:00:00', '2026-01-13 04:00:00']],
    ['Asia/Kathmandu', ['2026-10-13 17:45:00', '2026-01-13 17:45:00']],
  ]) {
    const code = `import { formatSyncTime } from './src/utils/syncTime.js';
      Date.prototype.toLocaleString = function () { throw Error('Broken console formatter'); };
      console.log(JSON.stringify(['2026-10-13T12:00:00Z','2026-01-13T12:00:00Z'].map(iso => formatSyncTime(Date.parse(iso)/1000,'en-US',true))));`
    const actual = execFileSync(process.execPath, ['--input-type=module', '-e', code], {
      cwd: new URL('../', import.meta.url), env: { ...process.env, TZ: zone }, encoding: 'utf8',
    })
    assert.deepEqual(JSON.parse(actual), expected)
  }
})

test('other browsers retain localized local time formatting', () => {
  const timestamp = Date.parse('2026-10-13T12:00:00Z') / 1000
  for (const locale of ['en-US', 'pl-PL']) {
    assert.equal(formatSyncTime(timestamp, locale), new Date(timestamp * 1000).toLocaleString(locale))
  }
})
