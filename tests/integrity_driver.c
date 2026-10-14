#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/stat.h>
#include "repository.h"
#include "slot.h"
#include "ps5_launcher.h"
#include "history_mgr.h"
#ifdef LEGACY_INTEGRITY
int download_to_file_limited(const char *url,const char *path,size_t limit) { (void)limit;return download_to_file(url,path); }
#endif
static int io_fault(const char *op) {
    const char *selected=getenv("PLDMGR_IO_FAULT");
    if(selected && !strcmp(selected,op)){errno=ENOSPC;return 1;}return 0;
}
size_t integrity_fwrite(const void *p,size_t size,size_t n,FILE *f){return io_fault("write") ? 0 : fwrite(p,size,n,f);}
int integrity_fflush(FILE *f){int rc=fflush(f);return io_fault("flush") ? -1 : rc;}
int integrity_fclose(FILE *f){int rc=fclose(f);return io_fault("close") ? -1 : rc;}
int integrity_fsync(int fd){return io_fault("sync") ? -1 : fsync(fd);}
int integrity_openat(int parent,const char *part,int flags,...) {
    const char *swap=getenv("PLDMGR_SWAP_FILE"), *target=getenv("PLDMGR_SWAP_TARGET");
    if(swap && target) {
        const char *leaf=strrchr(swap,'/');leaf=leaf?leaf+1:swap;
        if(!strcmp(part,leaf)) {
            struct stat st;if(lstat(swap,&st))abort();
            if(S_ISDIR(st.st_mode)){char backup[1024];snprintf(backup,sizeof(backup),"%s.saved",swap);if(rename(swap,backup))abort();}
            else if(unlink(swap))abort();
            if(symlink(target,swap))abort();
        }
    }
    return openat(parent,part,flags);
}
static int sockets, history;
void pldmgr_log(const char *fmt,...) {(void)fmt;}
int pldmgr_autoload_update_config_entry(const char *a,const char *b){(void)a;(void)b;return 0;}
void history_mgr_add(const char *path){(void)path;history++;}
int integrity_socket(int domain,int type,int protocol){(void)domain;(void)type;(void)protocol;sockets++;return open("/dev/null",O_WRONLY);}
int integrity_connect(int fd,const struct sockaddr *addr,socklen_t length){(void)fd;(void)addr;(void)length;return 0;}
ssize_t integrity_send(int fd,const void *buf,size_t length,int flags){(void)fd;(void)buf;(void)flags;return (ssize_t)length;}
int main(int argc,char **argv) {
    if(argc<3)return 2;
    if(!strcmp(argv[1],"download")) {
        if(argc!=5)return 2;
        int rc=download_to_file_limited(argv[2],argv[3],strtoull(argv[4],NULL,10));
        printf("%d\n",rc);return 0;
    }
    if(!strcmp(argv[1],"parse")) {
        RepoPayload *items=NULL;size_t count=0;
        int rc=parse_repository_payloads(argv[2],&items,&count);free(items);
        printf("%d %zu\n",rc,count);return 0;
    }
    if(!strcmp(argv[1],"launch")) {
        char path[512];int rc=slot_resolve(argv[2],path,sizeof(path));
        if(!rc)rc=ps5_launch_elf(path);
        printf("%d %d %d\n",rc,sockets,history);return 0;
    }
    if(!strcmp(argv[1],"direct")) {
        int rc=ps5_launch_elf(argv[2]);printf("%d %d %d\n",rc,sockets,history);return 0;
    }
    return 2;
}
