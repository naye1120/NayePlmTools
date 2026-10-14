// The marker supports explicit LAN clients and browsers that omit Origin.
// It is not a secret or authentication credential. Foreign origins are rejected
// by the server even when they supply this marker.
export function apiFetch(url, options = {}) {
  const headers = new Headers(options.headers)
  headers.set('X-PLDMGR-Request', '1')
  return fetch(url, { ...options, headers })
}

export function mutationFetch(url, options = {}) {
  return apiFetch(url, { ...options, method: 'POST' })
}

// Beacons cannot set custom headers. The server accepts them only with a
// matching browser Origin/Referer. Queue failures use the marked fetch path.
export function saveConfigOnExit(body) {
  try {
    if (typeof navigator !== 'undefined' && navigator.sendBeacon &&
        navigator.sendBeacon('/set_config', new Blob([body], { type: 'application/json' }))) {
      return true
    }
  } catch { /* fall through to keepalive fetch */ }
  mutationFetch('/set_config', {
    headers: { 'Content-Type': 'application/json' }, body, keepalive: true,
  }).catch(() => {})
  return false
}
