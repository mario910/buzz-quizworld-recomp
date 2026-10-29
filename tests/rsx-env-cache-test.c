#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
static unsigned lookups;
static char* lookup(const char* name) {
    ++lookups;
    return strstr(name, "present") ? "value" : NULL;
}
#define getenv lookup
#include "../ps3recomp/libs/video/rsx_env_cache.h"
#undef getenv
int main(void)
{
    RsxEnvCache cache = {0};
    static const char hit[] = "present", missing[] = "missing";
    assert(!strcmp(rsx_cached_env(&cache, hit), "value"));
    assert(!rsx_cached_env(&cache, missing));
    unsigned before = lookups;
    for (unsigned n = 0; n < 1000000; ++n) {
        assert(!strcmp(rsx_cached_env(&cache, hit), "value"));
        assert(!rsx_cached_env(&cache, missing));
    }
    assert(lookups == before);
    /* Stable keys that deliberately collide by address still require a full
     * pointer match; no value, including a miss, may leak to another key. */
    static char keys[1024][32];
    for (unsigned n = 0; n < 1024; ++n)
        snprintf(keys[n], sizeof keys[n], "%s-%u", (n & 1) ? "present" : "missing", n);
    for (unsigned round = 0; round < 3; ++round) for (unsigned n = 0; n < 1024; ++n) {
        char* value = rsx_cached_env(&cache, keys[n]);
        assert((n & 1) ? value && !strcmp(value, "value") : !value);
    }
    puts("RSX startup settings: cached hits/misses, repeated calls, exact-key collisions PASS");
}
