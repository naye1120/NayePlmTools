import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import './index.css'
// [CN-FORK] 主题层（必须在 index.css 之后导入，靠源码顺序覆盖暗色基线）
import './theme/light-theme.css'
import './theme/cn-typography.css'
import App from './App.jsx'
import './i18n';
import './theme/useTheme'; // 导入即应用主题偏好，避免首屏闪烁

createRoot(document.getElementById('root')).render(
  <StrictMode>
    <App />
  </StrictMode>,
)

if (window.applicationCache) {
  window.applicationCache.addEventListener('updateready', () => {
    if (window.applicationCache.status === window.applicationCache.UPDATEREADY) {
      window.applicationCache.swapCache();
      window.location.reload();
    }
  }, false);
}

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



