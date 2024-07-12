typedef void* pthread_t;
typedef unsigned char pthread_attr_t[0x28];
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine) (void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_cancel(pthread_t thread);
void pthread_exit(void *retval);
