// Safari 11 may lack AbortController. Always guard stale callbacks; cancel the
// transport too when the browser supports it.
export function createRequestScope(Controller = globalThis.AbortController) {
  const controller = typeof Controller === 'function' ? new Controller() : null
  let aborted = false
  return {
    signal: controller?.signal,
    get aborted() { return aborted },
    abort() { aborted = true; controller?.abort() },
  }
}
