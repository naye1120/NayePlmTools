import test from 'node:test'
import assert from 'node:assert/strict'
import { createAutoloadEditorSession } from '../src/utils/autoloadEditorSession.js'
import { createRequestScope } from '../src/utils/requestScope.js'

const tick = () => new Promise(resolve => setImmediate(resolve))
function fixture() {
  const session = createAutoloadEditorSession(), calls = [], exits = []
  session.setSave(value => new Promise((resolve, reject) => calls.push({ value, resolve, reject })))
  session.initialize({ AUTOLOAD_ENABLED: true, AUTOLOAD_LIST: 'a,b,c' })
  return { session, calls, exits, flush: () => session.flush(body => exits.push(JSON.parse(body))) }
}
test('unloaded settings and refreshes cannot save defaults or replace the editor draft', () => {
  const session = createAutoloadEditorSession()
  session.setSave(() => assert.fail('unexpected save'))
  for (const config of [{}, { AUTOLOAD_ENABLED: true }, { AUTOLOAD_ENABLED: 1, AUTOLOAD_LIST: '' }]) session.initialize(config)
  session.edit({ enabled: true }); session.flush(() => assert.fail('unexpected exit save'))
  assert.equal(session.getSnapshot().initialized, false)
  session.initialize({ AUTOLOAD_ENABLED: 'false', AUTOLOAD_LIST: 'a,b' })
  session.initialize({ AUTOLOAD_ENABLED: true, AUTOLOAD_LIST: '' })
  assert.deepEqual(session.getSnapshot().list, ['a', 'b'])
  assert.equal(session.getSnapshot().enabled, false)
})
test('rapid edits serialize requests and coalesce the newest complete draft', async () => {
  const { session, calls } = fixture()
  session.edit({ list: ['b', 'a', 'c'] })
  session.edit(state => ({ list: state.list.filter(x => x !== 'c') }))
  session.edit(state => ({ list: [...state.list, '!3000', 'd'] }))
  assert.equal(calls.length, 1)
  calls[0].resolve(true); await tick()
  assert.equal(calls.length, 2)
  assert.deepEqual(calls[1].value, { AUTOLOAD_ENABLED: true, AUTOLOAD_LIST: 'b,a,!3000,d' })
  calls[1].resolve(true); await tick()
  assert.equal(session.getSnapshot().saving, false)
})
test('returning to the previously saved state still restores it after an older in-flight write', async () => {
  const { session, calls } = fixture()
  session.edit({ list: ['b'] }); session.edit({ list: ['a', 'b', 'c'] })
  calls[0].resolve(true); await tick()
  assert.equal(calls[1].value.AUTOLOAD_LIST, 'a,b,c')
  calls[1].resolve(true); await tick()
})
test('a failed multi-field write cannot make a reverted draft look already saved', async () => {
  const { session, calls } = fixture()
  session.edit({ list: ['b'] }); session.edit({ list: ['a', 'b', 'c'] })
  calls[0].resolve(false); await tick()
  assert.equal(calls[1].value.AUTOLOAD_LIST, 'a,b,c')
  calls[1].resolve(true); await tick()
})
test('failed obsolete saves still drain new edits; latest failure/throw requires explicit retry', async () => {
  const { session, calls } = fixture()
  session.edit({ list: ['b'] }); session.edit({ list: ['c'] })
  calls[0].resolve(false); await tick()
  assert.equal(calls[1].value.AUTOLOAD_LIST, 'c')
  calls[1].reject(new Error('transport')); await tick()
  assert.equal(session.getSnapshot().error, true)
  assert.equal(calls.length, 2)
  session.retry(); assert.equal(calls.length, 3)
  calls[2].resolve(true); await tick()
  assert.equal(session.getSnapshot().error, false)
})
test('exit saves use the latest draft without acknowledging it or losing queued work', async () => {
  const { session, calls, exits, flush } = fixture()
  flush(); assert.equal(exits.length, 0)
  session.edit({ list: ['b'] }); session.edit({ list: ['c'] })
  flush(); flush(); assert.deepEqual(exits, [{ AUTOLOAD_ENABLED: true, AUTOLOAD_LIST: 'c' }])
  calls[0].resolve(true); await tick()
  assert.equal(calls[1].value.AUTOLOAD_LIST, 'c')
  calls[1].resolve(true); await tick(); flush()
  assert.equal(exits.length, 1)
})
test('unsubscribe does not cancel persistence; failed exit queueing remains retryable', async () => {
  const { session, calls } = fixture()
  let notifications = 0
  const unsubscribe = session.subscribe(() => notifications++)
  session.edit({ list: ['x'] }); unsubscribe(); const before = notifications
  session.flush(() => { throw new Error('cannot queue') })
  let exit
  session.flush(body => { exit = JSON.parse(body) })
  assert.equal(exit.AUTOLOAD_LIST, 'x')
  calls[0].resolve(true); await tick()
  assert.equal(notifications, before)
})
test('request scope cancels native transport or guards stale callbacks on older browsers', () => {
  const native = createRequestScope(); native.abort()
  assert.equal(native.aborted, true); assert.equal(native.signal.aborted, true)
  const fallback = createRequestScope(null)
  assert.equal(fallback.signal, undefined); assert.equal(fallback.aborted, false)
  fallback.abort(); fallback.abort(); assert.equal(fallback.aborted, true)
})
