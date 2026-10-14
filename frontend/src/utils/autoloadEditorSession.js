function encode(state) {
  return { AUTOLOAD_ENABLED: state.enabled,
    AUTOLOAD_LIST: state.list.map(item => item === 'DELAY' ? '!1000' : item).join(',') }
}
function key(state) {
  const value = encode(state)
  return `${value.AUTOLOAD_ENABLED}:${value.AUTOLOAD_LIST}`
}

// One editor owns its draft and serial save queue. Updates publish immediately;
// a completed request drains the latest draft, including edits made during it.
export function createAutoloadEditorSession() {
  let state = { initialized: false, enabled: false, list: [], saving: false, error: false }
  let savedKey = null, inFlight = null, exitKey = null, save
  const listeners = new Set()
  const publish = update => {
    state = { ...state, ...update }
    listeners.forEach(listener => listener())
  }
  async function drain() {
    if (!state.initialized || inFlight || key(state) === savedKey) return
    const submittedKey = key(state)
    const submitted = encode(state)
    inFlight = submittedKey
    publish({ saving: true, error: false })
    let success = false
    try { success = await save(submitted) === true } catch { /* retry remains explicit */ }
    // A failed multi-field config request can have published an earlier field.
    // Its backend state is unknown, even if the user reverted to the old draft.
    savedKey = success ? submittedKey : null
    inFlight = null
    publish({ saving: false, error: !success && key(state) === submittedKey })
    // A failed request must not discard newer edits or spin retrying itself.
    if (key(state) !== submittedKey) void drain()
  }
  return {
    getSnapshot: () => state,
    subscribe(listener) { listeners.add(listener); return () => listeners.delete(listener) },
    setSave(callback) { save = callback },
    initialize(config) {
      const valid = [true, false, 'true', 'false'].includes(config?.AUTOLOAD_ENABLED) &&
        typeof config?.AUTOLOAD_LIST === 'string'
      if (state.initialized || !valid) return
      state = { ...state, initialized: true, enabled: config.AUTOLOAD_ENABLED === true ||
        config.AUTOLOAD_ENABLED === 'true', list: config.AUTOLOAD_LIST.split(',').filter(Boolean) }
      savedKey = key(state)
      publish({})
    },
    edit(update) {
      if (!state.initialized) return
      const next = typeof update === 'function' ? update(state) : update
      publish(next)
      exitKey = null
      void drain()
    },
    retry() { if (state.initialized) void drain() },
    flush(send) {
      if (!state.initialized) return
      const desired = key(state)
      const dirty = desired !== savedKey || (inFlight !== null && inFlight !== desired)
      if (!dirty || desired === exitKey) return
      try {
        send(JSON.stringify(encode(state)))
        // Queued exit transport is not a server acknowledgment. An older normal
        // request still drains newer edits if JavaScript remains alive.
        exitKey = desired
      } catch { /* another exit event or explicit retry can try again */ }
    },
  }
}
