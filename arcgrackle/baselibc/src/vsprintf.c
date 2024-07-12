/*
 * vsprintf.c
 */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int vsprintf(char *buffer, const char *format, va_list ap)
{
	return vsnprintf(buffer, INT32_MAX, format, ap);
}
