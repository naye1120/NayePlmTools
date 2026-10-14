#include <pthread.h>
#include <stddef.h>
void *autoload_test_calloc(size_t, size_t);
#define calloc autoload_test_calloc
int autoload_test_pthread_create(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int autoload_test_pthread_join(pthread_t, void **);
#define pthread_create autoload_test_pthread_create
#define pthread_join autoload_test_pthread_join
