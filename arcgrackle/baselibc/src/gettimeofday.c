#include <sys/time.h>

int do_gettimeofday(struct timeval *tv);
int gettimeofday(struct timeval *tv, void *tz)
{
  (void)tz;
  return do_gettimeofday(tv);
}
