#include <errno.h>
#include <pthread.h>
#include <sched.h>

static inline int buzz_android_thread_affinity(pthread_t thread, size_t size, const cpu_set_t* set)
{
    const pid_t tid = pthread_gettid_np(thread);
    if (tid <= 0) return ESRCH;
    return sched_setaffinity(tid, size, set) == 0 ? 0 : errno;
}

#define pthread_setaffinity_np buzz_android_thread_affinity
