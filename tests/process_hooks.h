#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
void *test_malloc(size_t size);
int test_kill(pid_t pid, int signal);
int test_usleep(useconds_t usec);
#define malloc test_malloc
#define kill test_kill
#define usleep test_usleep

