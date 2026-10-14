// Mirrors the native HTTP contract so development cannot hide GET mutations.
export const mutationRoutes = [
  '/manage:upload', '/manage:delete', '/shutdown', '/set_config', '/abort',
  '/autoload_clear', '/repository_cache_clear', '/repository_refresh', '/repository_install', '/sources_set',
  '/sources_add', '/sources_remove', '/usb_move_perform', '/process_kill',
]
export function isMutationRoute(path) {
  return mutationRoutes.includes(path) || path.startsWith('/loadpayload:')
}
export function sameOrigin(value, host, referer = false) {
  if (!value || !host || !value.startsWith('http://') ||
      !Array.from(host).every(c => /[a-zA-Z0-9]/.test(c) || '.-:[]'.includes(c))) return false
  const rest = value.slice(7)
  const authority = rest.split(/[/?#]/)[0]
  if (authority.toLowerCase() !== host.toLowerCase()) return false
  const suffix = rest.slice(authority.length)
  return referer ? !suffix || suffix.startsWith('/') : !suffix
}
export function mutationAuthorized(headers) {
  if (headers['sec-fetch-site'] === 'cross-site') return false
  if (headers.origin !== undefined) return sameOrigin(headers.origin, headers.host)
  if (headers.referer !== undefined) return sameOrigin(headers.referer, headers.host, true)
  return headers['x-pldmgr-request'] === '1'
}
export function mutationPolicy(req, res, next) {
  res.set('Vary', 'Origin')
  const trusted = sameOrigin(req.headers.origin, req.headers.host)
  if (trusted) res.set('Access-Control-Allow-Origin', req.headers.origin)
  const expected = isMutationRoute(req.path) ? 'POST' : 'GET'
  if (req.method === 'OPTIONS') {
    const requested = req.headers['access-control-request-method']
    const list = req.headers['access-control-request-headers']
    const allowed = !list || list.split(',').every(header =>
      ['content-type', 'x-pldmgr-request'].includes(header.trim().toLowerCase()))
    if (!trusted || !allowed || (requested && requested !== expected)) {
      return res.status(403).json({ error: 'Preflight rejected' })
    }
    return res.set('Access-Control-Allow-Methods', expected)
      .set('Access-Control-Allow-Headers', 'Content-Type, X-PLDMGR-Request').sendStatus(200)
  }
  if (req.method !== expected) return res.set('Allow', `${expected}, OPTIONS`).sendStatus(405)
  if (expected === 'POST' && !mutationAuthorized(req.headers)) {
    return res.status(403).json({ error: 'Mutation origin or request marker rejected' })
  }
  next()
}
