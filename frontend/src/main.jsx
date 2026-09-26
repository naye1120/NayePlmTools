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



