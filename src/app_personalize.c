/* [CN-FORK] v0.6.0.179 「个性化软件」的启动对账守护。
 *
 * 为什么单独一个文件：这里只是把三条各自独立的对账串起来（显隐在 app_visibility.c、
 * 置顶在 app_pin.c、自定义图标/背景在 app_tile_write.c），而它一被链接，
 * 就会把 app_tile_write.c 那套编解码器（stb）拉进来 —— 那些宿主测试里
 * 只有 app_visibility.c / app_pin.c 的小套件不该为此变重。所以守护自己一个翻译单元，
 * 测试不编它就等于不存在。
 *
 * 为什么可以每次开机无条件跑：三条链都是**先比一遍、一致就一次写都不发**
 * （见各自的 sync_all 注释）—— 幂等是这个设计成立的前提，不是优化。
 * 与 main.c 里那条 game_title_sync_start_daemon（改名被 SMP 冲回原名 → 开机重放）
 * 是同一套东西，「关机开机后被重置」这类问题在这个仓库里就用这一种修法。 */

#include "app_visibility.h"

#include <pthread.h>
#include <unistd.h>

#include "app_tile.h"
#include "pldmgr.h"
#include "utils.h"

/* 与 game_title.c 的 TT_SYNC_DELAY_SEC 同一个量级、同一个理由：payload 起来那几秒
 * app.db 常被 Shell / SMP 占着（tile_db.c 自己还有 5 秒 busy_timeout），
 * 等一会儿比第一次撞锁就失败划算。 */
#define AP_SYNC_DELAY_SEC 25

static void *ap_sync_main(void *arg) {
    (void)arg;
    sleep(AP_SYNC_DELAY_SEC);
    char msg[192];
    int c = 0, a = 0, f = 0;

    if (app_vis_sync_all(msg, sizeof(msg), &c, &a, &f) == 0)
        pldmgr_log("[PLDMGR] personalize: 显隐对账（%s）\n", msg);
    else
        pldmgr_log("[PLDMGR] personalize: 显隐对账失败（%s）\n", msg);

    if (app_pin_sync_all(msg, sizeof(msg), &c, &a, &f) == 0)
        pldmgr_log("[PLDMGR] personalize: 置顶对账（%s）\n", msg);
    else
        pldmgr_log("[PLDMGR] personalize: 置顶对账失败（%s）\n", msg);

    if (app_tile_sync_all(msg, sizeof(msg), &c, &a, &f) == 0)
        pldmgr_log("[PLDMGR] personalize: 图标/背景对账（%s）\n", msg);
    else
        pldmgr_log("[PLDMGR] personalize: 图标/背景对账失败（%s）\n", msg);

    return NULL;
}

void app_personalize_sync_start_daemon(void) {
    /* safe_mode：写主机数据库属于「死机后要能止血」的高危动作，与装 app 一个级别，
     * 一律跳过（同 game_title_sync_start_daemon）。 */
    if (pldmgr_utils_safe_mode()) {
        pldmgr_log("[PLDMGR] personalize: SAFE MODE，跳过启动对账\n");
        return;
    }
    pthread_t th;
    if (pthread_create(&th, NULL, ap_sync_main, NULL) == 0) {
        pthread_detach(th);
    } else {
        pldmgr_log("[PLDMGR] personalize: 对账线程创建失败，跳过\n");
    }
}
