import './polyfills.js'
import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
// [CN-FORK] 主题层（必须在 index.css 之后导入，靠源码顺序覆盖暗色基线）
import './theme/light-theme.css'
import './theme/cn-typography.css'
import App from './App.jsx'
import './i18n';
import './theme/useTheme'; // 导入即应用主题偏好，避免首屏闪烁
/* [CN-FORK] v0.6.0 合入：App.jsx 的 useRepository() 要靠这层 Provider 供数，
 * 根级 ErrorBoundary 兜住整个 render 周期外的崩溃（App.jsx 里另有按视图的
 * ErrorBoundary，两层不冲突）。 */
import { RepositoryProvider } from './contexts/RepositoryContext'
import ErrorBoundary from './components/ui/ErrorBoundary'

// Setup global uncaught error display before anything else runs
if (typeof window !== 'undefined') {
  const escapeHtml = (str) => {
    return String(str || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  };

  const showGlobalError = (title, err) => {
    try {
      let container = document.getElementById('ps5-global-error-overlay');
      if (!container) {
        container = document.createElement('div');
        container.id = 'ps5-global-error-overlay';
        container.style.cssText =
          'position:fixed;top:0;left:0;right:0;bottom:0;background:#08080a;color:#f87171;padding:24px;font-family:monospace;font-size:13px;z-index:9999999;overflow:auto;white-space:pre-wrap;line-height:1.5;box-sizing:border-box;';
        const target = document.body || document.documentElement;
        if (target) {
          target.appendChild(container);
        } else {
          window.addEventListener('DOMContentLoaded', () => {
            (document.body || document.documentElement).appendChild(container);
          });
        }
      }
      const rawMsg = err?.stack || err?.message || String(err || 'Unknown error');
      const safeTitle = escapeHtml(title);
      const safeMsg = escapeHtml(rawMsg);
      container.innerHTML = `
        <div style="max-width:900px;margin:20px auto;background:rgba(80,10,10,0.4);border:2px solid #ef4444;border-radius:16px;padding:24px;">
          <div style="font-size:20px;font-weight:bold;color:#fff;margin-bottom:12px;text-transform:uppercase;">${safeTitle}</div>
          <div style="color:#d4d4d8;margin-bottom:16px;font-size:14px;">An uncaught error was triggered outside of the React render cycle:</div>
          <div style="background:#000;border:1px solid #7f1d1d;border-radius:8px;padding:16px;color:#fca5a5;overflow-x:auto;max-height:300px;margin-bottom:20px;">${safeMsg}</div>
          <div style="display:flex;gap:12px;flex-wrap:wrap;">
            <button onclick="window.location.reload()" style="background:#dc2626;color:#fff;border:none;padding:10px 20px;border-radius:8px;font-weight:bold;cursor:pointer;font-family:monospace;text-transform:uppercase;">Reload</button>
            <button onclick="try{localStorage.clear();sessionStorage.clear();}catch(e){}window.location.reload();" style="background:#27272a;color:#e4e4e7;border:1px solid #3f3f46;padding:10px 20px;border-radius:8px;font-weight:bold;cursor:pointer;font-family:monospace;text-transform:uppercase;">Reset Storage & Reload</button>
          </div>
        </div>
      `;
    } catch (e) {
      console.error('Failed to show global error overlay:', e);
    }
  };

  window.addEventListener('error', (event) => {
    console.error('Global error caught:', event);
    showGlobalError(
      `Uncaught Error: ${event.message || 'Error'}`,
      event.error || `${event.filename || 'unknown'}:${event.lineno || 0}:${event.colno || 0}`
    );
  });

  window.addEventListener('unhandledrejection', (event) => {
    console.error('Unhandled rejection caught:', event);
    showGlobalError('Unhandled Promise Rejection', event.reason);
  });
}

/* [CN-FORK] v0.6.0 合入：上游把根级 render 包了 ErrorBoundary + RepositoryProvider
 * （fork 原来是裸 StrictMode + App）。两者都留着：ErrorBoundary 兜整棵树，
 * RepositoryProvider 给 App 的 useRepository() 供数。挂载点仍紧跟全局错误兜底之后。 */
createRoot(document.getElementById('root')).render(
  <ErrorBoundary>
    <StrictMode>
      <RepositoryProvider>
        <App />
      </RepositoryProvider>
    </StrictMode>
  </ErrorBoundary>,
)

// [CN-FORK] 版本握手自动刷新：PS5 浏览器常驻 SPA 文档，AppCache 更新只有在文档重新导航时才会应用，
// 换版重启守护进程后旧页面会一直残留并走旧逻辑（如点击拉起直接裸跳 :8844 因而失败）。
// 这里在挂载 / 页面重新可见 / 获得焦点 / 定时轮询时比对 /version 与构建时烧入的 __APP_VERSION__，
// 不一致则刷新页面。sessionStorage 计数防止服务端版本号异常时刷新死循环。
// 注：0.5.2.81 及更早的版本无此逻辑，换版后需手动刷新一次页面；自此以后自动收敛。
if (__APP_VERSION__ !== 'dev') {
  const RELOAD_COUNT_KEY = 'pldmgr_reload_count';
  const checkVersion = async () => {
    try {
      const res = await fetch('/version', { cache: 'no-store' });
      if (!res.ok) return;
      const serverVersion = (await res.text()).trim();
      if (!serverVersion) return;
      if (serverVersion === __APP_VERSION__) {
        sessionStorage.removeItem(RELOAD_COUNT_KEY);
        return;
      }
      const count = Number(sessionStorage.getItem(RELOAD_COUNT_KEY) || 0);
      if (count >= 3) return;
      sessionStorage.setItem(RELOAD_COUNT_KEY, String(count + 1));
      window.location.reload();
    } catch {
      /* 网络抖动：忽略，等下一轮触发 */
    }
  };
  checkVersion();
  setInterval(checkVersion, 60000);
  document.addEventListener('visibilitychange', () => {
    if (!document.hidden) checkVersion();
  });
  window.addEventListener('focus', checkVersion);
}

if (typeof window !== 'undefined' && window.applicationCache) {
  try {
    window.applicationCache.addEventListener(
      'updateready',
      () => {
        try {
          if (window.applicationCache.status === window.applicationCache.UPDATEREADY) {
            window.applicationCache.swapCache();
            const lastReload = Number(sessionStorage.getItem('last_appcache_reload') || 0);
            if (Date.now() - lastReload > 8000) {
              sessionStorage.setItem('last_appcache_reload', String(Date.now()));
              window.location.reload();
            }
          }
        } catch (e) {
          console.warn('applicationCache swapCache failed:', e);
        }
      },
      false
    );
  } catch (e) {
    console.warn('applicationCache listener registration failed:', e);
  }
}
