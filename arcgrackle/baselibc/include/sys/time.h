struct timeval {
    unsigned int tv_sec;
    unsigned int tv_usec;
};

int gettimeofday(struct timeval *tv, void *tz);
