#define BUZZ_TEST_HOST_COMMANDS 1
#include "geometry-host-command-proof.h"
#include "geometry-position-commands-test.cpp"
extern "C" void buzz_geometry_job_native_fail(spu_context*,const char* reason)
{
    std::fprintf(stderr,"Host position commands invariant: %s\n",reason);std::abort();
}
