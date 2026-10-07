/* [CN-FORK] v0.5.2.142 libcurl 的全局初始化，单独一个翻译单元。
 *
 * **为什么不放在 utils.c 里**（这一版就是这么写的，然后炸了）：
 * utils.c 被一大票**纯文件系统**的宿主测试链着（game_saves / media_scan /
 * game_mgr），它们本来一个字节的网络代码都不碰。`curl_global_init` 一进 utils.o，
 * 这些测试立刻 `undefined reference to curl_global_init`，等于把「存档的宿主测试
 * 不依赖 curl」这条设计（当初专门为此做了 game_saves_set_state_provider 这层
 * 解耦）从另一个方向拆掉了。挪到这个只被 curl 使用者链进来的文件里，两条都成立。
 *
 * curl 官方文档：`curl_global_init` 不是线程安全的，必须保证只被调用一次。
 * 这里用**文件级** pthread_once 兜住 —— 连接线程（用户手点「立即推送」）与自动
 * 存档守护线程完全可能同时是第一调用者，各持一把 once 控制块就是那个竞态。 */

#include <pthread.h>

#include <curl/curl.h>

#include "utils.h"

static pthread_once_t g_curl_once = PTHREAD_ONCE_INIT;

static void pldmgr_curl_init_once(void) { curl_global_init(CURL_GLOBAL_DEFAULT); }

void pldmgr_curl_init(void) {
    pthread_once(&g_curl_once, pldmgr_curl_init_once);
}
