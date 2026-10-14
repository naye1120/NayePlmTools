#include <stdio.h>
#include <unistd.h>
size_t config_test_fwrite(const void *, size_t, size_t, FILE *);
size_t config_test_fread(void *, size_t, size_t, FILE *);
int config_test_ferror(FILE *);
int config_test_fflush(FILE *);
int config_test_fclose(FILE *);
int config_test_fsync(int);
int config_test_rename(const char *, const char *);
int config_test_mkstemp(char *);
#define fwrite config_test_fwrite
#define fread config_test_fread
#define ferror config_test_ferror
#define fflush config_test_fflush
#define fclose config_test_fclose
#define fsync config_test_fsync
#define rename config_test_rename
#define mkstemp config_test_mkstemp
