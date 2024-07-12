int write(int fd, const void *buf, unsigned int count);
int read(int fd, void *buf, unsigned int count);
int close(int fd);
int stat(const char *pathname, void *buf);

int usleep(int usecs);
