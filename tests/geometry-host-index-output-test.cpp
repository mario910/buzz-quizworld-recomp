#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_index_output.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
static void check_index_output_rejects()
{
    buzz::GeometryHostIndexData indices;
    uint8_t destination[64]; const char* reason = nullptr;
    for (unsigned fault=0;fault<7;++fault) {
        indices.ready=true; indices.triangles=4; indices.final_group_kept=4;
        memset(destination,0xa5,sizeof destination);
        switch(fault) {
        case 0:indices.ready=false;break;
        case 1:indices.triangles=65535/3+1;break;
        case 2:indices.final_group_kept=5;break;
        case 3:indices.triangles=1;break;
        case 4:break; // Null nonempty destination.
        case 5:break; // One byte short of the aligned output.
        case 6:break; // Logical bytes fit; alignment bytes do not.
        }
        assert(!buzz::emit_geometry_host_indices(indices,fault==4?nullptr:destination,
            fault==5?31:fault==6?24:sizeof destination,&reason) && reason);
        for (unsigned byte:destination) assert(byte==0xa5);
    }
    indices.ready=true; indices.triangles=indices.final_group_kept=0;
    assert(buzz::emit_geometry_host_indices(indices,nullptr,0,&reason) && !reason);
    puts("PASS index output admission: seven unchanged-destination rejects and null empty stream");
}
static void compare_index_output(const buzz::GeometryHostIndexData& indices,
    const uint8_t* expected, unsigned bytes)
{
    static uint8_t* storage = static_cast<uint8_t*>(VirtualAlloc(nullptr,0x41000,
        MEM_RESERVE | MEM_COMMIT,PAGE_READWRITE));
    static bool protected_page = false;
    if (!protected_page) {
        check_index_output_rejects();
        assert(storage); DWORD previous;
        assert(VirtualProtect(storage+0x40000,0x1000,PAGE_NOACCESS,&previous));
        protected_page = true;
    }
    auto* destination = storage + 0x40000 - bytes;
    memset(destination-32,0xa5,bytes+32);
    const char* reason = nullptr;
    assert(buzz::emit_geometry_host_indices(indices,destination,bytes,&reason) && !reason);
    assert(!memcmp(destination,expected,bytes));
    for (unsigned at=0;at<32;++at) assert(destination[-32+int(at)]==0xa5);
    if (bytes) {
        assert(!buzz::emit_geometry_host_indices(indices,destination,bytes-1,&reason) && reason);
        assert(!memcmp(destination,expected,bytes));
    }
    static uint64_t cases=0,checked_bytes=0; static unsigned cohorts[5][8]{};
    ++cases; checked_bytes+=bytes; ++cohorts[indices.final_group_kept][indices.triangles&7];
    // The base driver executes 160 FP cases after the complete real corpus.
    if (cases==19399) {
        unsigned shapes=0; for(const auto& row:cohorts) for(unsigned count:row) shapes+=count!=0;
        printf("PASS final index serialization: %llu real jobs/%llu bytes; %u last-group/alignment shapes; exact PAGE_NOACCESS ends, prefix canaries and unchanged short-span rejects\n",
            static_cast<unsigned long long>(cases),static_cast<unsigned long long>(checked_bytes),shapes);
    }
}
#define BUZZ_HOST_INDEX_OUTPUT_CHECK compare_index_output
#define BUZZ_HOST_CULL_REFERENCE "../analysis/native-geometry/phase3/host-index-output-v1/reference.inc"
#include "geometry-host-cull-job-test.cpp"
