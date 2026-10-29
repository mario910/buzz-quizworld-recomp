#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif
#ifdef _MSC_VER
#define TEST_TLS __declspec(thread)
#else
#define TEST_TLS _Thread_local
#endif
static TEST_TLS unsigned lookups;
static TEST_TLS char thread_value[32];
static char* lookup(const char* name)
{
    ++lookups;
    if (!strcmp(name, "missing")) return NULL;
    if (!strcmp(name, "zero")) return "0";
    return thread_value;
}
#define getenv lookup
#include "../ps3recomp/runtime/spu/spurs_env_cache.h"
#undef getenv

static const char present[] = "present", missing[] = "missing", zero[] = "zero";

static void warm_and_check(void)
{
    assert(spurs_cached_env(present) == thread_value);
    assert(spurs_cached_env(missing) == NULL);
    assert(!strcmp(spurs_cached_env(zero), "0"));
    assert(lookups == 3);
    for (unsigned n = 0; n < 100000; ++n) {
        assert(spurs_cached_env(present) == thread_value);
        assert(spurs_cached_env(missing) == NULL);
        assert(!strcmp(spurs_cached_env(zero), "0"));
    }
    assert(lookups == 3);
}

#ifdef _WIN32
static DWORD WINAPI worker(void* argument)
#else
static void* worker(void* argument)
#endif
{
    snprintf(thread_value, sizeof thread_value, "worker-%u", *(unsigned*)argument);
    /* Main already cached the SAME key addresses with its own mock value.
     * A shared cache would leak that pointer and skip this thread's lookups. */
    warm_and_check();
    return 0;
}

int main(void)
{
    strcpy(thread_value, "main");
    warm_and_check();
    static const char same_text_different_address[] = "present";
    assert(spurs_cached_env(same_text_different_address) == thread_value);
    assert(lookups == 4); /* Cache keys are addresses, not string hashes. */

    static char keys[40][32];
    for (unsigned i = 0; i < 40; ++i) {
        snprintf(keys[i], sizeof keys[i], "setting-%u", i);
        assert(spurs_cached_env(keys[i]) == thread_value);
    }
    assert(lookups == 44);
    for (unsigned i = 0; i < 40; ++i)
        assert(spurs_cached_env(keys[i]) == thread_value);
    assert(lookups == 56); /* First 28 fit; last 12 use the live fallback. */
    assert(spurs_cached_env(present) == thread_value);
    assert(spurs_cached_env(missing) == NULL);
    assert(!strcmp(spurs_cached_env(zero), "0"));
    assert(lookups == 56); /* A full cache does not evict earlier entries. */

    unsigned arguments[16];
#ifdef _WIN32
    HANDLE threads[16];
#else
    pthread_t threads[16];
#endif
    for (unsigned i = 0; i < 16; ++i) {
        arguments[i] = i;
#ifdef _WIN32
        threads[i] = CreateThread(NULL, 0, worker, &arguments[i], 0, NULL);
        assert(threads[i]);
#else
        assert(!pthread_create(&threads[i], NULL, worker, &arguments[i]));
#endif
    }
    for (unsigned i = 0; i < 16; ++i) {
#ifdef _WIN32
        assert(WaitForSingleObject(threads[i], INFINITE) == WAIT_OBJECT_0);
        DWORD status;
        assert(GetExitCodeThread(threads[i], &status) && status == 0);
        CloseHandle(threads[i]);
#else
        assert(!pthread_join(threads[i], NULL));
#endif
    }
    assert(lookups == 56);
    assert(spurs_cached_env(present) == thread_value);
    puts("SPURS env cache: hits, misses, presence, capacity fallback, 16-thread isolation PASS");
    return 0;
}
