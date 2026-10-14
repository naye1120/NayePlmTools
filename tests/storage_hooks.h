#ifndef STORAGE_HOOKS_H
#define STORAGE_HOOKS_H
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
size_t storage_test_fread(void *, size_t, size_t, FILE *);
size_t storage_test_fwrite(const void *, size_t, size_t, FILE *);
int storage_test_ferror(FILE *);
int storage_test_fflush(FILE *);
int storage_test_fclose(FILE *);
int storage_test_fsync(int);
int storage_test_close(int);
int storage_test_rename(const char *, const char *);
int storage_test_mkdir(const char *, mode_t);
char *storage_test_mkdtemp(char *);
int storage_test_mkstemp(char *);
FILE *storage_test_fopen(const char *, const char *);
#define fread storage_test_fread
#define fwrite storage_test_fwrite
#define ferror storage_test_ferror
#define fflush storage_test_fflush
#define fclose storage_test_fclose
#define fsync storage_test_fsync
#define close storage_test_close
#define rename storage_test_rename
#define mkdir storage_test_mkdir
#define mkdtemp storage_test_mkdtemp
#define mkstemp storage_test_mkstemp
#define fopen storage_test_fopen
#endif
