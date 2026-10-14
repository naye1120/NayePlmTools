#include <stdio.h>
#include <stdarg.h>
int json_test_vsnprintf(char *buf, size_t size, const char *fmt, va_list args);
#define vsnprintf json_test_vsnprintf
