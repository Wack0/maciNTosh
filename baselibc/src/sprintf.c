/*
 * sprintf.c
 */

#ifdef WITH_STDIO
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int sprintf(char *buffer, const char *format, ...)
{
	va_list ap;
	int rv;

	va_start(ap, format);
	rv = vsnprintf(buffer, INT32_MAX, format, ap);
	va_end(ap);

	return rv;
}
#endif
