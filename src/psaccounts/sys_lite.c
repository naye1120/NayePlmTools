/* PS5 Account Tools — minimal system support.
 *
 * Standalone (no elf-arsenal deps). Provides only what the in-process feature
 * modules (npsignin, offact, avatar) and the lean web server need:
 *   - credential escalation + UserService init at load (constructor)
 *   - foreground-user lookup
 *
 * Escalation is done directly against the ps5-payload-sdk kernel API
 * (<ps5/kernel.h>, linked via -lkernel_sys) rather than a vendored helper.
 * It is required so the payload can touch the system registry (sceRegMgr*)
 * and write the per-user NP files under /system_data/priv.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <ps5/kernel.h>

#include "sys.h"


/* ------------------------------------------------------------------ *
 * SceUserService                                                       *
 * ------------------------------------------------------------------ */

int  sceUserServiceGetForegroundUser(uint32_t *user_id);
int  sceUserServiceGetUserName(int32_t user_id, char *name, size_t max_size);
/* [CN-FORK] Initialize/Terminate 的extern 声明随 constructor 一起去掉：本进程由
 * src/main.c 带优先级参数初始化用户服务，退出路径也归 main.c 管。 */


/* ------------------------------------------------------------------ *
 * Credential escalation (native SDK kernel API).                       *
 *                                                                      *
 * Grants root uids/gids, an unrestricted authid + capabilities, and    *
 * breaks out of the jail by pointing rootdir/jaildir at the real root  *
 * vnode. Mirrors the standard ps5-payload escalation.                  *
 * ------------------------------------------------------------------ */

#define JB_AUTHID  0x4801000000000013ULL

static int
escalate_creds(pid_t pid) {
  if(pid <= 0) return -1;
  if(!kernel_get_proc(pid)) return -1;

  int rc = 0;

  /* uid/gid → 0 (root) on every cred slot the kernel checks. */
  if(kernel_set_ucred_uid  (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_ruid (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_svuid(pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_rgid (pid, 0) != 0) rc = -1;
  if(kernel_set_ucred_svgid(pid, 0) != 0) rc = -1;

  /* Escape the jail so /system_data/priv is reachable. */
  intptr_t rootvnode = kernel_get_root_vnode();
  if(rootvnode) {
    if(kernel_set_proc_rootdir(pid, rootvnode) != 0) rc = -1;
    if(kernel_set_proc_jaildir(pid, rootvnode) != 0) rc = -1;
  }

  /* Unrestricted authid + full capabilities + high-attr flag. */
  if(kernel_set_ucred_authid(pid, JB_AUTHID) != 0) rc = -1;
  uint8_t caps[16];
  memset(caps, 0xff, sizeof(caps));
  if(kernel_set_ucred_caps(pid, caps) != 0) rc = -1;

  uint8_t attrs[32] = {0};
  attrs[0] = 0x80;
  if(kernel_set_ucred_attrs(pid, attrs) != 0) rc = -1;

  return rc;
}


/* ------------------------------------------------------------------ *
 * Foreground user                                                      *
 * ------------------------------------------------------------------ */

uint32_t
sys_get_foreground_user(char *name_out, size_t name_out_size) {
  uint32_t uid = 0;
  if(sceUserServiceGetForegroundUser(&uid) != 0 || uid == 0) {
    if(name_out && name_out_size > 0) name_out[0] = 0;
    return 0;
  }
  if(name_out && name_out_size > 0) {
    char tmp[17] = {0};
    if(sceUserServiceGetUserName((int32_t)uid, tmp, sizeof(tmp)) != 0) {
      tmp[0] = 0;
    }
    size_t n = strlen(tmp);
    if(n >= name_out_size) n = name_out_size - 1;
    memcpy(name_out, tmp, n);
    name_out[n] = 0;
  }
  return uid;
}


/* ------------------------------------------------------------------ *
 * Startup: escalate our own creds.                                     *
 * [CN-FORK] v0.5.2.99 并入 NayePlmTools 主 ELF 后的改动，只动这一节：                 *
 *   - 原来是 __attribute__((constructor))，在 main() 之前自动执行。并入后改成       *
 *     由 src/main.c 显式调用 psaccounts_escalate()，位置仍在 SceUserService 初始    *
 *     化之前（顺序与原工程一致：先拿 root/出 jail，再 init 用户服务）。构造函数     *
 *     形式在本仓库里不可见、也不可关，出问题时无法从启动序列判断是谁提的权。        *
 *   - 去掉 syscall(SYS_thr_set_name, ...)：本进程的线程名由 main.c 统一设成         *
 *     "NayePlmTools.elf"，前端与 find_pid 的旧进程清理都依赖这个名字。              *
 *   - 去掉 sceUserServiceInitialize/Terminate：主进程已经带 prio 参数初始化过，     *
 *     重复 init 会让第二次返回 0x80960003，重复 terminate 则可能在服务还活着时      *
 *     把它关掉。                                                                    *
 *   - 去掉 signal(SIGCHLD, SIG_IGN)：本仓库要 fork/exec，屏蔽 CHLD 会破坏 wait()。  *
 *     SIGPIPE 的忽略 main.c 里已有。                                                *
 * 提权本身一行没改：sceRegMgr 读写与 /system_data/priv 下的 NP 文件、profile        *
 * 缓存都必须有 root + 真 root vnode，这是这些功能能跑起来的前提。                   *
 * ------------------------------------------------------------------ */

void
psaccounts_escalate(void) {
  if(escalate_creds(getpid()) != 0)
    fprintf(stderr, "psaccounts: credential escalation partially failed\n");
}
