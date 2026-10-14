import test from 'node:test'
import assert from 'node:assert/strict'
import { autoloadPhase, autoloadItemProgress, autoloadDisplay, autoloadSequenceItems, autoloadDelayDisplay } from '../src/utils/autoloadProgress.js'

test('autoload phases distinguish waiting, execution delays, done and aborted', () => {
  for (const state of ['waiting', 'countdown', 'executing', 'done', 'aborted', 'failed', 'disabled']) {
    assert.equal(autoloadPhase({ state, remaining: 0, current: '' }), state)
  }
  assert.equal(autoloadPhase({ remaining: 0, current: 'DONE' }), 'done')
  assert.equal(autoloadPhase({ remaining: 0, current: 'local/a' }), 'executing')
})

test('sequence indices include consecutive delays while payload outcomes exclude them', () => {
  const status = { state: 'executing', list: 'a,!3000,!1500,b,c', current: 'DELAY', entry_index: 2, done: 1, outcomes: 'fpp' }
  const entries = autoloadSequenceItems(status)
  assert.deepEqual(entries.map(entry => entry.active), [false, false, true, false, false])
  assert.deepEqual(entries.map(entry => entry.processed), [true, true, false, false, false])
  assert.equal(entries[0].failed, true)
  const launching = autoloadSequenceItems({ ...status, current: 'b', entry_index: 3 })
  assert.equal(launching[3].active, true)
  assert.equal(launching[2].processed, true)
  const aborted = autoloadSequenceItems({ ...status, state: 'aborted' })
  assert.ok(aborted.every(entry => !entry.active))
  assert.equal(aborted[2].processed, false)
  assert.equal(aborted[4].processed, false)
  const finished = autoloadSequenceItems({ ...status, state: 'done', done: 3, outcomes: 'fks' })
  assert.ok(finished.every(entry => entry.processed && !entry.active))
  assert.equal(finished[3].skipped, true)
  assert.equal(finished[4].failed, false)
})

test('delay-only and zero delay sequences stay independent of payload totals', () => {
  const entries = autoloadSequenceItems({ state: 'executing', list: '!0,!500', current: 'DELAY', entry_index: 1, total: 0, done: 0 })
  assert.equal(entries[0].processed, true)
  assert.equal(entries[1].active, true)
  assert.equal(autoloadDisplay({ total: 0, done: 0 }, entries.filter(entry => !entry.isDelay).length).total, 0)
  assert.deepEqual(autoloadSequenceItems({ list: null }), [])
  assert.equal(autoloadSequenceItems({ state: 'executing', list: 'a,b', current: 'b', done: 1 })[1].active, true)
})

test('delay fill stays finite, bounded and uses milliseconds', () => {
  assert.deepEqual(autoloadDelayDisplay({ entry_duration_ms: 3000, entry_remaining_ms: 2250 }), { durationMs: 3000, remainingMs: 2250, progress: 0.25 })
  for (const value of [0, -1, NaN, Infinity, '3000', null, undefined]) {
    const display = autoloadDelayDisplay({ entry_duration_ms: value, entry_remaining_ms: value })
    assert.ok(Object.values(display).every(Number.isFinite))
    assert.ok(display.progress >= 0 && display.progress <= 1)
  }
  assert.equal(autoloadDelayDisplay({ entry_duration_ms: 3000, entry_remaining_ms: 5000 }).progress, 0)
})
test('processed failed/skipped entries never appear successful and pending entries remain pending after abort', () => {
  const status = { state: 'aborted', done: 3, current: '', outcomes: 'sfkp' }
  assert.deepEqual(autoloadItemProgress(status, 0), { active: false, processed: true, failed: false, skipped: false })
  assert.deepEqual(autoloadItemProgress(status, 1), { active: false, processed: true, failed: true, skipped: false })
  assert.deepEqual(autoloadItemProgress(status, 2), { active: false, processed: true, failed: false, skipped: true })
  assert.deepEqual(autoloadItemProgress(status, 3), { active: false, processed: false, failed: false, skipped: false })
})
test('sequence delays do not mark the next payload as currently launching', () => {
  assert.equal(autoloadItemProgress({ state: 'executing', current: 'DELAY', done: 1, outcomes: 'sp' }, 1).active, false)
  assert.equal(autoloadItemProgress({ state: 'executing', current: 'local/next', done: 1, outcomes: 'sp' }, 1).active, true)
})
test('countdown/progress stay finite and clamped for zero, negative and malformed numbers', () => {
  assert.deepEqual(autoloadDisplay({ delay: 0, remaining: 0, total: 0, done: 0 }),
    { total: 0, done: 0, progress: 0, delayMs: 0, remainingMs: 0 })
  for (const value of [NaN, Infinity, -Infinity, -1, '3', null]) {
    const display = autoloadDisplay({ delay: value, remaining_ms: value, remaining: value, total: value, done: value }, 3)
    assert.equal(display.total, 3)
    assert.equal(display.done, 0)
    for (const number of Object.values(display)) assert.ok(Number.isFinite(number) && number >= 0)
  }
  assert.equal(autoloadDisplay({ delay: 5, remaining_ms: 9000, total: 3, done: 99 }).progress, 1)
  assert.equal(autoloadDisplay({ delay: 5, remaining_ms: 9000 }).remainingMs, 5000)
  assert.equal(autoloadDisplay({ remaining: 2 }).remainingMs, 2000)
  assert.equal(autoloadPhase({ remaining: Infinity }), 'disabled')
})
