import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { viteSingleFile } from 'vite-plugin-singlefile'
import browserslist from 'browserslist'
import { browserslistToTargets } from 'lightningcss'

// [CN-FORK] mock 后端代理目标；用 MOCK_PORT 环境变量指向备用端口（默认 8081），
// 便于在 8081 被遗留进程占用时另起一份干净 mock 做预览。
const API_TARGET = `http://127.0.0.1:${Number(process.env.MOCK_PORT) || 8081}`

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [react(), viteSingleFile()],
  build: {
    target: ['es2015', 'safari12'],
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: true,
        drop_debugger: true,
      },
      format: {
        comments: false,
      },
    },
    cssCodeSplit: false,
    assetsInlineLimit: 100000, // Inline all assets
  },
  server: {
    proxy: {
      '/getip': { target: API_TARGET, changeOrigin: true },
      '/version': { target: API_TARGET, changeOrigin: true },
      '/list_payloads': { target: API_TARGET, changeOrigin: true },
      '/autoload_status': { target: API_TARGET, changeOrigin: true },
      '/get_config': { target: API_TARGET, changeOrigin: true },
      '/set_config': { target: API_TARGET, changeOrigin: true },
      /* [CN-FORK] v0.5.1.64 删 /system_stats 代理：后端该端点随主页硬件信息功能整体下线 */
      '/loadpayload:': { target: API_TARGET, changeOrigin: true },
      '/manage:': { target: API_TARGET, changeOrigin: true },
      '/abort': { target: API_TARGET, changeOrigin: true },
      '/autoload_clear': { target: API_TARGET, changeOrigin: true },
      '/events': {
        target: API_TARGET,
        changeOrigin: true,
        sse: true
      },
      '/sources_list': { target: API_TARGET, changeOrigin: true },
      '/sources_set': { target: API_TARGET, changeOrigin: true },
      '/sources_add': { target: API_TARGET, changeOrigin: true },
      '/sources_remove': { target: API_TARGET, changeOrigin: true },
      '/repository_payloads': { target: API_TARGET, changeOrigin: true },
      '/repository_refresh': { target: API_TARGET, changeOrigin: true },
      // [CN-FORK] 「PKG安装」视图与设置页管理卡片需要这三个端点，缺了本地 dev 会直接落到 failed 态
      '/pkgmanager_status': { target: API_TARGET, changeOrigin: true },
      '/pkgmanager_launch': { target: API_TARGET, changeOrigin: true },
      '/pkgmanager_stop': { target: API_TARGET, changeOrigin: true },
      // [CN-FORK] 上传是 POST 且 mock 已实现，不代理的话设置页「上传更新」在本地必失败
      '/pkgmanager_upload': { target: API_TARGET, changeOrigin: true },
      // [CN-FORK] 「常用入口」页三个端点：缺了会被 SPA fallback 吞成 index.html，
      //           res.json() 抛错又被 catch 静默吃掉，页面永远卡在转圈（看不出原因）
      '/quicklinks': { target: API_TARGET, changeOrigin: true },
      '/quicklinks_add': { target: API_TARGET, changeOrigin: true },
      '/quicklinks_remove': { target: API_TARGET, changeOrigin: true },
      // [CN-FORK] 温控/游戏板块三个端点：缺了会被 SPA fallback 吞成 index.html，预览里永远读失败
      '/thermal_stats': { target: API_TARGET, changeOrigin: true },
      '/game_list': { target: API_TARGET, changeOrigin: true },
      '/game_icon': { target: API_TARGET, changeOrigin: true },
      '/fan_control': { target: API_TARGET, changeOrigin: true },
      /* [CN-FORK] v0.5.1.64 删 /game_icon 代理：「当前游戏」探测随主页一起下线 */
      '/repository_install': { target: API_TARGET, changeOrigin: true }
    }
  }
})
