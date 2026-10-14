import assert from 'node:assert/strict'
import test from 'node:test'
import {
  getRouteFromHash,
  formatHash,
  getHistoryChain,
  seedHistory,
  writeHistory,
} from '../src/hooks/useHistoryNavigation.js'

test('getRouteFromHash parses root, top-level, and sub-page hashes correctly', () => {
  assert.deepEqual(getRouteFromHash(''), { type: 'dashboard' })
  assert.deepEqual(getRouteFromHash('#'), { type: 'dashboard' })
  assert.deepEqual(getRouteFromHash('#/'), { type: 'dashboard' })
  assert.deepEqual(getRouteFromHash('#/dashboard'), { type: 'dashboard' })
  assert.deepEqual(getRouteFromHash('#/storage'), { type: 'storage' })
  assert.deepEqual(getRouteFromHash('#/autoload'), { type: 'autoload' })
  assert.deepEqual(getRouteFromHash('#/processes'), { type: 'processes' })
  assert.deepEqual(getRouteFromHash('#/settings'), { type: 'settings' })
  assert.deepEqual(getRouteFromHash('#/donate'), { type: 'donate' })
  assert.deepEqual(getRouteFromHash('#/sources'), { type: 'sources' })
  assert.deepEqual(getRouteFromHash('#/move-from-usb'), { type: 'move_from_usb', path: null })
  assert.deepEqual(getRouteFromHash('#/move_from_usb'), { type: 'move_from_usb', path: null })
  assert.deepEqual(getRouteFromHash('#/unknown'), { type: 'dashboard' })
})

test('getRouteFromHash parses payload detail routes with encoding and optional src query', () => {
  assert.deepEqual(
    getRouteFromHash('#/payload/ps5-kstuff.elf'),
    { type: 'payload', filename: 'ps5-kstuff.elf', srcId: null }
  )
  assert.deepEqual(
    getRouteFromHash('#/payload/etaHEN_v1.8b.bin?src=my-custom-repo'),
    { type: 'payload', filename: 'etaHEN_v1.8b.bin', srcId: 'my-custom-repo' }
  )
  assert.deepEqual(
    getRouteFromHash('#/payload/test%20payload%2Bv2.elf'),
    { type: 'payload', filename: 'test payload+v2.elf', srcId: null }
  )
  assert.deepEqual(
    getRouteFromHash('#/storage/payload/ps5-kstuff.elf'),
    { type: 'payload', filename: 'ps5-kstuff.elf', srcId: null }
  )
})

test('getRouteFromHash parses move-from-usb with query path or subpath', () => {
  assert.deepEqual(
    getRouteFromHash('#/move-from-usb?path=%2Fmnt%2Fusb0%2Fpayload.elf'),
    { type: 'move_from_usb', path: '/mnt/usb0/payload.elf' }
  )
  assert.deepEqual(
    getRouteFromHash('#/move-from-usb/%2Fmnt%2Fusb0%2Fpayload.elf'),
    { type: 'move_from_usb', path: '/mnt/usb0/payload.elf' }
  )
})

test('formatHash generates clean hash URLs for all route types', () => {
  assert.equal(formatHash({ type: 'dashboard' }), '#/')
  assert.equal(formatHash(null), '#/')
  assert.equal(formatHash({ type: 'storage' }), '#/storage')
  assert.equal(formatHash({ type: 'autoload' }), '#/autoload')
  assert.equal(formatHash({ type: 'processes' }), '#/processes')
  assert.equal(formatHash({ type: 'settings' }), '#/settings')
  assert.equal(formatHash({ type: 'donate' }), '#/donate')
  assert.equal(formatHash({ type: 'sources' }), '#/sources')
  assert.equal(formatHash({ type: 'move_from_usb' }), '#/move-from-usb')
  assert.equal(
    formatHash({ type: 'move_from_usb', path: '/mnt/usb0/payload.elf' }),
    '#/move-from-usb?path=%2Fmnt%2Fusb0%2Fpayload.elf'
  )
  assert.equal(
    formatHash({ type: 'payload', filename: 'ps5-kstuff.elf' }),
    '#/payload/ps5-kstuff.elf'
  )
  assert.equal(
    formatHash({ type: 'payload', filename: 'ps5-kstuff.elf', srcId: 'extra-repo' }),
    '#/payload/ps5-kstuff.elf?src=extra-repo'
  )
})

test('getHistoryChain builds correct hierarchical navigation chains', () => {
  assert.deepEqual(getHistoryChain({ type: 'dashboard' }), [{ type: 'dashboard' }])
  assert.deepEqual(getHistoryChain({ type: 'storage' }), [
    { type: 'dashboard' },
    { type: 'storage' },
  ])
  assert.deepEqual(getHistoryChain({ type: 'payload', filename: 'ps5-kstuff.elf' }), [
    { type: 'dashboard' },
    { type: 'storage' },
    { type: 'payload', filename: 'ps5-kstuff.elf' },
  ])
  assert.deepEqual(getHistoryChain({ type: 'settings' }), [
    { type: 'dashboard' },
    { type: 'settings' },
  ])
  assert.deepEqual(getHistoryChain({ type: 'sources' }), [
    { type: 'dashboard' },
    { type: 'settings' },
    { type: 'sources' },
  ])
  assert.deepEqual(getHistoryChain({ type: 'move_from_usb', path: '/mnt/usb0/test.elf' }), [
    { type: 'dashboard' },
    { type: 'storage' },
    { type: 'move_from_usb', path: '/mnt/usb0/test.elf' },
  ])
  assert.deepEqual(getHistoryChain({ type: 'autoload' }), [
    { type: 'dashboard' },
    { type: 'autoload' },
  ])
  assert.deepEqual(getHistoryChain({ type: 'processes' }), [
    { type: 'dashboard' },
    { type: 'processes' },
  ])
  assert.deepEqual(getHistoryChain({ type: 'donate' }), [
    { type: 'dashboard' },
    { type: 'donate' },
  ])
})

test('seedHistory creates proper baseline stack with replaceState first then pushState', () => {
  const actions = []
  const originalWindow = globalThis.window
  globalThis.window = {
    history: {
      replaceState: (state, title, url) => actions.push({ type: 'replace', state, url }),
      pushState: (state, title, url) => actions.push({ type: 'push', state, url }),
    },
  }

  try {
    const payloadRoute = { type: 'payload', filename: 'ps5-kstuff.elf' }
    seedHistory(payloadRoute)

    assert.equal(actions.length, 3)
    assert.deepEqual(actions[0], { type: 'replace', state: { type: 'dashboard', hash: '#/' }, url: '#/' })
    assert.deepEqual(actions[1], { type: 'push', state: { type: 'storage', hash: '#/storage' }, url: '#/storage' })
    assert.deepEqual(actions[2], { type: 'push', state: { ...payloadRoute, hash: '#/payload/ps5-kstuff.elf' }, url: '#/payload/ps5-kstuff.elf' })
  } finally {
    globalThis.window = originalWindow
  }
})

test('writeHistory pushes when hash changes and is idempotent when hash matches', () => {
  const pushed = []
  const originalWindow = globalThis.window
  globalThis.window = {
    location: { hash: '#/storage' },
    history: {
      pushState: (state, title, url) => pushed.push({ state, title, url }),
    },
  }

  try {
    // 1. Pushing new route with different hash
    const payloadRoute = { type: 'payload', filename: 'ps5-kstuff.elf' }
    writeHistory(payloadRoute, false)
    assert.equal(pushed.length, 1)
    assert.equal(pushed[0].url, '#/payload/ps5-kstuff.elf')
    assert.equal(pushed[0].state.type, 'payload')

    // 2. Calling with matching hash is idempotent
    globalThis.window.location.hash = '#/payload/ps5-kstuff.elf'
    writeHistory(payloadRoute, false)
    assert.equal(pushed.length, 1) // unchanged
  } finally {
    globalThis.window = originalWindow
  }
})

test('writeHistory replaces state when replace is true even if hash matches', () => {
  const replaced = []
  const originalWindow = globalThis.window
  globalThis.window = {
    location: { hash: '#/storage' },
    history: {
      pushState: () => {},
      replaceState: (state, title, url) => replaced.push({ state, title, url }),
    },
  }

  try {
    const storageRoute = { type: 'storage' }
    writeHistory(storageRoute, true)
    assert.equal(replaced.length, 1)
    assert.equal(replaced[0].url, '#/storage')
  } finally {
    globalThis.window = originalWindow
  }
})

test('getRouteFromHash handles null, undefined, whitespace, and empty paths gracefully', () => {
  assert.deepEqual(getRouteFromHash(undefined), { type: 'dashboard' })
  assert.deepEqual(getRouteFromHash(null), { type: 'dashboard' })
  assert.deepEqual(getRouteFromHash('   '), { type: 'dashboard' })
  assert.deepEqual(getRouteFromHash('#   '), { type: 'dashboard' })
  assert.deepEqual(getRouteFromHash('#///'), { type: 'dashboard' })
})

test('seedHistory creates proper baseline stack for settings and sources', () => {
  const actions = []
  const originalWindow = globalThis.window
  globalThis.window = {
    history: {
      replaceState: (state, title, url) => actions.push({ type: 'replace', state, url }),
      pushState: (state, title, url) => actions.push({ type: 'push', state, url }),
    },
  }

  try {
    seedHistory({ type: 'sources' })
    assert.equal(actions.length, 3)
    assert.deepEqual(actions[0], { type: 'replace', state: { type: 'dashboard', hash: '#/' }, url: '#/' })
    assert.deepEqual(actions[1], { type: 'push', state: { type: 'settings', hash: '#/settings' }, url: '#/settings' })
    assert.deepEqual(actions[2], { type: 'push', state: { type: 'sources', hash: '#/sources' }, url: '#/sources' })
  } finally {
    globalThis.window = originalWindow
  }
})

test('getRouteFromHash falls back to storage when payload filename is omitted or blank', () => {
  assert.deepEqual(getRouteFromHash('#/payload'), { type: 'storage' })
  assert.deepEqual(getRouteFromHash('#/payload/'), { type: 'storage' })
  assert.deepEqual(getRouteFromHash('#/payload/   '), { type: 'storage' })
  assert.deepEqual(getRouteFromHash('#/storage/payload'), { type: 'storage' })
  assert.deepEqual(getRouteFromHash('#/storage/payload/'), { type: 'storage' })
  assert.deepEqual(getRouteFromHash('#/storage/payload/   '), { type: 'storage' })
})

test('formatHash and getHistoryChain gracefully handle missing payload filenames', () => {
  assert.equal(formatHash({ type: 'payload', filename: '' }), '#/storage')
  assert.equal(formatHash({ type: 'payload', filename: null }), '#/storage')
  assert.deepEqual(getHistoryChain({ type: 'payload', filename: '' }), [
    { type: 'dashboard' },
    { type: 'storage' },
  ])
  assert.deepEqual(getHistoryChain({ type: 'payload', filename: null }), [
    { type: 'dashboard' },
    { type: 'storage' },
  ])
})

test('multi-source resolution prioritizes matching srcId before falling back', () => {
  const enrichedSources = [
    {
      id: 'repo-1',
      url: 'https://repo1.example.com',
      payloads: [
        { filename: 'duplicate.elf', name: 'Duplicate from Repo 1', isInstalled: false, isUpdate: false },
      ],
    },
    {
      id: 'repo-2',
      url: 'https://repo2.example.com',
      payloads: [
        { filename: 'duplicate.elf', name: 'Duplicate from Repo 2', isInstalled: true, isUpdate: false },
      ],
    },
  ]

  const resolvePayload = (selected, sources) => {
    const targetFilename = selected.filename
    if (selected.srcId) {
      const targetSrc = sources.find(s => s.id === selected.srcId)
      if (targetSrc) {
        const match = targetSrc.payloads?.find(p => p.filename === targetFilename)
        if (match) {
          return {
            payload: match,
            srcId: targetSrc.id,
            srcUrl: targetSrc.url,
          }
        }
      }
    }
    for (const src of sources) {
      const match = src.payloads?.find(p => p.filename === targetFilename)
      if (match) {
        return {
          payload: match,
          srcId: src.id,
          srcUrl: src.url,
        }
      }
    }
    return null
  }

  // 1. When srcId specifies repo-2, it must return repo-2 even though repo-1 has duplicate.elf first
  const resolved2 = resolvePayload({ filename: 'duplicate.elf', srcId: 'repo-2' }, enrichedSources)
  assert.equal(resolved2.srcId, 'repo-2')
  assert.equal(resolved2.payload.name, 'Duplicate from Repo 2')

  // 2. When srcId is not provided, it falls back to the first available source (repo-1)
  const resolvedDefault = resolvePayload({ filename: 'duplicate.elf', srcId: null }, enrichedSources)
  assert.equal(resolvedDefault.srcId, 'repo-1')
  assert.equal(resolvedDefault.payload.name, 'Duplicate from Repo 1')

  // 3. When srcId is unknown, it falls back to the first matching source
  const resolvedFallback = resolvePayload({ filename: 'duplicate.elf', srcId: 'unknown-repo' }, enrichedSources)
  assert.equal(resolvedFallback.srcId, 'repo-1')
  assert.equal(resolvedFallback.payload.name, 'Duplicate from Repo 1')
})

test('writeHistory and seedHistory do not throw when window or window.history is absent', () => {
  const originalWindow = globalThis.window
  try {
    globalThis.window = undefined
    assert.doesNotThrow(() => writeHistory({ type: 'dashboard' }))
    assert.doesNotThrow(() => seedHistory({ type: 'dashboard' }))

    globalThis.window = {}
    assert.doesNotThrow(() => writeHistory({ type: 'dashboard' }))
    assert.doesNotThrow(() => seedHistory({ type: 'dashboard' }))
  } finally {
    globalThis.window = originalWindow
  }
})

test('initial popstate on root dashboard without user navigation is safely ignored', () => {
  // Simulate WebKit onload popstate behavior
  let closed = false
  let backed = false
  const mockWindow = {
    close: () => { closed = true },
    history: { back: () => { backed = true } },
    location: { hash: '#/' },
  }

  const simulatePopState = ({ curView, targetType, hasNavigated, mountAgeMs }) => {
    // Mirror handlePopState logic
    if (!hasNavigated && curView === 'dashboard' && targetType === 'dashboard') {
      return 'ignored_initial'
    }
    if (mountAgeMs < 1500 && curView === 'dashboard' && targetType === 'dashboard') {
      return 'ignored_recent_mount'
    }
    if (curView === 'dashboard' && targetType === 'dashboard') {
      if (hasNavigated) {
        mockWindow.close()
      }
      return 'handled_dashboard'
    }
    return 'transition'
  }

  // 1. Initial load popstate (mountAgeMs = 100, hasNavigated = false)
  const res1 = simulatePopState({ curView: 'dashboard', targetType: 'dashboard', hasNavigated: false, mountAgeMs: 100 })
  assert.equal(res1, 'ignored_initial')
  assert.equal(closed, false)
  assert.equal(backed, false)

  // 2. Popstate after user actively navigated back to dashboard
  const res2 = simulatePopState({ curView: 'dashboard', targetType: 'dashboard', hasNavigated: true, mountAgeMs: 5000 })
  assert.equal(res2, 'handled_dashboard')
  assert.equal(closed, true)
  assert.equal(backed, false) // should never call history.back inside popstate
})

test('getRouteFromHash and formatHash correctly roundtrip modes and modals', () => {
  // 1. Dashboard modes
  assert.deepEqual(getRouteFromHash('#/?mode=manage'), { type: 'dashboard', mode: 'manage' })
  assert.deepEqual(getRouteFromHash('#/dashboard?mode=manage'), { type: 'dashboard', mode: 'manage' })
  assert.deepEqual(getRouteFromHash('#/?mode=favorites'), { type: 'dashboard', mode: 'favorites' })
  assert.equal(formatHash({ type: 'dashboard', mode: 'manage' }), '#/?mode=manage')
  assert.equal(formatHash({ type: 'dashboard', mode: 'favorites' }), '#/?mode=favorites')

  // 2. Modals on dashboard
  assert.deepEqual(getRouteFromHash('#/?modal=confirm'), { type: 'dashboard', modal: 'confirm' })
  assert.equal(formatHash({ type: 'dashboard', modal: 'confirm' }), '#/?modal=confirm')

  // 3. Stacking mode + modal
  assert.deepEqual(getRouteFromHash('#/?mode=manage&modal=confirm'), { type: 'dashboard', mode: 'manage', modal: 'confirm' })
  assert.equal(formatHash({ type: 'dashboard', mode: 'manage', modal: 'confirm' }), '#/?mode=manage&modal=confirm')

  // 4. Modals on other pages
  assert.deepEqual(getRouteFromHash('#/storage?modal=conflict'), { type: 'storage', modal: 'conflict' })
  assert.equal(formatHash({ type: 'storage', modal: 'conflict' }), '#/storage?modal=conflict')

  assert.deepEqual(getRouteFromHash('#/settings?modal=logs'), { type: 'settings', modal: 'logs' })
  assert.equal(formatHash({ type: 'settings', modal: 'logs' }), '#/settings?modal=logs')

  assert.deepEqual(getRouteFromHash('#/autoload?modal=delay'), { type: 'autoload', modal: 'delay' })
  assert.equal(formatHash({ type: 'autoload', modal: 'delay' }), '#/autoload?modal=delay')

  assert.deepEqual(getRouteFromHash('#/payload/ps5-kstuff.elf?modal=external_link'), { type: 'payload', filename: 'ps5-kstuff.elf', srcId: null, modal: 'external_link' })
  assert.equal(formatHash({ type: 'payload', filename: 'ps5-kstuff.elf', modal: 'external_link' }), '#/payload/ps5-kstuff.elf?modal=external_link')
})

test('getHistoryChain builds proper hierarchy when modes and modals are present', () => {
  assert.deepEqual(getHistoryChain({ type: 'dashboard', mode: 'manage' }), [
    { type: 'dashboard' },
    { type: 'dashboard', mode: 'manage' },
  ])

  assert.deepEqual(getHistoryChain({ type: 'dashboard', mode: 'manage', modal: 'confirm' }), [
    { type: 'dashboard' },
    { type: 'dashboard', mode: 'manage' },
    { type: 'dashboard', mode: 'manage', modal: 'confirm' },
  ])

  assert.deepEqual(getHistoryChain({ type: 'storage', modal: 'conflict' }), [
    { type: 'dashboard' },
    { type: 'storage' },
    { type: 'storage', modal: 'conflict' },
  ])

  assert.deepEqual(getHistoryChain({ type: 'settings', modal: 'logs' }), [
    { type: 'dashboard' },
    { type: 'settings' },
    { type: 'settings', modal: 'logs' },
  ])

  assert.deepEqual(getHistoryChain({ type: 'payload', filename: 'ps5-kstuff.elf', modal: 'external_link' }), [
    { type: 'dashboard' },
    { type: 'storage' },
    { type: 'payload', filename: 'ps5-kstuff.elf' },
    { type: 'payload', filename: 'ps5-kstuff.elf', modal: 'external_link' },
  ])
})

test('back navigation dismisses modal then exits mode without prematurely closing browser', () => {
  let browserClosed = false
  let confirmClosed = false
  let manageModeClosed = false

  const mockState = {
    confirmModal: { show: true },
    isManageMode: true,
    historyModal: 'confirm',
    historyMode: 'manage',
  }

  const handlePop = (destinationHash) => {
    const target = getRouteFromHash(destinationHash)

    // 1. Dismiss modal first
    if (mockState.historyModal) {
      mockState.historyModal = target.modal || null
      if (mockState.confirmModal.show) {
        mockState.confirmModal.show = false
        confirmClosed = true
      }
      return 'dismissed_modal'
    }

    // 2. Dismiss mode
    if (mockState.isManageMode && target.mode !== 'manage') {
      mockState.isManageMode = false
      mockState.historyMode = target.mode || null
      manageModeClosed = true
      return 'dismissed_mode'
    }

    // 3. Close browser only on clean root dashboard
    if (target.type === 'dashboard' && !target.mode && !target.modal) {
      browserClosed = true
      return 'closed_browser'
    }

    return 'other'
  }

  // Step 1: User is at #/?mode=manage&modal=confirm. Controller Circle pops to #/?mode=manage.
  const step1 = handlePop('#/?mode=manage')
  assert.equal(step1, 'dismissed_modal')
  assert.equal(confirmClosed, true)
  assert.equal(manageModeClosed, false)
  assert.equal(browserClosed, false)
  assert.equal(mockState.isManageMode, true)

  // Step 2: User is at #/?mode=manage. Controller Circle pops to #/.
  const step2 = handlePop('#/')
  assert.equal(step2, 'dismissed_mode')
  assert.equal(manageModeClosed, true)
  assert.equal(browserClosed, false)
  assert.equal(mockState.isManageMode, false)

  // Step 3: User is at #/ (clean root dashboard). Controller Circle on root exits browser.
  const step3 = handlePop('#/')
  assert.equal(step3, 'closed_browser')
  assert.equal(browserClosed, true)
})

test('back navigation dismisses external link modal on payload page without exiting to storage', () => {
  let externalLinkClosed = false
  let storageNavigated = false

  const mockPayloadState = {
    externalLinkUrl: 'https://example.com',
    historyModal: 'external_link',
    selectedPayload: { filename: 'kstuff.elf' },
  }

  const handlePayloadPop = (destinationHash) => {
    const target = getRouteFromHash(destinationHash)

    // 1. Dismiss modal first
    if (mockPayloadState.historyModal) {
      mockPayloadState.historyModal = target.modal || null
      if (mockPayloadState.externalLinkUrl) {
        mockPayloadState.externalLinkUrl = null
        externalLinkClosed = true
      }
      return 'dismissed_modal'
    }

    // 2. View transition
    if (target.type === 'storage') {
      mockPayloadState.selectedPayload = null
      storageNavigated = true
      return 'navigated_storage'
    }

    return 'other'
  }

  // Step 1: User is viewing payload with external link modal open at #/payload/kstuff.elf?modal=external_link.
  // Back button pops hash to #/payload/kstuff.elf.
  const step1 = handlePayloadPop('#/payload/kstuff.elf')
  assert.equal(step1, 'dismissed_modal')
  assert.equal(externalLinkClosed, true)
  assert.equal(storageNavigated, false)
  assert.notEqual(mockPayloadState.selectedPayload, null)

  // Step 2: User is now at #/payload/kstuff.elf with modal dismissed.
  // Back button pops hash to #/storage.
  const step2 = handlePayloadPop('#/storage')
  assert.equal(step2, 'navigated_storage')
  assert.equal(storageNavigated, true)
  assert.equal(mockPayloadState.selectedPayload, null)
})


