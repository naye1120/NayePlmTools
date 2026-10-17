#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <poll.h>
#include <sys/socket.h>
#include "ps5_launcher.h"

#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %d: %s (%s)\n",__LINE__,#x,mode);exit(1);} } while(0)
static const char *mode,*fixture;
static int peer=-1, sock=-1, file=-1, sockets, opened, history, calls;
static size_t sent_bytes, received_bytes, expected_size;
static unsigned char *received;
static pthread_t receiver;
static int has_receiver;
static long long started;
static long long now_ms(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
void pldmgr_log(const char *fmt,...) { (void)fmt; }
void history_mgr_add(const char *path) { CHECK(!strcmp(path,fixture));history++; }
int slot_open_payload(const char *path,char *canonical,size_t size) { CHECK(!strcmp(path,fixture));opened++;snprintf(canonical,size,"%s",path);file=open(path,O_RDONLY);return file; }
/* Self-update validation has its own integration suite. Ordinary transfers
 * must never enter the privileged opener or version reader. */
int slot_open_self_update(const char *path) { (void)path; abort(); }
int repository_self_update_version_fd(int fd, char *version, size_t size) { (void)fd; (void)version; (void)size; abort(); }
int slot_version_compare(const char *a, const char *b) { (void)a; (void)b; abort(); }
/* Disc-player calls are recorded; host tests never signal a real process. */
static int suspends, app_kills, signals, lookups, slept, app_queries;
void pldmgr_notify(const char *fmt,...) { (void)fmt; }
int sceLncUtilGetAppIdOfRunningBigApp(void) {
    app_queries++;
    return !strcmp(mode,"disc-none") || (!strcmp(mode,"disc-gone") && app_queries>1) ? -1 : 42;
}
int sceLncUtilGetAppTitleId(uint32_t app_id,char *title) {
    CHECK(app_id==42);
    if(!strcmp(mode,"disc-title-error")) return -1;
    strcpy(title,!strcmp(mode,"disc-other") ? "OTHERAPP" : "NPXS40140"); return 0;
}
int sceLncUtilSuspendApp(uint32_t app_id) { CHECK(app_id==42); suspends++; return !strcmp(mode,"disc-suspend-error") ? -1 : 0; }
int sceLncUtilKillApp(uint32_t app_id) { CHECK(app_id==42 && suspends==1); app_kills++; return !strcmp(mode,"disc-kill-error") || !strcmp(mode,"disc-gone") ? -1 : 0; }
pid_t process_find_pid(const char *name) { CHECK(!strcmp(name,"SceDiscPlayer") && suspends==1); lookups++; return !strcmp(mode,"disc-no-pid") ? -1 : 123; }
int launcher_test_kill(pid_t pid,int sig) { CHECK(pid==123 && sig==SIGKILL && suspends==1); signals++; return 0; }
unsigned int launcher_test_sleep(unsigned int seconds) { slept+=(int)seconds; return 0; }
static int disc_test(void) {
    int rc=ps5_kill_disc_player();
    int skipped=!strcmp(mode,"disc-none") || !strcmp(mode,"disc-other") || !strcmp(mode,"disc-title-error");
    int suspend_failed=!strcmp(mode,"disc-suspend-error");
    CHECK(rc== (suspend_failed || !strcmp(mode,"disc-kill-error") ? -1 : 0));
    CHECK(suspends==(skipped ? 0 : 1));
    CHECK(app_kills==(skipped || suspend_failed ? 0 : 1));
    CHECK(lookups==app_kills);
    CHECK(signals==(!strcmp(mode,"disc-no-pid") ? 0 : app_kills));
    CHECK(slept==2*app_kills+signals);
    printf("Disc Player: %s passed\n",mode); return 0;
}
static void *receive_bytes(void *unused) {
    (void)unused;
    for(;;) { ssize_t n=recv(peer,received+received_bytes,expected_size-received_bytes,0); if(n==0)break;CHECK(n>0);received_bytes+=(size_t)n;if(received_bytes==expected_size)break; }
    close(peer);return NULL;
}
int launcher_test_socket(int domain,int type,int protocol) {
    (void)domain;(void)type;(void)protocol;sockets++;
    if(!strcmp(mode,"socket")){errno=EMFILE;return -1;}
    int pair[2];CHECK(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));sock=pair[0];peer=pair[1];
    int size=4096;CHECK(!setsockopt(sock,SOL_SOCKET,SO_SNDBUF,&size,sizeof(size)));
    if(!strcmp(mode,"ok") || !strcmp(mode,"partial") || !strcmp(mode,"interrupt")) { has_receiver=1;CHECK(!pthread_create(&receiver,NULL,receive_bytes,NULL)); }
    return sock;
}
int launcher_test_fcntl(int fd,int command,...) { va_list args;va_start(args,command);int arg=va_arg(args,int);va_end(args);if(!strcmp(mode,"fcntl")){errno=EIO;return -1;}return fcntl(fd,command,arg); }
int launcher_test_connect(int fd,const struct sockaddr *address,socklen_t size) {
    (void)fd;(void)address;(void)size;
    if(!strcmp(mode,"connect")){errno=ECONNREFUSED;return -1;}
    if(!strcmp(mode,"connect-stall") || !strcmp(mode,"socket-error")){errno=EINPROGRESS;return -1;}
    return 0;
}
int launcher_test_getsockopt(int fd,int level,int name,void *out,socklen_t *size) { if(!strcmp(mode,"socket-error")){errno=EIO;return -1;}return getsockopt(fd,level,name,out,size); }
int launcher_test_poll(struct pollfd *fds,nfds_t count,int timeout) {
    if(!strcmp(mode,"connect-stall")){usleep((useconds_t)timeout*1000);return 0;}
    return poll(fds,count,timeout);
}
ssize_t launcher_test_send(int fd,const void *buffer,size_t size,int flags) {
    calls++;
    if(!strcmp(mode,"zero"))return 0;
    if(!strcmp(mode,"interrupt") && calls==1){errno=EINTR;return -1;}
    if(!strcmp(mode,"interrupt") && calls==2){errno=EAGAIN;return -1;}
    if((!strcmp(mode,"partial") || !strcmp(mode,"cancel-send")) && size>17)size=17;
    ssize_t n=send(fd,buffer,size,flags);if(n>0)sent_bytes+=(size_t)n;return n;
}
ssize_t launcher_test_read(int fd,void *buffer,size_t size) { if(!strcmp(mode,"read")){errno=EIO;return -1;}return read(fd,buffer,size); }
static int cancelled(void *context) { (void)context;return !strcmp(mode,"cancel-before") || (!strcmp(mode,"cancel-send") && sent_bytes>=68) || (!strcmp(mode,"cancel-wait") && now_ms()-started>=100); }
int main(int argc,char **argv) {
    CHECK(argc==3);mode=argv[1];fixture=argv[2];signal(SIGPIPE,SIG_IGN);
    if(!strncmp(mode,"disc-",5)) return disc_test();
    FILE *f=fopen(fixture,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>0);expected_size=(size_t)length;CHECK(!fclose(f));
    received=malloc(expected_size);CHECK(received);started=now_ms();
    int rc=ps5_launch_elf_cancellable(fixture,cancelled,NULL);
    long long elapsed=now_ms()-started;
    if(has_receiver)CHECK(!pthread_join(receiver,NULL));else if(peer>=0)close(peer);
    int success=!strcmp(mode,"ok") || !strcmp(mode,"partial") || !strcmp(mode,"interrupt");
    CHECK(success ? rc==0 && history==1 : rc==-1 && history==0);
    if(success) {
        CHECK(received_bytes==expected_size && sent_bytes==expected_size);
        FILE *original=fopen(fixture,"rb");CHECK(original);unsigned char *expected=malloc(expected_size);CHECK(expected);CHECK(fread(expected,1,expected_size,original)==expected_size);CHECK(!fclose(original));CHECK(!memcmp(expected,received,expected_size));free(expected);
    }
    if(!strcmp(mode,"cancel-before"))CHECK(!opened && !sockets);
    if(!strcmp(mode,"cancel-send"))CHECK(sent_bytes==68);
    if(!strcmp(mode,"cancel-wait"))CHECK(elapsed>=100 && elapsed<1000);
    if(!strcmp(mode,"connect-stall"))CHECK(elapsed>=200 && elapsed<1500);
    if(!strcmp(mode,"send-stall"))CHECK(elapsed>=1000 && elapsed<3000 && sent_bytes<expected_size);
    if(file>=0)CHECK(fcntl(file,F_GETFD)==-1 && errno==EBADF);
    if(sock>=0)CHECK(fcntl(sock,F_GETFD)==-1 && errno==EBADF);
    free(received);
    printf("Loader: %s passed (sent=%zu, history=%d, elapsed=%lldms)\n",mode,sent_bytes,history,elapsed);return 0;
}
