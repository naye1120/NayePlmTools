#include <stdio.h>
#include <unistd.h>
size_t integrity_fwrite(const void *,size_t,size_t,FILE *);
int integrity_fflush(FILE *);
int integrity_fclose(FILE *);
int integrity_fsync(int);
#define fwrite integrity_fwrite
#define fflush integrity_fflush
#define fclose integrity_fclose
#define fsync integrity_fsync
