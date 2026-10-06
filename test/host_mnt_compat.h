/* [CN-FORK] 主机侧编译兼容头 —— 只给 test/run_host_tests.sh 的 -include 用，**不进真机**。
 *
 * src/game_mgr.c 里唯一在主机上编不过的地方是 game_mgr_icon_path() 找沙盒 app0 那段：
 *     struct statfs *mnts = NULL;
 *     int mn = getmntinfo(&mnts, MNT_NOWAIT);
 *     ... mnts[i].f_mntonname ...
 * getmntinfo()/MNT_NOWAIT/struct statfs::f_mntonname 都是 BSD 专有，Linux 一个都没有。
 * 而 statfs() 那个**函数**在 game_mgr.c 里根本没被调用过 —— 只用了 getmntinfo 和
 * f_mntonname 这两项，所以在这里补一份最小定义就够，game_mgr.c 本身一个字都不用改
 * （v0.5.2.116「列表空掉」那类 bug 才有机会被断言兜住）。
 *
 * 主机上没有 /mnt/sandbox/<TID>_xxx/app0 挂载，返回 0 条 = 找不到沙盒图标，
 * 走「回落到 /user/app/<TID>/sce_sys」的正常分支，语义与真机上没这个挂载时一致。 */
#ifndef PLDMGR_HOST_MNT_COMPAT_H
#define PLDMGR_HOST_MNT_COMPAT_H

#ifndef MNT_NOWAIT
#define MNT_NOWAIT 0
#endif

struct statfs {
    char f_mntonname[1024];
};

static inline int getmntinfo(struct statfs **mp, int flags) {
    (void)mp;
    (void)flags;
    return 0;   /* 主机上永远没有沙盒挂载可列 */
}

#endif /* PLDMGR_HOST_MNT_COMPAT_H */
