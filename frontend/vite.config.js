import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { viteSingleFile } from 'vite-plugin-singlefile'
import browserslist from 'browserslist'
import { browserslistToTargets } from 'lightningcss'

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
      '/getip': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/version': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/list_payloads': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/autoload_status': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/get_config': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/set_config': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/system_stats': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/loadpayload:': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/manage:': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/abort': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/autoload_clear': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/events': {
        target: 'http://127.0.0.1:8081',
        changeOrigin: true,
        sse: true
      },
      '/sources_list': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/sources_set': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/sources_add': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/sources_remove': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/repository_payloads': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/repository_refresh': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      // [CN-FORK] 「PKG安装」视图与设置页管理卡片需要这三个端点，缺了本地 dev 会直接落到 failed 态
      '/pkgmanager_status': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/pkgmanager_launch': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/pkgmanager_stop': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      // [CN-FORK] 上传是 POST 且 mock 已实现，不代理的话设置页「上传更新」在本地必失败
      '/pkgmanager_upload': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      // [CN-FORK] 「常用入口」页三个端点：缺了会被 SPA fallback 吞成 index.html，
      //           res.json() 抛错又被 catch 静默吃掉，页面永远卡在转圈（看不出原因）
      '/quicklinks': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/quicklinks_add': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/quicklinks_remove': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      // [CN-FORK] 首页「当前游戏」图标是 <img src>，同样走 dev server，不代理就取不到图
      '/game_icon': { target: 'http://127.0.0.1:8081', changeOrigin: true },
      '/repository_install': { target: 'http://127.0.0.1:8081', changeOrigin: true }
    }
  }
})
