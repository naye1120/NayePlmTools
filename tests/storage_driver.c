#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pthread.h>
#include <stdarg.h>
#include "slot.h"
#include "payload_mgr.h"
#include "sources.h"
#include "pldmgr.h"
#include "sha256.h"
#include "utils.h"
#include "config.h"
#include "json_helpers.h"

static const char *fail_op;
static int fail_at, hits, triggered, crash_phase, double_rename_failure;
static int usb_after_commit;
static char input[640],usb[640];
static FILE *read_error;
static FILE *journal_file;
static int checks;
static int autoload_updates;
static char context[128];
#define CHECK(x) do { checks++; if (!(x)) { fprintf(stderr,"FAIL line %d: %s (fault %s/%d hits %d)\n",__LINE__,#x,fail_op?fail_op:"none",fail_at,hits); exit(1); } } while(0)
static int fault(const char *name) {
    if (fail_op && !strcmp(fail_op,name) && ++hits == fail_at) { triggered=1; errno=ENOSPC; return 1; }
    return 0;
}
size_t storage_test_fread(void *p,size_t s,size_t n,FILE *f) {
    if (fault("read")) { read_error=f; return 0; } return fread(p,s,n,f);
}
size_t storage_test_fwrite(const void *p,size_t s,size_t n,FILE *f) {
    size_t rc=fault("write") ? 0 : fwrite(p,s,n,f);
    if(crash_phase==3) _exit(79);
    return rc;
}
int storage_test_ferror(FILE *f) { return f==read_error || ferror(f); }
int storage_test_fflush(FILE *f) { int rc=fflush(f); return fault("flush") ? -1 : rc; }
int storage_test_fclose(FILE *f) {
    int journal=f==journal_file;
    if(f==read_error)read_error=NULL;
    if(journal)journal_file=NULL;
    int rc=fclose(f); if(journal && crash_phase==4)_exit(80);
    return fault("fclose") ? -1 : rc;
}
int storage_test_fsync(int fd) { return fault("sync") ? -1 : fsync(fd); }
int storage_test_close(int fd) { int rc=close(fd); return fault("close") ? -1 : rc; }
int storage_test_mkdir(const char *p,mode_t mode) { return fault("mkdir") ? -1 : mkdir(p,mode); }
char *storage_test_mkdtemp(char *p) { return fault("mkdtemp") ? NULL : mkdtemp(p); }
int storage_test_mkstemp(char *p) { return fault("mkstemp") ? -1 : mkstemp(p); }
FILE *storage_test_fopen(const char *p,const char *m) {
    FILE *f=fault("open") ? NULL : fopen(p,m);
    if(crash_phase==4 && strlen(p)>7 && !strcmp(p+strlen(p)-7,"/target") && m[0]=='w')journal_file=f;
    return f;
}
int storage_test_rename(const char *a,const char *b) {
    if (fault("rename") || (double_rename_failure && fail_op && !strcmp(fail_op,"rename") && hits==fail_at+1)) return -1;
    int rc=rename(a,b);
    if (!rc && usb_after_commit && strlen(a)>4 && !strcmp(a+strlen(a)-4,"/new")) {
        if (usb_after_commit==1) unlink(usb);
        else { FILE *f=fopen(usb,"wb"); if(!f)abort();fwrite("changed!!",1,9,f);fclose(f); }
    }
    if (!rc && crash_phase && strstr(a,"/.txn-")) {
        if (crash_phase==1 && strstr(b,"/old")) _exit(77);
        if (crash_phase==2 && strlen(a)>4 && !strcmp(a+strlen(a)-4,"/new")) _exit(78);
    }
    if (!rc && crash_phase==1 && strstr(b,"/.txn-") && strstr(b,"/old")) _exit(77);
    return rc;
}
void pldmgr_log(const char *fmt,...) { (void)fmt; }
int pldmgr_autoload_update_config_entry(const char *a,const char *b) { (void)a;(void)b;autoload_updates++; return 0; }
/* Recorded local bytes only; any network-looking request aborts. */
int download_to_file(const char *url,const char *path) {
    if (!strcmp(url, REPOSITORY_SOURCE_URL)) return -1;
    if (strcmp(url,"fixture://payload")) abort();
    FILE *f=fopen(path,"wb"); if(!f)return -1;
    int rc=fwrite("new bytes",1,9,f)==9 ? 0:-1;
    if(fclose(f))rc=-1; return rc;
}
static void reset_fault(void) { fail_op=NULL; fail_at=hits=triggered=double_rename_failure=0; read_error=NULL; }
static void write_bytes(const char *p,const char *bytes) {
    FILE *f=fopen(p,"wb"); CHECK(f); CHECK(fwrite(bytes,1,strlen(bytes),f)==strlen(bytes)); CHECK(!fclose(f));
}
static void verify(const char *key,const char *bytes) {
    char dir[640],buf[64]={0}; SlotMeta meta;
    snprintf(dir,sizeof(dir),"%s/%s",PAYLOADS_STORAGE_DIR,key);
    CHECK(!slot_read_meta(dir,&meta)); CHECK(!strcmp(meta.filename,"safe.elf"));
    FILE *f=fopen(meta.payload_path,"rb"); CHECK(f); size_t n=fread(buf,1,sizeof(buf)-1,f); CHECK(!ferror(f));CHECK(!fclose(f));
    if(strcmp(buf,bytes))fprintf(stderr,"Context %s: expected %s, actual %s at %s\n",context,bytes,buf,key);
    CHECK(n==strlen(bytes)); CHECK(!strcmp(buf,bytes));
    CHECK(!strcmp(meta.origin,"usb") || !strcmp(meta.origin,"web_upload") || !strcmp(meta.kind,"repo"));
}
static int upload(const char *mode,char key[128]) {
    char msg[256];return payload_mgr_import_to_storage_mode("safe.elf",input,mode,"web_upload",NULL,msg,sizeof(msg),key,128);
}
static void seed(void) { char key[128]; reset_fault(); write_bytes(input,"old bytes"); CHECK(!upload("replace",key)); verify("local/safe","old bytes"); write_bytes(input,"new bytes"); write_bytes(usb,"new bytes"); }
static int operate(int which,char key[128]) {
    char json[4096];
    if(which==0)return upload("replace",key);
    if(which==1)return payload_mgr_usb_move(usb,"replace",0,json,sizeof(json));
    return sources_repo_install(NULL,"fixture","replace",NULL,json,sizeof(json));
}
static void verify_repo(const char *bytes) {
    char key[128];CHECK(!slot_repo_key("fixture",REPOSITORY_SOURCE_URL,key,sizeof(key)));verify(key,bytes);
}
static void seed_repo(void) {
    SlotMeta meta={0};char key[128],out[128];
    meta.schema=2;strcpy(meta.kind,"repo");strcpy(meta.filename,"safe.elf");strcpy(meta.id,"fixture");
    strcpy(meta.source_url,REPOSITORY_SOURCE_URL);
    CHECK(!slot_repo_key("fixture",REPOSITORY_SOURCE_URL,key,sizeof(key)));
    write_bytes(input,"old bytes");CHECK(!slot_commit_file(key,input,"replace",&meta,0,out,sizeof(out)));write_bytes(input,"new bytes");
}
struct job { int index,rc,repo; const char *mode; char key[128],path[640]; };
static void *worker(void *v) {
    struct job *j=v;char msg[256];
    if (j->repo) {
        char json[4096]; j->rc=sources_repo_install(NULL,"fixture",j->mode,NULL,json,sizeof(json));
        if (!j->rc) j->rc=json_extract_string(json,json+strlen(json),"slot",j->key,sizeof(j->key));
        return NULL;
    }
    j->rc=payload_mgr_import_to_storage_mode("parallel.elf",j->path,j->mode,"web_upload",NULL,msg,sizeof(msg),j->key,sizeof(j->key));return NULL;
}
static void concurrency(const char *mode,int repo) {
    struct job jobs[16];pthread_t threads[16];
    for(int i=0;i<16;i++) { jobs[i].index=i;jobs[i].mode=mode;jobs[i].repo=repo;
        snprintf(jobs[i].path,sizeof(jobs[i].path),"%s/job%d",PAYLOADS_STORAGE_DIR,i);write_bytes(jobs[i].path,"parallel bytes");
        CHECK(!pthread_create(&threads[i],NULL,worker,&jobs[i])); }
    int successes=0;
    for(int i=0;i<16;i++){CHECK(!pthread_join(threads[i],NULL));if(!jobs[i].rc)successes++;}
    CHECK(successes==(!strcmp(mode,"new")?1:16));
    if(!strcmp(mode,"keep_both"))for(int i=0;i<16;i++)for(int k=0;k<i;k++)CHECK(strcmp(jobs[i].key,jobs[k].key));
}
int main(int argc, char **argv) {
    CHECK(!ensure_dir_recursive(PAYLOADS_STORAGE_DIR));
    snprintf(input,sizeof(input),"%s/input",PAYLOADS_STORAGE_DIR);
    snprintf(usb,sizeof(usb),PLDMGR_USB_ROOT_FORMAT,0);CHECK(!ensure_dir_recursive(usb));
    strcat(usb,"/safe.elf");
    seed();char key[128],json[4096];
    if (argc > 1) {
        if (!strcmp(argv[1], "new")) CHECK(upload("new",key)!=0);
        else {
            fail_op = !strcmp(argv[1], "rename") ? "rename" : "write"; fail_at=1;
#ifndef LEGACY_STORAGE
            if (!strcmp(argv[1], "metadata")) fail_at=2;
#endif
            CHECK(upload("replace",key)!=0);reset_fault();
        }
        verify("local/safe","old bytes");
        printf("Verified regression: %s\n",argv[1]);return 0;
    }
#ifdef LEGACY_STORAGE
    return 0;
#else
    CHECK(!payload_mgr_usb_check(usb,json,sizeof(json)));CHECK(strstr(json,"exists_different"));CHECK(strstr(json,"sha256"));
    write_bytes(usb,"old bytes");CHECK(!payload_mgr_usb_check(usb,json,sizeof(json)));CHECK(strstr(json,"exists_same"));write_bytes(usb,"new bytes");
    CHECK(upload("new",key)!=0);verify("local/safe","old bytes");CHECK(!access(input,F_OK));
    CHECK(upload("invented",key)!=0);verify("local/safe","old bytes");
    CHECK(payload_mgr_usb_move(usb,"new",0,json,sizeof(json))!=0);CHECK(!access(usb,F_OK));verify("local/safe","old bytes");
    CHECK(payload_mgr_usb_move(usb,"invented",0,json,sizeof(json))!=0);CHECK(!access(usb,F_OK));
    CHECK(!upload("keep_both",key));CHECK(strcmp(key,"local/safe"));verify(key,"new bytes");verify("local/safe","old bytes");
    CHECK(!payload_mgr_usb_move(usb,"keep_both",1,json,sizeof(json)));CHECK(!access(usb,F_OK));
    CHECK(!payload_mgr_usb_move(usb,"keep_both",0,json,sizeof(json)));CHECK(access(usb,F_OK));
    seed_repo();CHECK(sources_repo_install(NULL,"fixture","new",NULL,json,sizeof(json))!=0);verify_repo("old bytes");
    CHECK(!sources_repo_install(NULL,"fixture",NULL,NULL,json,sizeof(json)));verify_repo("new bytes");
    seed_repo();CHECK(!sources_repo_install(NULL,"fixture","keep_both",NULL,json,sizeof(json)));verify_repo("old bytes");
    CHECK(!sources_repo_install(NULL,"fixture",NULL,NULL,json,sizeof(json)));verify_repo("new bytes");
    /* A cross-source replacement must not retire/remap the old source until
     * publication succeeds. Configuration atomicity is tested separately. */
    SlotMeta other={0};char other_key[128],other_dir[640];
    other.schema=2;strcpy(other.kind,"repo");strcpy(other.filename,"safe.elf");strcpy(other.id,"fixture");
    strcpy(other.source_url,"https://fixture.invalid/other");
    CHECK(!slot_repo_key("fixture",other.source_url,other_key,sizeof(other_key)));
    write_bytes(input,"old bytes");CHECK(!slot_commit_file(other_key,input,"replace",&other,0,key,sizeof(key)));
    seed_repo();autoload_updates=0;fail_op="rename";fail_at=3;
    CHECK(sources_repo_install(NULL,"fixture","replace",other_key,json,sizeof(json))!=0);reset_fault();
    CHECK(autoload_updates==0);verify_repo("old bytes");verify(other_key,"old bytes");
    CHECK(!sources_repo_install(NULL,"fixture","replace",other_key,json,sizeof(json)));verify_repo("new bytes");
    snprintf(other_dir,sizeof(other_dir),"%s/%s",PAYLOADS_STORAGE_DIR,other_key);CHECK(access(other_dir,F_OK));
    CHECK(autoload_updates>0);
    /* Changing the filename must publish matching metadata and bytes together. */
    SlotMeta renamed={0},read_back;char renamed_dir[640];
    renamed.schema=2;strcpy(renamed.kind,"local");strcpy(renamed.origin,"web_upload");strcpy(renamed.filename,"before.elf");
    write_bytes(input,"old bytes");CHECK(!slot_commit_file("local/renamed",input,"replace",&renamed,0,key,sizeof(key)));
    snprintf(renamed_dir,sizeof(renamed_dir),"%s/local/renamed",PAYLOADS_STORAGE_DIR);
    strcpy(renamed.filename,"after.bin");write_bytes(input,"new bytes");fail_op="rename";fail_at=3;
    CHECK(slot_commit_file("local/renamed",input,"replace",&renamed,0,key,sizeof(key))!=0);reset_fault();
    CHECK(!slot_read_meta(renamed_dir,&read_back));CHECK(!strcmp(read_back.filename,"before.elf"));CHECK(!access(read_back.payload_path,F_OK));
    CHECK(!slot_commit_file("local/renamed",input,"replace",&renamed,0,key,sizeof(key)));
    CHECK(!slot_read_meta(renamed_dir,&read_back));CHECK(!strcmp(read_back.filename,"after.bin"));CHECK(!access(read_back.payload_path,F_OK));
    const char *ops[]={"open","read","write","flush","fclose","sync","close","mkdir","mkdtemp","mkstemp","rename"};
    int failures=0;
    for(int which=0;which<3;which++)for(size_t o=0;o<sizeof(ops)/sizeof(*ops);o++) {
        for(int n=1;n<150;n++) {
            seed();if(which==2)seed_repo();
            /* Byte-for-byte old metadata must survive failed publication. */
            char olddir[640],metapath[700],before[65],after[65];
            if(which==2){CHECK(!slot_repo_key("fixture",REPOSITORY_SOURCE_URL,key,sizeof(key)));}
            else strcpy(key,"local/safe");
            snprintf(olddir,sizeof(olddir),"%s/%s",PAYLOADS_STORAGE_DIR,key);
            snprintf(metapath,sizeof(metapath),"%s/meta.json",olddir);CHECK(!compute_sha256_file(metapath,before));
            fail_op=ops[o];fail_at=n;hits=triggered=0;
            snprintf(context,sizeof(context),"backend %d, fault %s/%d",which,ops[o],n);
            int rc=operate(which,key),did_trigger=triggered;
            reset_fault();
            if(rc){failures++;if(which==2)verify_repo("old bytes");else verify("local/safe","old bytes");
                CHECK(!compute_sha256_file(metapath,after));CHECK(!strcmp(before,after));
                if(which==1)CHECK(!access(usb,F_OK));
            }else {if(which==2)verify_repo("new bytes");else verify("local/safe","new bytes");}
            CHECK(!slot_storage_recover());CHECK(!slot_storage_recover());
            if(!did_trigger)break;
        }
    }
    strcpy(context,"concurrency/crash");
    concurrency("new",0);concurrency("keep_both",0);concurrency("replace",0);
    concurrency("keep_both",1);concurrency("replace",1);
    seed(); fail_op="rename"; fail_at=3; double_rename_failure=1;
    CHECK(upload("replace",key)!=0); reset_fault();
    CHECK(!slot_storage_recover()); verify("local/safe","old bytes");
    /* Media disappears or changes after copy: committed slot stays valid,
     * and a changed original is never deleted. */
    for(int change=1;change<=2;change++) {
        seed();usb_after_commit=change;
        CHECK(!payload_mgr_usb_move(usb,"replace",0,json,sizeof(json)));usb_after_commit=0;
        CHECK(strstr(json,"warning"));verify("local/safe","new bytes");
        if(change==2)CHECK(!access(usb,F_OK));
    }
    for(int phase=1;phase<=4;phase++) {
        seed();pid_t pid=fork();CHECK(pid>=0);
        if(!pid){crash_phase=phase;upload("replace",key);_exit(99);}
        int status;CHECK(waitpid(pid,&status,0)==pid);CHECK(WIFEXITED(status));CHECK(WEXITSTATUS(status)==(76+phase));
        CHECK(!slot_storage_recover());CHECK(!slot_storage_recover());verify("local/safe",phase==2?"new bytes":"old bytes");
    }
    char abandoned[768],unrelated[768];
    snprintf(abandoned,sizeof(abandoned),"%s/.upload-ABCDEF",PAYLOADS_STORAGE_DIR);write_bytes(abandoned,"orphan");
    snprintf(unrelated,sizeof(unrelated),"%s/.upload-keep",PAYLOADS_STORAGE_DIR);write_bytes(unrelated,"retain");
    CHECK(!slot_storage_recover());CHECK(access(abandoned,F_OK));CHECK(!access(unrelated,F_OK));
    snprintf(abandoned,sizeof(abandoned),"%s/local/.txn-invalid",PAYLOADS_STORAGE_DIR);CHECK(!mkdir(abandoned,0700));
    snprintf(unrelated,sizeof(unrelated),"%s/target",abandoned);write_bytes(unrelated,"../safe");
    CHECK(slot_storage_recover()!=0);CHECK(!access(unrelated,F_OK));
    write_bytes(input,"new bytes");CHECK(upload("replace",key)!=0);
    CHECK(!unlink(unrelated));CHECK(!rmdir(abandoned));CHECK(!slot_storage_recover());verify("local/safe","old bytes");
    printf("Storage tests: %d checks, %d rejected I/O operations; concurrent keys and interrupted commits passed\n",checks,failures);
    return 0;
#endif
}

int download_to_file_limited(const char *url, const char *path, size_t limit) {
    (void)limit; return download_to_file(url, path);
}
