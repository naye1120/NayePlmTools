#ifndef STUB_SERVERS_H
#define STUB_SERVERS_H

/* [CN-FORK] v0.5.2.142 宿主测试用的两个极小桩服务。
 *
 * 为什么要自己写 FTP 桩（而不是只测 URL 拼装）：用户真机上配的**就是 FTP**
 * （192.168.1.214:2121），推拉这条链上最容易错的几处（目录不存在时要不要自己
 * 建、EPSV/PASV 走哪条、USER/PASS 有没有真的带上）全在协议交互里，而这些
 * 用「函数返回值对不对」是测不出来的 —— 必须真跑一遍协议。
 *
 * 两个桩都只监听 127.0.0.1 的随机端口，只服务本测试进程，跑完即关。
 * 刻意做得**极简**：只实现 curl 实际会发的那些命令，多余的一律 502，
 * 这样协议面一变测试立刻会红（而不是被一个宽容的桩悄悄放过去）。 */

/* HTTP 桩：GET <path> 从 root 读文件（不存在回 404）；PUT <path> 写进 root。
 * 路径就是 URL 的路径部分（含子目录），<TID>/<uid>.zip 那两层原样落到磁盘。 */
int  stub_http_start(const char *root);
void stub_http_stop(void);
int  stub_http_port(void);

/* FTP 桩：USER/PASS/SYST/FEAT/PWD/CWD/MKD/TYPE/PASV/EPSV/STOR/RETR/SIZE/QUIT。
 * 有一个虚拟 cwd，映射到 root 下的真实路径 —— 这样 curl 的
 * CURLOPT_FTP_CREATE_MISSING_DIRS 才会真的走到 MKD 那条分支。 */
int  stub_ftp_start(const char *root);
void stub_ftp_stop(void);
int  stub_ftp_port(void);

#endif
