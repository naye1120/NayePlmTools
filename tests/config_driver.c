#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include "config.h"
#include "json_helpers.h"
#include "history_mgr.h"
#include "sources.h"
#include "pldmgr.h"
#include "slot.h"
#include "sha256.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static const char *fault_name;
static int fault_at, fault_hits;
static FILE *bad_read;
static _Atomic int aborts;
static int hit(const char *name) { if (fault_name && !strcmp(name,fault_name) && ++fault_hits == fault_at) { errno=ENOSPC; return 1; } return 0; }
size_t config_test_fwrite(const void *p,size_t a,size_t b,FILE *f) { if(hit("write")) return 0; return fwrite(p,a,b,f); }
size_t config_test_fread(void *p,size_t a,size_t b,FILE *f) { if(hit("read")) { bad_read=f; return 0; } return fread(p,a,b,f); }
int config_test_ferror(FILE *f) { return f==bad_read || ferror(f); }
int config_test_fflush(FILE *f) { int rc=fflush(f); return hit("flush") ? -1 : rc; }
int config_test_fclose(FILE *f) { if(f==bad_read)bad_read=NULL; int rc=fclose(f); return hit("close") ? -1 : rc; }
int config_test_fsync(int fd) { return hit("sync") ? -1 : fsync(fd); }
int config_test_rename(const char *a,const char *b) { return hit("rename") ? -1 : rename(a,b); }
int config_test_mkstemp(char *p) { return hit("create") ? -1 : mkstemp(p); }
void pldmgr_log(const char *fmt,...) { (void)fmt; }
void pldmgr_autoload_abort(void) { aborts++; }
int pldmgr_autoload_update_config_entry(const char *a,const char *b) { return config_update_list_entry(AUTOLOAD_CONFIG_PATH,a,b); }
static _Atomic int downloads;
int download_to_file_limited(const char *url,const char *path,size_t limit) {
    (void)limit; CHECK(strstr(url,"fixture://") || !strcmp(url, REPOSITORY_SOURCE_URL)); downloads++;
    const char *json="{\"name\":\"F\\u00edxture\",\"payloads\":[{\"id\":\"safe\",\"name\":\"Safe\",\"filename\":\"safe.elf\",\"url\":\"http://example.invalid/safe.elf\"}]}";
    FILE *f=fopen(path,"wb"); CHECK(f); CHECK(fwrite(json,1,strlen(json),f)==strlen(json)); CHECK(!fclose(f));
    usleep(1000); return 0;
}
int download_to_file(const char *url,const char *path) { return download_to_file_limited(url,path,0); }
static void fault(const char *name,int at) { fault_name=name; fault_at=at; fault_hits=0; }
static char *read_text(const char *path) { char *text; size_t size; CHECK(!read_file_text(path,&text,&size)); return text; }
static void expect_text(const char *path,const char *expected) { char *text=read_text(path); CHECK(!strcmp(text,expected)); free(text); }
static void scalar(void) {
    PldmgrConfig initial; config_read(&initial); CHECK(initial.kill_disc_player==1); CHECK(initial.show_dashboard_icons==1); CHECK(initial.autoload_return_to_dashboard==0);
    /* Legacy values survive unrelated saves; toggling writes one canonical key. */
    CHECK(!config_upsert_value("KILL_DISC_PLAYER_ON_STARTUP","0"));
    CHECK(config_read_bool("KILL_DISC_PLAYER_ON_STARTUP",1)==0);
    CHECK(config_read_bool("AUTOLOAD_ENABLED",1)==1);
    CHECK(!config_handle_set_json("{\"AUTO_BROWSER_OPEN\":false}"));
    PldmgrConfig cfg; config_read(&cfg); CHECK(cfg.auto_browser_open==0); CHECK(cfg.autoload_delay==5); CHECK(cfg.auto_install_app==1 && cfg.kill_disc_player==0);
    CHECK(!config_handle_set_json("{\"KILL_DISC_PLAYER_ON_STARTUP\":true}")); config_read(&cfg); CHECK(cfg.kill_disc_player==1 && cfg.auto_browser_open==0);
    CHECK(!config_handle_set_json("{\"KILL_DISC_PLAYER_ON_STARTUP\":false,\"FAVORITES\":\"local/keep\",\"AUTOLOAD_LIST\":\"local/keep\"}"));
    config_read(&cfg); CHECK(cfg.kill_disc_player==0);
    CHECK(config_read_bool("KILL_DISC_PLAYER_ON_STARTUP",1)==0);
    char *disc_saved=read_text(PLDMGR_CONFIG_PATH);
    char *key=strstr(disc_saved,"KILL_DISC_PLAYER_ON_STARTUP=0\n"); CHECK(key && !strstr(key+1,"KILL_DISC_PLAYER_ON_STARTUP=")); free(disc_saved);
    CHECK(!config_handle_set_json("{\"AUTOLOAD_RETURN_TO_DASHBOARD\":true}"));
    config_read(&cfg); CHECK(cfg.autoload_return_to_dashboard==1);
    CHECK(!config_handle_set_json("{\"AUTOLOAD_DELAY\":5}"));
    config_read(&cfg); CHECK(cfg.autoload_return_to_dashboard==1);
    CHECK(!config_handle_set_json("{\"AUTOLOAD_RETURN_TO_DASHBOARD\":false}"));
    config_read(&cfg); CHECK(cfg.autoload_return_to_dashboard==0);
    CHECK(!config_handle_set_json("{\"SHOW_DASHBOARD_ICONS\":false}"));
    config_read(&cfg); CHECK(cfg.show_dashboard_icons==0);
    CHECK(!config_handle_set_json("{\"AUTOLOAD_DELAY\":10}"));
    config_read(&cfg); CHECK(cfg.show_dashboard_icons==0);
    CHECK(!config_handle_set_json("{\"SHOW_DASHBOARD_ICONS\":true}"));
    config_read(&cfg); CHECK(cfg.show_dashboard_icons==1);
    CHECK(!config_handle_set_json("{\"SCAN_USB_PAYLOADS\":true}")); config_read(&cfg); CHECK(cfg.scan_usb_payloads==1 && cfg.auto_browser_open==0);
    CHECK(!config_handle_set_json("{\"AUTO_INSTALL_APP\":false}")); config_read(&cfg); CHECK(cfg.auto_install_app==0 && cfg.scan_usb_payloads==1);
    CHECK(!config_handle_set_json("{\"MULTI_SOURCES_ENABLED\":true}")); config_read(&cfg); CHECK(cfg.multi_sources_enabled==1);
    CHECK(!config_handle_set_json("{\"AUTOLOAD_DELAY\":0}")); config_read(&cfg); CHECK(cfg.autoload_delay==0);
    CHECK(!config_write_last_update(123456));
    CHECK(!config_handle_set_json("{\"AUTOLOAD_ENABLED\":true}")); config_read(&cfg); CHECK(cfg.autoload_enabled==1 && cfg.last_repository_update==123456 && cfg.autoload_delay==0);
    CHECK(!config_handle_set_json("{\"AUTOLOAD_ENABLED\":false}")); CHECK(aborts==1);
    CHECK(!config_handle_set_json("{\"nested\":{\"AUTOLOAD_DELAY\":20},\"FAVORITES_INITIALIZED\":true}")); config_read(&cfg); CHECK(cfg.autoload_delay==0);
    CHECK(!config_handle_set_json("{\"AUTOLOAD_LIST\":\"local/żółw,!1000,local/next\",\"FAVORITES\":[\"local/żółw\",\"local/a\\\"b\"]}"));
    expect_text(AUTOLOAD_CONFIG_PATH,"local/żółw\n!1000\nlocal/next\n");
    expect_text(FAVORITES_CONFIG_PATH,"local/żółw\nlocal/a\"b\n");
    CHECK(!config_handle_set_json("{\"FAVORITES\":\"\",\"AUTOLOAD_LIST\":\"\"}"));
    expect_text(AUTOLOAD_CONFIG_PATH,""); expect_text(FAVORITES_CONFIG_PATH,"");
    CHECK(!config_handle_set_json("{\"AUTOLOAD_DELAY\":3600}"));
    CHECK(!config_upsert_value("CUSTOM", "keep")); CHECK(!config_write_last_update(999)); CHECK(!config_handle_set_json("{\"AUTOLOAD_DELAY\":5}"));
    char *saved=read_text(PLDMGR_CONFIG_PATH); CHECK(strstr(saved,"CUSTOM=keep\n")); free(saved);
    puts("Config: individual settings, false/zero, timestamps, Unicode, empty and array lists passed");
}
static void validation(void) {
    CHECK(!config_handle_set_json("{\"AUTOLOAD_ENABLED\":true,\"FAVORITES\":\"local/keep\",\"AUTOLOAD_LIST\":\"local/keep\"}"));
    char *old=read_text(PLDMGR_CONFIG_PATH);
    const char *invalid[]={"{\"AUTOLOAD_RETURN_TO_DASHBOARD\":0}", "{\"AUTOLOAD_RETURN_TO_DASHBOARD\":null}", "{\"AUTOLOAD_RETURN_TO_DASHBOARD\":true,\"AUTOLOAD_RETURN_TO_DASHBOARD\":false}", "{\"SHOW_DASHBOARD_ICONS\":0}", "{\"SHOW_DASHBOARD_ICONS\":null}", "{\"SHOW_DASHBOARD_ICONS\":true,\"SHOW_DASHBOARD_ICONS\":false}", "{\"KILL_DISC_PLAYER_ON_STARTUP\":0}", "{\"KILL_DISC_PLAYER_ON_STARTUP\":null}", "{\"KILL_DISC_PLAYER_ON_STARTUP\":true,\"KILL_DISC_PLAYER_ON_STARTUP\":false}", "{", "{} garbage", "[]", "{\"AUTOLOAD_ENABLED\":false,\"AUTOLOAD_DELAY\":-1}",
        "{\"FAVORITES\":\"local/change\",\"AUTO_BROWSER_OPEN\":0}", "{\"AUTOLOAD_ENABLED\":null}", "{\"AUTOLOAD_ENABLED\":\"true\"}",
        "{\"AUTOLOAD_DELAY\":1.5}", "{\"AUTOLOAD_DELAY\":1e2}", "{\"AUTOLOAD_DELAY\":3601}", "{\"AUTOLOAD_DELAY\":999999999999999999999999}",
        "{\"AUTOLOAD_ENABLED\":true,\"AUTOLOAD_ENABLED\":false}", "{\"AUTOLOAD_DELAY\":1,\"AUTOLOAD_D\\u0045LAY\":2}",
        "{\"FAVORITES\":[1]}", "{\"FAVORITES\":[\"local/a,b\"]}", "{\"FAVORITES\":null}", "{\"AUTOLOAD_LIST\":\"a\\nb\"}",
        "{\"FAVORITES\":\"a\\u0000b\"}", "{\"FAVORITES\":\"a\\u0001b\"}", "{\"FAVORITES\":\"a\",\"FAVORITES\":\"b\"}"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        CHECK(config_handle_set_json(invalid[i])==CONFIG_INVALID);
        expect_text(PLDMGR_CONFIG_PATH,old); expect_text(FAVORITES_CONFIG_PATH,"local/keep\n"); expect_text(AUTOLOAD_CONFIG_PATH,"local/keep\n"); CHECK(aborts==0);
    }
    char huge[4200]; memset(huge,'a',sizeof(huge)); memcpy(huge,"{\"FAVORITES\":\"",14); memcpy(huge+sizeof(huge)-3,"\"}",3);
    CHECK(config_handle_set_json(huge)==CONFIG_INVALID); free(old);
    CHECK(!sources_save("{\"sources\":[]}",14)); char *sources=read_text(SOURCES_CONFIG_PATH);
    const char *bad[]={"{\"sources\":[]", "{\"sources\":[],\"sources\":[]}", "{\"sources\":[{}]}", "{\"nested\":{\"sources\":[]}}", "{\"sources\":null}", "{\"sources\":[{\"id\":\"x\",\"name\":\"X\",\"url\":false}]}"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) { CHECK(sources_save(bad[i],strlen(bad[i]))==CONFIG_INVALID); expect_text(SOURCES_CONFIG_PATH,sources); }
    CHECK(!config_handle_set_json("{\"MULTI_SOURCES_ENABLED\":true}"));
    const char *custom_init = "{\"sources\":[{\"id\":\"source_custom\",\"name\":\"Old Name\",\"url\":\"fixture://custom-rename\",\"removable\":true}]}";
    CHECK(!sources_save(custom_init, strlen(custom_init)));
    char repolist[16384];
    CHECK(sources_multi_repository_list_json(repolist, sizeof(repolist), 1) > 0);
    CHECK(strstr(repolist, "Fíxture"));
    CHECK(!strstr(repolist, "Old Name"));
    char src_out[4096];
    CHECK(!sources_list_json(src_out, sizeof(src_out)));
    CHECK(strstr(src_out, "Fíxture"));
    CHECK(!strstr(src_out, "Old Name"));
    CHECK(!sources_save("{\"sources\":[]}", 14));
    free(sources); puts("Config/source validation: malformed input rejected without writes or aborts");
}
static void atomic_files(void) {
    char path[512]; snprintf(path,sizeof(path),"%s/atomic.txt",BASE_DATA_DIR);
    CHECK(!write_file_text(path,"old",3));
    const char *operations[]={"create","write","flush","sync","close","rename"};
    for(size_t i=0;i<sizeof(operations)/sizeof(*operations);i++) { fault(operations[i],1); CHECK(write_file_text(path,"new",3)==-1); fault(NULL,0); expect_text(path,"old"); }
    fault("read",1); char *text=(char *)1; size_t size=1; CHECK(read_file_text(path,&text,&size)==-1 && text==NULL && size==0); fault(NULL,0);
    fault("close",1); CHECK(read_file_text(path,&text,&size)==-1 && text==NULL && size==0); fault(NULL,0);
    CHECK(!config_handle_set_json("{\"AUTOLOAD_ENABLED\":false,\"FAVORITES\":\"old\",\"AUTOLOAD_LIST\":\"old\"}"));
    fault("rename",2); CHECK(config_handle_set_json("{\"AUTOLOAD_ENABLED\":true,\"FAVORITES\":\"new\",\"AUTOLOAD_LIST\":\"new\"}")==-1); fault(NULL,0);
    expect_text(AUTOLOAD_CONFIG_PATH,"new\n"); expect_text(FAVORITES_CONFIG_PATH,"old\n"); PldmgrConfig cfg; config_read(&cfg); CHECK(!cfg.autoload_enabled);
    CHECK(!config_handle_set_json("{\"FAVORITES\":\"a,b,c\"}")); fault("rename",1); CHECK(pldmgr_favorites_update_entry("b","longer")==-1); fault(NULL,0); expect_text(FAVORITES_CONFIG_PATH,"a\nb\nc\n");
    CHECK(!pldmgr_favorites_update_entry("b","longer")); expect_text(FAVORITES_CONFIG_PATH,"a\nlonger\nc\n");
    CHECK(!pldmgr_favorites_update_entry("a",NULL)); expect_text(FAVORITES_CONFIG_PATH,"longer\nc\n");
    CHECK(!sources_save("{\"sources\":[]}",14));
    char *source_old=read_text(SOURCES_CONFIG_PATH);
    const char *source_new="{\"sources\":[{\"id\":\"test\",\"name\":\"Test\",\"url\":\"fixture://test\"}]}";
    fault("rename",1); CHECK(sources_save(source_new,strlen(source_new))==-1); fault(NULL,0); expect_text(SOURCES_CONFIG_PATH,source_old);
    CHECK(!sources_save(source_new,strlen(source_new))); free(source_old); source_old=read_text(SOURCES_CONFIG_PATH);
    char message[256]; fault("rename",1); CHECK(sources_remove(1,message,sizeof(message))==-1); fault(NULL,0); expect_text(SOURCES_CONFIG_PATH,source_old);
    free(source_old);
    char *config_old=read_text(PLDMGR_CONFIG_PATH);
    fault("rename",1); CHECK(config_handle_set_json("{\"SCAN_USB_PAYLOADS\":true}")==-1); fault(NULL,0); expect_text(PLDMGR_CONFIG_PATH,config_old); free(config_old);
    DIR *dir=opendir(BASE_DATA_DIR); CHECK(dir); struct dirent *entry; while((entry=readdir(dir))) CHECK(!strstr(entry->d_name,".tmp-")); closedir(dir);
    puts("Atomic persistence: create/write/flush/sync/close/rename/read failures preserve old files; multi-file policy and remaps passed");
}
static pthread_mutex_t gate_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t gate_condition = PTHREAD_COND_INITIALIZER;
static int gate_count;
static void start_together(void) {
    pthread_mutex_lock(&gate_mutex);
    if (++gate_count == 8) pthread_cond_broadcast(&gate_condition);
    else while (gate_count < 8) pthread_cond_wait(&gate_condition,&gate_mutex);
    pthread_mutex_unlock(&gate_mutex);
}
static void *settings_worker(void *argument) {
    long id=(long)argument; start_together();
    for(int n=0;n<200;n++) {
        switch(id) {
        case 0: CHECK(!config_handle_set_json("{\"AUTO_BROWSER_OPEN\":false}")); break;
        case 1: CHECK(!config_handle_set_json("{\"SCAN_USB_PAYLOADS\":true}")); break;
        case 2: CHECK(!config_handle_set_json("{\"AUTO_INSTALL_APP\":false}")); break;
        case 3: CHECK(!config_write_last_update(777777)); break;
        case 4: CHECK(!config_handle_set_json("{\"FAVORITES\":\"local/a,local/b,local/c\"}")); break;
        case 5: CHECK(!config_handle_set_json("{\"AUTOLOAD_LIST\":\"local/d,!1000,local/e\"}")); break;
        case 6: CHECK(!config_update_list_entry(FAVORITES_CONFIG_PATH,"missing","unused")); break;
        case 7: { PldmgrConfig cfg; config_read(&cfg); CHECK(cfg.autoload_delay==5); char out[4096]; int exists; CHECK(!config_get_favorites_csv(out,sizeof(out),&exists)); break; }
        }
    } return NULL;
}
static void parallel_settings(void) {
    pthread_t threads[8];
    for(long i=0;i<8;i++) CHECK(!pthread_create(&threads[i],NULL,settings_worker,(void *)i));
    for(int i=0;i<8;i++) CHECK(!pthread_join(threads[i],NULL));
    PldmgrConfig cfg; config_read(&cfg); CHECK(!cfg.auto_browser_open && cfg.scan_usb_payloads && !cfg.auto_install_app && cfg.last_repository_update==777777);
    expect_text(FAVORITES_CONFIG_PATH,"local/a\nlocal/b\nlocal/c\n"); expect_text(AUTOLOAD_CONFIG_PATH,"local/d\n!1000\nlocal/e\n");
    puts("Config concurrency: 1,600 updates/remaps/reads preserve independent fields, timestamps and lists");
}
typedef struct { int count; char entries[20][512]; } HistoryCheck;
static int history_item(const char *a,const char *b,void *context) {
    HistoryCheck *check=context; if(check->count==20) return -1;
    char *path=check->entries[check->count]; if(json_decode_string(a,b,path,512))return -1;
    for(int i=0;i<check->count;i++)if(!strcmp(check->entries[i],path))return -1;
    check->count++; return 0;
}
static int history_member(const char *key,const char *a,const char *b,void *context) { return strcmp(key,"history") ? -1 : json_visit_array(a,b,history_item,context); }
static void *history_worker(void *argument) {
    long id=(long)argument;
    for(int i=0;i<2000;i++) {
        if(id<4) { char path[512]; snprintf(path,sizeof(path),"local/thread-%ld-item-%d",id,i%25); history_mgr_add(path); }
        else { char json[65536]; size_t n=history_mgr_to_json(json,sizeof(json)); CHECK(n); HistoryCheck check={0}; CHECK(!json_visit_object(json,json+n,history_member,&check)); }
    } return NULL;
}
static void parallel_history(void) {
    pthread_t threads[8]; for(long i=0;i<8;i++)CHECK(!pthread_create(&threads[i],NULL,history_worker,(void *)i));
    for(int i=0;i<8;i++)CHECK(!pthread_join(threads[i],NULL));
    for(int i=0;i<25;i++) { char path[64]; snprintf(path,sizeof(path),"local/item-%d",i); history_mgr_add(path); }
    history_mgr_add("local/item-10"); char json[65536]; size_t size=history_mgr_to_json(json,sizeof(json)); CHECK(size); puts(json);
    HistoryCheck check={0}; CHECK(!json_visit_object(json,json+size,history_member,&check)); CHECK(check.count==20 && !strcmp(check.entries[0],"local/item-10") && !strcmp(check.entries[1],"local/item-24"));
    char long_path[600]; memset(long_path,'x',599); long_path[599]=0; history_mgr_add(long_path); history_mgr_add(long_path);
    size=history_mgr_to_json(json,sizeof(json)); check=(HistoryCheck){0}; CHECK(!json_visit_object(json,json+size,history_member,&check)); CHECK(check.count==20);
    puts("History concurrency: 8,000 launches and 8,000 snapshots preserve bounds/uniqueness; deterministic ordering passed");
}
static void *source_worker(void *argument) { char url[128],msg[256]; snprintf(url,sizeof(url),"fixture://source-%ld",(long)argument); CHECK(!sources_add(url,msg,sizeof(msg))); return NULL; }
static void parallel_sources(void) {
    CHECK(!sources_save("{\"sources\":[]}",14)); pthread_t threads[12];
    for(long i=0;i<12;i++)CHECK(!pthread_create(&threads[i],NULL,source_worker,(void *)i));
    for(int i=0;i<12;i++)CHECK(!pthread_join(threads[i],NULL));
    char json[20000]; CHECK(!sources_list_json(json,sizeof(json))); puts(json); CHECK(downloads==12);
    for(int i=0;i<12;i++) { char url[128]; snprintf(url,sizeof(url),"fixture://source-%d",i); CHECK(strstr(json,url)); }
    for(int i=0;i<12;i++) { char msg[128]; CHECK(!sources_remove(1,msg,sizeof(msg))); }
    CHECK(!sources_list_json(json,sizeof(json))); CHECK(!strstr(json,"fixture://")); puts("Source concurrency: 12 simultaneous validations/adds retained; removals passed");
}
int main(int argc,char **argv) {
    CHECK(argc==2); CHECK(!ensure_dir_recursive(BASE_DATA_DIR));
    if(!strcmp(argv[1],"scalar"))scalar(); else if(!strcmp(argv[1],"validation"))validation();
    else if(!strcmp(argv[1],"atomic"))atomic_files(); else if(!strcmp(argv[1],"settings"))parallel_settings();
    else if(!strcmp(argv[1],"history"))parallel_history(); else if(!strcmp(argv[1],"sources"))parallel_sources(); else return 2;
    return 0;
}
