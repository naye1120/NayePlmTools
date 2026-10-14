#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include "pldmgr.h"
#include "autoload.h"
#include "config.h"
#include "json_helpers.h"
#include "ps5_launcher.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static _Atomic int launches, blocked, released, entered, fail_thread, fail_alloc, created, joined;
void *autoload_test_calloc(size_t count,size_t size) { return fail_alloc ? NULL : calloc(count,size); }
int autoload_test_pthread_create(pthread_t *thread,const pthread_attr_t *attr,void *(*fn)(void *),void *arg) {
    if(fail_thread) return EAGAIN;
    int rc=pthread_create(thread,attr,fn,arg); if(!rc)created++; return rc;
}
int autoload_test_pthread_join(pthread_t thread,void **result) { int rc=pthread_join(thread,result); if(!rc)joined++;return rc; }
void pldmgr_log(const char *fmt,...) { (void)fmt; }
void pldmgr_notify(const char *fmt,...) { (void)fmt; }
int pldmgr_server_is_active(void) { return 1; }
int slot_resolve(const char *key,char *out,size_t size) {
    if(!strcmp(key,"local/missing"))return -1;
    snprintf(out,size,"/fixture/%s",key);return 0;
}
int ps5_launch_elf_cancellable(const char *path,PayloadLaunchCancelled cancelled,void *context) {
    entered++;
    while(blocked && !released) { if(cancelled && cancelled(context))return -1;usleep(1000); }
    if(cancelled && cancelled(context))return -1;
    if(strstr(path,"fail"))return -1;
    launches++;return 0;
}
int ps5_launch_elf(const char *path) { return ps5_launch_elf_cancellable(path,NULL,NULL); }
static long long now_ms(void) { struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
static void setup(const char *list,int browser,int delay) {
    PldmgrConfig cfg={.autoload_enabled=1,.auto_browser_open=browser,.autoload_delay=delay};
    CHECK(!config_write(&cfg)); CHECK(!write_file_text(AUTOLOAD_CONFIG_PATH,list,strlen(list)));
}
static AutoloadSnapshot wait_state(AutoloadState wanted) {
    long long end=now_ms()+6000; AutoloadSnapshot snapshot;
    do { pldmgr_autoload_get_snapshot(&snapshot);if(snapshot.state==wanted)return snapshot;usleep(1000); } while(now_ms()<end);
    fprintf(stderr,"wanted state %d, got %d (%s)\n",wanted,snapshot.state,snapshot.current);exit(1);
}
static void wait_entered(void) { long long end=now_ms()+2000;while(!entered && now_ms()<end)usleep(1000);CHECK(entered); }
static void counters(void) {
    setup("\r\n   !0\r\n\t\nlocal/ok\r\n   pldmgr.elf \nlocal/missing\nlocal/fail",0,0);
    CHECK(!pldmgr_autoload_start()); AutoloadSnapshot snapshot=wait_state(AUTOLOAD_DONE);
    CHECK(snapshot.total==4); CHECK(snapshot.done==4 && snapshot.successful==1 && snapshot.failed==2 && snapshot.skipped==1);
    CHECK(!strcmp(snapshot.list,"!0,local/ok,pldmgr.elf,local/missing,local/fail")); CHECK(!strcmp(snapshot.outcomes,"skff")); CHECK(launches==1);
    CHECK(!pldmgr_autoload_start()); CHECK(created==1);pldmgr_autoload_reset();CHECK(!pldmgr_autoload_start());CHECK(created==1 && launches==1);
    pldmgr_autoload_stop();CHECK(joined==1);
    puts("Autoload counters: CRLF/blanks/indented delays/final line, skip/fail/success, repeat start and terminal reset passed");
}
static void active_reset(void) {
    setup("local/ok",1,1);CHECK(!pldmgr_autoload_start());
    AutoloadSnapshot snapshot=wait_state(AUTOLOAD_WAITING);
    /* WAITING is initially visible before config loading; wait for parsed data. */
    long long end=now_ms()+2000;while(snapshot.total!=1 && now_ms()<end){usleep(1000);pldmgr_autoload_get_snapshot(&snapshot);}CHECK(snapshot.total==1);
    pldmgr_autoload_reset(); pldmgr_autoload_get_snapshot(&snapshot);CHECK(snapshot.state==AUTOLOAD_WAITING && snapshot.total==1);
    pldmgr_autoload_frontend_ready(); snapshot=wait_state(AUTOLOAD_COUNTDOWN);CHECK(snapshot.remaining>0);
    long long first=snapshot.remaining_ms;usleep(20000);pldmgr_autoload_reset();pldmgr_autoload_get_snapshot(&snapshot);CHECK(snapshot.state==AUTOLOAD_COUNTDOWN && snapshot.remaining_ms<=first);
    CHECK(!pldmgr_autoload_start());CHECK(created==1);pldmgr_autoload_abort();wait_state(AUTOLOAD_ABORTED);pldmgr_autoload_stop();CHECK(!launches && joined==1);
    puts("Autoload active reset: waiting/countdown survive reset, deadline advances, abort prevents launch");
}
static AutoloadSnapshot wait_entry(int index) {
    long long end=now_ms()+3000; AutoloadSnapshot snapshot;
    do { pldmgr_autoload_get_snapshot(&snapshot);if(snapshot.entry_index==index)return snapshot;usleep(1000); } while(now_ms()<end);
    CHECK(snapshot.entry_index==index);return snapshot;
}
static void delay_progress(void) {
    blocked=1;setup("!600\n!800\n!0\nlocal/ok",0,0);CHECK(!pldmgr_autoload_start());
    AutoloadSnapshot snapshot=wait_entry(0);
    CHECK(!strcmp(snapshot.list,"!600,!800,!0,local/ok") && snapshot.total==1 && snapshot.done==0);
    CHECK(snapshot.entry_duration_ms==600 && snapshot.entry_remaining_ms>0 && snapshot.entry_remaining_ms<=600);
    long long first=snapshot.entry_remaining_ms;usleep(50000);pldmgr_autoload_reset();pldmgr_autoload_get_snapshot(&snapshot);
    CHECK(snapshot.entry_index==0 && snapshot.entry_remaining_ms<first && snapshot.entry_remaining_ms>0);
    snapshot=wait_entry(1);CHECK(snapshot.entry_duration_ms==800 && snapshot.entry_remaining_ms>600 && snapshot.done==0);
    wait_entered();pldmgr_autoload_get_snapshot(&snapshot);
    CHECK(snapshot.entry_index==3 && snapshot.entry_duration_ms==0 && snapshot.entry_remaining_ms==0);
    released=1;wait_state(AUTOLOAD_DONE);pldmgr_autoload_stop();
    setup("!0\n!5000",0,0);CHECK(!pldmgr_autoload_start());snapshot=wait_entry(1);
    CHECK(snapshot.total==0 && snapshot.done==0 && snapshot.entry_duration_ms==5000);
    pldmgr_autoload_abort();snapshot=wait_state(AUTOLOAD_ABORTED);
    CHECK(snapshot.entry_index==1 && snapshot.entry_remaining_ms>0 && snapshot.entry_remaining_ms<=5000);
    first=snapshot.entry_remaining_ms;usleep(20000);pldmgr_autoload_get_snapshot(&snapshot);CHECK(snapshot.entry_remaining_ms==first);
    pldmgr_autoload_stop();pldmgr_autoload_reset();pldmgr_autoload_get_snapshot(&snapshot);
    CHECK(snapshot.entry_index==-1 && snapshot.entry_remaining_ms==0 && snapshot.entry_duration_ms==0);
    setup("!0\n!50",0,0);CHECK(!pldmgr_autoload_start());snapshot=wait_state(AUTOLOAD_DONE);
    CHECK(snapshot.total==0 && snapshot.done==0 && snapshot.entry_index==1 && snapshot.entry_remaining_ms==0);pldmgr_autoload_stop();
    puts("Autoload delay progress: exact sequence indices, decreasing deadlines, consecutive/zero delays, reset and frozen abort status passed");
}
static void cancellable(const char *which) {
    if(!strcmp(which,"wait")) { setup("local/ok",1,0);CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_WAITING); }
    else if(!strcmp(which,"delay")) { setup("!60000\nlocal/ok",0,0);CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_EXECUTING); }
    else { blocked=1;setup("local/ok\nlocal/next",0,0);CHECK(!pldmgr_autoload_start());wait_entered(); }
    pldmgr_autoload_reset();
    long long begin=now_ms(); pldmgr_autoload_stop();CHECK(now_ms()-begin<1000);AutoloadSnapshot snapshot;pldmgr_autoload_get_snapshot(&snapshot);CHECK(snapshot.state==AUTOLOAD_ABORTED && !launches && joined==1);
    CHECK(!pldmgr_autoload_start()); /* Explicit stop/start permits a fresh run. */
    pldmgr_autoload_stop();CHECK(created==2 && joined==2);
    printf("Autoload stop: %s cancels/joins within 1s and never launches a later entry\n",which);
}
static void snapshot_file(void) {
    blocked=1;setup("local/ok\nlocal/next",0,0);CHECK(!pldmgr_autoload_start());wait_entered();
    CHECK(!write_file_text(AUTOLOAD_CONFIG_PATH,"local/changed",13));
    AutoloadSnapshot snapshot;pldmgr_autoload_get_snapshot(&snapshot);CHECK(snapshot.total==2 && !strcmp(snapshot.list,"local/ok,local/next"));
    pldmgr_autoload_reset();pldmgr_autoload_get_snapshot(&snapshot);CHECK(snapshot.state==AUTOLOAD_EXECUTING && snapshot.total==2);
    released=1;snapshot=wait_state(AUTOLOAD_DONE);CHECK(launches==2 && snapshot.done==2);pldmgr_autoload_stop();CHECK(joined==1);
    puts("Autoload snapshot: execution/reset and sequence edits cannot replace a running snapshot");
}
static void invalid(void) {
    const char *bad[]={"!", "!-1", "!1x", "!3600001", "!999999999999999999999999"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) { setup(bad[i],0,0);CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_FAILED);pldmgr_autoload_stop(); }
    setup("local/ok",0,0);unlink(AUTOLOAD_CONFIG_PATH);CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_DISABLED);pldmgr_autoload_stop();
    PldmgrConfig cfg={0};CHECK(!config_write(&cfg));CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_DISABLED);pldmgr_autoload_stop();
    char large[4200];memset(large,'a',sizeof(large));CHECK(!write_file_text(AUTOLOAD_CONFIG_PATH,large,sizeof(large)));
    cfg.autoload_enabled=1;CHECK(!config_write(&cfg));CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_FAILED);pldmgr_autoload_stop();
    CHECK(!write_file_text(AUTOLOAD_CONFIG_PATH,"a\0b",3));CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_FAILED);pldmgr_autoload_stop();
    CHECK(!launches && created==joined); puts("Autoload early exits: malformed delays, missing/disabled sequence all join without launches");
}
static _Atomic int polling;
static void *poll_worker(void *unused) {
    (void)unused;
    while(polling) {
        AutoloadSnapshot snapshot;pldmgr_autoload_get_snapshot(&snapshot);
        CHECK(snapshot.done<=snapshot.total && snapshot.done==snapshot.successful+snapshot.failed+snapshot.skipped);
        CHECK(strlen(snapshot.outcomes)==(size_t)snapshot.total);
        if(snapshot.state==AUTOLOAD_DONE)CHECK(snapshot.done==snapshot.total);
        pldmgr_autoload_reset();pldmgr_autoload_frontend_ready();CHECK(!pldmgr_autoload_start());
    } return NULL;
}
static void concurrency(void) {
    blocked=1;setup("local/ok\nlocal/next",0,0);CHECK(!pldmgr_autoload_start());wait_entered();polling=1;
    pthread_t readers[6];for(int i=0;i<6;i++)CHECK(!pthread_create(&readers[i],NULL,poll_worker,NULL));
    usleep(50000);released=1;
    /* Readers can clear terminal display, so completion is evidenced by launches
     * and the owned worker being joined, rather than a transient terminal state. */
    long long end=now_ms()+3000;while(launches<2 && now_ms()<end)usleep(1000);CHECK(launches==2);
    polling=0;for(int i=0;i<6;i++)CHECK(!pthread_join(readers[i],NULL));pldmgr_autoload_stop();CHECK(created==1 && joined==1);
    puts("Autoload concurrency: six simultaneous snapshot/reset/readiness/start clients retain coherent counters and one worker");
}
static void unicode_name(void) {
    char key[256]="local/";
    for(int i=0;i<40;i++)strcat(key,"😀");
    blocked=1;setup(key,0,0);CHECK(!pldmgr_autoload_start());wait_entered();
    AutoloadSnapshot snapshot;pldmgr_autoload_get_snapshot(&snapshot);
    char escaped[1024];CHECK(!pldmgr_json_escape(snapshot.current,escaped,sizeof(escaped)));
    CHECK(!strcmp(snapshot.list,key) && strlen(snapshot.current)<=127);
    released=1;wait_state(AUTOLOAD_DONE);pldmgr_autoload_stop();
    setup("a",0,0);CHECK(!pldmgr_autoload_start());snapshot=wait_state(AUTOLOAD_DONE);CHECK(snapshot.total==1 && snapshot.done==1);pldmgr_autoload_stop();
    puts("Autoload bounds: long Unicode current names retain complete scalars, one-character final entries count");
}
static void allocation_failure(void) {
    setup("local/ok",0,0);fail_alloc=1;CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_FAILED);pldmgr_autoload_stop();CHECK(!launches && created==1 && joined==1);
    fail_alloc=0;CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_DONE);pldmgr_autoload_stop();CHECK(launches==1 && created==2 && joined==2);
    puts("Autoload allocation failure: sequence is rejected and joined; a fresh lifecycle can retry");
}
static void frontend_timeout(void) {
    setup("local/ok",1,0);long long begin=now_ms();CHECK(!pldmgr_autoload_start());
    wait_state(AUTOLOAD_DONE);pldmgr_autoload_stop();CHECK(now_ms()-begin>=2000 && launches==1);
    puts("Autoload frontend timeout: one sequence starts without a frontend after the bounded wait");
}
static void thread_failure(void) {
    setup("local/ok",0,0);fail_thread=1;CHECK(pldmgr_autoload_start()==-1);wait_state(AUTOLOAD_FAILED);pldmgr_autoload_stop();
    fail_thread=0;CHECK(!pldmgr_autoload_start());wait_state(AUTOLOAD_DONE);pldmgr_autoload_stop();CHECK(created==1 && joined==1 && launches==1);
    puts("Autoload thread failure: retry succeeds with no leaked or falsely owned thread");
}
int main(int argc,char **argv) {
    CHECK(argc==2);CHECK(!ensure_dir_recursive(BASE_DATA_DIR));
    if(!strcmp(argv[1],"counters"))counters();else if(!strcmp(argv[1],"reset"))active_reset();
    else if(!strcmp(argv[1],"wait")||!strcmp(argv[1],"delay")||!strcmp(argv[1],"send"))cancellable(argv[1]);
    else if(!strcmp(argv[1],"progress"))delay_progress();else if(!strcmp(argv[1],"snapshot"))snapshot_file();else if(!strcmp(argv[1],"invalid"))invalid();
    else if(!strcmp(argv[1],"concurrency"))concurrency();else if(!strcmp(argv[1],"thread"))thread_failure();else if(!strcmp(argv[1],"unicode"))unicode_name();else if(!strcmp(argv[1],"allocation"))allocation_failure();else if(!strcmp(argv[1],"timeout"))frontend_timeout();else return 2;
    return 0;
}
