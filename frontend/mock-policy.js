// Mirrors the native HTTP contract so development cannot hide GET mutations.
//
// [CN-FORK] v0.6.0.177 这张表当年只抄了上游的 14 条，本 fork 加的 25 条 POST 路由
// 一条都没进来 —— 于是 mock 把真机上那个 405 原样复现了出来（expected 被算成 GET，
// 前端发 POST 就 405），而 `node --test` 里**没有任何用例覆盖这些路由**，
// 所以两头都是「测试全绿、真机上一片红」。现在与 src/http_server.c 的两张表对齐：
//   · postRoutes      → 方法闸门 + CSRF（is_post_route）
//   · getMutationRoutes → 方法仍是 GET，只过 CSRF（is_get_mutation）
// 分成两张而不是一张，正是因为原生侧若把变更型 GET 并进闸门，会把前端按 GET 调的
// 老功能从 405 换成 403 —— mock 要连这个**形状**一起镜像，才有镜像的意义。
export const postRoutes = [
  '/manage:upload', '/manage:delete', '/shutdown', '/set_config', '/abort',
  '/autoload_clear', '/repository_cache_clear', '/repository_refresh', '/repository_install', '/sources_set',
  '/sources_add', '/sources_remove', '/usb_move_perform', '/process_kill',
  /* [CN-FORK] 游戏管理 / 多媒体管理 / PKG 的 POST 端点（与 http_server.c 表 A 同一批） */
  '/game_save_upload', '/game_save_import_garlic', '/game_tile_icon', '/game_tile_background',
  '/game_music', '/game_music_at9', '/game_prefs_set', '/game_delete', '/game_save_auto',
  '/game_save_test', '/game_save_snap', '/game_save_sync', '/game_rename', '/game_title_sync',
  '/game_mount', '/game_unmount', '/game_uninstall', '/game_save_remote_delete',
  '/system_app_visible', '/system_app_pin', '/tile_group_pin', '/tile_group_order',
  '/game_media_pack', '/game_media_sync', '/pkgmanager_upload',
]
export const getMutationRoutes = [
  /* [CN-FORK] 变更型 GET：方法不变（前端就是这么调的），但必须过 CSRF 校验 */
  '/quicklinks_add', '/quicklinks_remove', '/game_save_delete', '/game_media_delete',
  '/game_tile_icon_reset', '/game_tile_background_reset', '/game_music_reset',
  '/game_job_cancel', '/pkgmanager_launch', '/pkgmanager_stop', '/fan_control',
]
/** @deprecated 用 isPostRoute / isGetMutationRoute；保留这个名字是因为它就是
 *  原生 is_mutation_route 的镜像（表 A ∪ 表 B）。 */
export function isMutationRoute(path) {
  return isPostRoute(path) || getMutationRoutes.includes(path)
}
export function isPostRoute(path) {
  return postRoutes.includes(path) || path.startsWith('/loadpayload:')
}
export function isGetMutationRoute(path) {
  return getMutationRoutes.includes(path)
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
  /* 方法闸门只看表 A；psaccounts 的 /api/* 在原生侧是 GET、POST 都放行，
   * 但 mock 没有 /api/* 的处理器，这里不镜像它（镜像了也只是让 404 提前出现）。 */
  const expected = isPostRoute(req.path) ? 'POST' : 'GET'
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
  /* CSRF 闸门看**总表**（表 A ∪ 表 B）：变更型 GET 一样要过。 */
  if (isMutationRoute(req.path) && !mutationAuthorized(req.headers)) {
    return res.status(403).json({ error: 'Mutation origin or request marker rejected' })
  }
  next()
}
