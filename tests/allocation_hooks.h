#include <stdlib.h>
#include <string.h>
#include <stddef.h>
void *allocation_test_malloc(size_t size);
void *allocation_test_realloc(void *pointer, size_t size);
char *allocation_test_strdup(const char *string);
#define malloc allocation_test_malloc
#define realloc allocation_test_realloc
#define strdup allocation_test_strdup
