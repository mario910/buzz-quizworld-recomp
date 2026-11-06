/* Linux mechanism benchmark only: a synthetic delayed DMA producer. This
 * measures handoff CPU/latency, not game FPS, battery life or device thermals. */
#include "../port/ppu_word_wait.c"
#include <pthread.h>
#include <sys/resource.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { N = 4000, EA = 0x101A0 };
static uint8_t memory[0x20000];
static pthread_barrier_t go, done;
static uint64_t published, latency[N];
static uint64_t now_ns(void) {
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
    return (uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;
}
static void* producer(void* unused) {
    (void)unused;
    for(unsigned n=0;n<N;++n) {
        pthread_barrier_wait(&go);
        struct timespec delay={0,250000};nanosleep(&delay,NULL);
        published=now_ns();
        __atomic_store_n((uint32_t*)(memory+EA),0,__ATOMIC_RELEASE);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        buzz_ppu_word_dma_written(EA,4);
        pthread_barrier_wait(&done);
    }
    return NULL;
}
static double cpu_ms(struct rusage* r) {
    return (r->ru_utime.tv_sec+r->ru_stime.tv_sec)*1000.0+
           (r->ru_utime.tv_usec+r->ru_stime.tv_usec)/1000.0;
}
static int compare(const void* a,const void* b) {
    const uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);
}
int main(int argc,char**argv) {
    if(argc!=2 || (strcmp(argv[1],"poll") && strcmp(argv[1],"word")))return 1;
    const int word=!strcmp(argv[1],"word");
    pthread_barrier_init(&go,NULL,2);pthread_barrier_init(&done,NULL,2);
    struct rusage before,after;getrusage(RUSAGE_SELF,&before);uint64_t start=now_ns();
    pthread_t thread;if(pthread_create(&thread,NULL,producer,NULL))return 1;
    unsigned calls=0;
    for(unsigned n=0;n<N;++n) {
        __atomic_store_n((uint32_t*)(memory+EA),1,__ATOMIC_RELEASE);
        pthread_barrier_wait(&go);
        while(__atomic_load_n((uint32_t*)(memory+EA),__ATOMIC_ACQUIRE)) {
            ++calls;
            if(word)buzz_ppu_word_wait(memory,EA,0x288BE0);
            else {struct timespec delay={0,2000};nanosleep(&delay,NULL);}
        }
        latency[n]=now_ns()-published;pthread_barrier_wait(&done);
    }
    pthread_join(thread,NULL);uint64_t elapsed=now_ns()-start;getrusage(RUSAGE_SELF,&after);
    uint64_t total=0;for(unsigned n=0;n<N;++n)total+=latency[n];qsort(latency,N,sizeof latency[0],compare);
    printf("{\"mode\":\"%s\",\"handoffs\":%u,\"wait_calls\":%u,\"elapsed_ms\":%.3f,\"process_cpu_ms\":%.3f,\"latency_mean_us\":%.3f,\"latency_p95_us\":%.3f}\n",
        argv[1],N,calls,elapsed/1e6,cpu_ms(&after)-cpu_ms(&before),total/(1000.0*N),latency[(N*95)/100]/1000.0);
    pthread_barrier_destroy(&go);pthread_barrier_destroy(&done);return 0;
}
