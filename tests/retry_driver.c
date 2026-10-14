#include <stdio.h>
#include <fcntl.h>
int integrity_openat(int parent, const char *part, int flags, ...) { return openat(parent, part, flags); }
#include <stdlib.h>
#include <string.h>
#include "sources.h"
#include "repository.h"
#include "slot.h"
#include "sha256.h"
#include "utils.h"
#include "json_helpers.h"
#include "pldmgr.h"
static const char *manifest;
static int first_calls,retry_calls,fail_first;
void pldmgr_log(const char *fmt,...){(void)fmt;}
int pldmgr_autoload_update_config_entry(const char *a,const char *b){(void)a;(void)b;return 0;}
static int write_bytes(const char *path,const char *text){FILE*f=fopen(path,"wb");if(!f)return -1;int rc=fwrite(text,1,strlen(text),f)==strlen(text)?0:-1;if(fclose(f))rc=-1;return rc;}
int download_to_file(const char *url,const char *path){
    if(!strcmp(url,REPOSITORY_SOURCE_URL)){char *text=NULL;size_t size=0;if(read_file_text(manifest,&text,&size))return -1;int rc=write_bytes(path,text);free(text);return rc;}
    if(!strcmp(url,"fixture://initial")){first_calls++;if(write_bytes(path,"first bytes"))return -1;return fail_first?-1:0;}
    if(!strcmp(url,"fixture://retry")){retry_calls++;return write_bytes(path,"retry bytes");}
    abort();
}
int download_to_file_limited(const char *url,const char *path,size_t limit){(void)limit;return download_to_file(url,path);}
int main(int argc,char **argv){
    if(argc!=3)return 2;manifest=argv[1];fail_first=atoi(argv[2]);
    char key[128],out[128],input[640],meta_path[640],slot_dir[640],before[65],after[65],bodyhash[65],json[4096];
    SlotMeta meta={0};meta.schema=2;strcpy(meta.kind,"repo");strcpy(meta.id,"fixture");strcpy(meta.filename,"old.elf");strcpy(meta.source_url,REPOSITORY_SOURCE_URL);
    if(ensure_dir_recursive(PAYLOADS_STORAGE_DIR))return 2;
    snprintf(input,sizeof(input),"%s/seed",PAYLOADS_STORAGE_DIR);
    if(write_bytes(input,"old bytes") || slot_repo_key("fixture",REPOSITORY_SOURCE_URL,key,sizeof(key)) || slot_commit_file(key,input,"replace",&meta,0,out,sizeof(out)))return 3;
    snprintf(meta_path,sizeof(meta_path),"%s/%s/meta.json",PAYLOADS_STORAGE_DIR,key);
    if(compute_sha256_file(meta_path,before))return 3;
    strcpy(slot_dir,meta.slot_dir);
    int rc=sources_repo_install(NULL,"fixture",NULL,NULL,json,sizeof(json));
    if(compute_sha256_file(meta_path,after) || slot_read_meta(slot_dir,&meta) || compute_sha256_file(meta.payload_path,bodyhash))return 3;
    printf("{\"rc\":%d,\"first\":%d,\"retry\":%d,\"metadata_same\":%s,\"body_hash\":\"%s\"}\n",rc,first_calls,retry_calls,!strcmp(before,after)?"true":"false",bodyhash);
    return 0;
}
