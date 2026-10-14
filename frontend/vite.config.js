// [CN-FORK] v0.6.0.176 上游 eslint.config.js 已对 *config.js 注入 globals.node，
// 故删掉原来的 /* global process */ 声明 —— 两者同时声明会触发 no-redeclare。
import { readFileSync } from 'node:fs'
import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import { viteSingleFile } from 'vite-plugin-singlefile'

// [CN-FORK] mock 后端代理目标；用 MOCK_PORT 环境变量指向备用端口（默认 8081），
// 便于在 8081 被遗留进程占用时另起一份干净 mock 做预览。
const API_TARGET = `http://127.0.0.1:${Number(process.env.MOCK_PORT) || 8081}`

// [CN-FORK] 构建时烧入后端的 MENU_VERSION（供 src/main.jsx 与 /version 比对做版本握手）。
// 直接读 pldmgr.h 兜底：本机打包流程为「Windows 宿主 npm run build + WSL make all」，
// 不经 Makefile 的 frontend-build，只靠环境变量会静默退化成 'dev'（握手关闭）。
const readMenuVersion = () => {
  try {
    const src = readFileSync(new URL('../include/pldmgr.h', import.meta.url), 'utf8')
    const m = src.match(/#define\s+MENU_VERSION\s+"([^"]+)"/)
    return m ? m[1] : 'dev'
  } catch {
    return 'dev'
  }
}

// https://vitejs.dev/config/
export default defineConfig(({ command }) => ({
  plugins: [react(), viteSingleFile()],
  define: {
    // dev server 恒为 'dev'（跳过握手，避免本地联调时误刷新）；构建时 env 优先、否则读 pldmgr.h
    __APP_VERSION__: JSON.stringify(
      command === 'serve' ? 'dev' : process.env.APP_VERSION || readMenuVersion()
    ),
  },
  build: {
    target: ['es2015', 'safari11'],
    minify: 'terser',
    terserOptions: {
      compress: {
        drop_console: false,
        drop_debugger: true,
      },
      mangle: {
        safari10: true,
      },
      format: {
        comments: false,
        safari10: true,
      },
    },
    cssCodeSplit: false,
    assetsInlineLimit: 100000, // Inline all assets
  },
  server: {
    proxy: {
      /* [CN-FORK] v0.6.0 合入：changeOrigin 统一从 true 改成 false（上游改动，全部条目一起改）。
       * vite dev proxy 默认把转发请求的 Host 改写成目标主机，而后端 0.6.0 新加的
       * same_origin() / mutation_authorized() 是拿 Origin/Referer 和 Host 对的 ——
       * 改成 false 让浏览器侧的 Host 原样透传到后端，本地联调时「同源」判定才成立。 */
      /* 上游 0.6.0 新增的 5 条：USB 导入探测/执行、进程 kill、启动历史、关机。
       * 后端这 5 个路由都在（ROUTE_USB_MOVE_CHECK/PERFORM、ROUTE_PROCESS_KILL、
       * ROUTE_HISTORY_LIST、ROUTE_SHUTDOWN），缺代理会被 SPA fallback 吞成 index.html。 */
      '/usb_move_check': { target: API_TARGET, changeOrigin: false },
      '/usb_move_perform': { target: API_TARGET, changeOrigin: false },
      '/process_kill': { target: API_TARGET, changeOrigin: false },
      '/history_list': { target: API_TARGET, changeOrigin: false },
      '/shutdown': { target: API_TARGET, changeOrigin: false },
      '/getip': { target: API_TARGET, changeOrigin: false },
      '/version': { target: API_TARGET, changeOrigin: false },
      '/list_payloads': { target: API_TARGET, changeOrigin: false },
      '/autoload_status': { target: API_TARGET, changeOrigin: false },
      '/get_config': { target: API_TARGET, changeOrigin: false },
      '/set_config': { target: API_TARGET, changeOrigin: false },
      /* [CN-FORK] v0.5.1.64 删 /system_stats 代理：后端该端点随主页硬件信息功能整体下线 */
      '/loadpayload:': { target: API_TARGET, changeOrigin: false },
      /* [CN-FORK] v0.5.2.83 「常用入口」通用拉起前查进程表（同名已在跑就不重复拉起），
       * 缺这条代理会被 SPA fallback 吞成 index.html，判定永远读不到进程 → 静默重复拉起 */
      '/processes_list': { target: API_TARGET, changeOrigin: false },
      '/manage:': { target: API_TARGET, changeOrigin: false },
      '/abort': { target: API_TARGET, changeOrigin: false },
      '/autoload_clear': { target: API_TARGET, changeOrigin: false },
      '/events': {
        target: API_TARGET,
        changeOrigin: false,
        sse: true
      },
      '/sources_list': { target: API_TARGET, changeOrigin: false },
      '/sources_set': { target: API_TARGET, changeOrigin: false },
      '/sources_add': { target: API_TARGET, changeOrigin: false },
      '/sources_remove': { target: API_TARGET, changeOrigin: false },
      '/repository_payloads': { target: API_TARGET, changeOrigin: false },
      '/repository_refresh': { target: API_TARGET, changeOrigin: false },
      // [CN-FORK] 「PKG安装」视图与设置页管理卡片需要这三个端点，缺了本地 dev 会直接落到 failed 态
      '/pkgmanager_status': { target: API_TARGET, changeOrigin: false },
      // [CN-FORK] v0.5.2.131 游戏详情「去 ShadowMount 管理」先探后开（见 GameDetailModal.jsx）。
      // 缺这条代理会被 SPA fallback 吞成 index.html，r.json() 抛错被 catch 静默吃掉，
      // 探活永远读不到 running → 每次都走「拉起 → 等端口就绪」分支，而等就绪时同样读不到
      // → 31 秒超时 → 关掉刚开的空白标签。表现是「点按钮没反应」。
      '/smp_status': { target: API_TARGET, changeOrigin: false },
      '/pkgmanager_launch': { target: API_TARGET, changeOrigin: false },
      '/pkgmanager_stop': { target: API_TARGET, changeOrigin: false },
      // [CN-FORK] 上传是 POST 且 mock 已实现，不代理的话设置页「上传更新」在本地必失败
      '/pkgmanager_upload': { target: API_TARGET, changeOrigin: false },
      // [CN-FORK] 「常用入口」页三个端点：缺了会被 SPA fallback 吞成 index.html，
      //           res.json() 抛错又被 catch 静默吃掉，页面永远卡在转圈（看不出原因）
      '/quicklinks': { target: API_TARGET, changeOrigin: false },
      '/quicklinks_add': { target: API_TARGET, changeOrigin: false },
      '/quicklinks_remove': { target: API_TARGET, changeOrigin: false },
      // [CN-FORK] 温控/游戏板块三个端点：缺了会被 SPA fallback 吞成 index.html，预览里永远读失败
      '/thermal_stats': { target: API_TARGET, changeOrigin: false },
      '/game_list': { target: API_TARGET, changeOrigin: false },
      '/game_icon': { target: API_TARGET, changeOrigin: false },
      /* [CN-FORK] v0.5.2.112 游戏管理新功能（详情/删除/存档/多媒体/改名）整组端点。
       * 这一组有 20 个，逐条列太脆（漏一条的后果就是「预览里那个弹窗永远报读不到」，
       * 而且报的是被 SPA fallback 吞成 HTML 之后的 JSON 解析错误，完全看不出原因），
       * 所以统一用 /game_ 前缀代理 —— 后端这一组接口也全在这个前缀下，不会误伤别的路径。 */
      '/game_': { target: API_TARGET, changeOrigin: false },
      /* [CN-FORK] v0.5.2.166 主机「主屏固定列表」（app.db 的 tbl_group_<uid>）。
       * 这一组不在 /game_ 前缀下（后端路由是 /tile_group、/tile_group_pin、
       * /tile_group_order），漏了会被 SPA fallback 吞成 index.html ——
       * 表现是置顶按钮点了报「主机返回了无法识别的内容」，排序模式提交也永远失败。
       * 一条前缀就覆盖三条（http-proxy 按前缀匹配）。 */
      '/tile_group': { target: API_TARGET, changeOrigin: false },
      '/fan_control': { target: API_TARGET, changeOrigin: false },
      /* [CN-FORK] v0.5.1.64 删 /game_icon 代理：「当前游戏」探测随主页一起下线 */
      '/repository_install': { target: API_TARGET, changeOrigin: false },
      /* [CN-FORK] v0.5.2.99 PS5 Account Tools 源码并入：账号板块整组接口在 /api/ 前缀下
       * （/api/accounts[/…]、/api/safety[/restore]、/api/avatar[…]、/api/remoteplay/pin[/status]）。
       * 一条前缀代理覆盖全部；缺了会被 SPA fallback 吞成 index.html，界面只显示「读不到账号列表」。 */
      '/api': { target: API_TARGET, changeOrigin: false }    }
  }
}))
