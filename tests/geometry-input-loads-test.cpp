#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_host_unit_vectors.h"
#include "../port/native_geometry_host_streams.h"
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <initializer_list>

int main() {
    const size_t page = sysconf(_SC_PAGESIZE);
    auto* mapping = static_cast<uint8_t*>(mmap(nullptr, page*3, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0));
    assert(mapping != MAP_FAILED);
    auto* arena = mapping+page;
    assert(mprotect(arena,page,PROT_READ|PROT_WRITE)==0);
    unsigned random=12345, checks=0;
    for(unsigned trial=0;trial<32;++trial) {
        for(size_t i=0;i<page;++i) {random^=random<<13;random^=random>>17;random^=random<<5;arena[i]=random;}
        for(unsigned stride : {3u,4u,5u,7u,8u,10u,11u,16u,255u}) {
            for(unsigned vertices=0;vertices<=8;++vertices) {
                const unsigned span=vertices?(vertices-1)*stride+3:0;
                for(unsigned placement=0;placement<18;++placement) {
                    const auto* source=placement==17?arena+page-span:arena+placement;
                    const auto actual=buzz::geometry_host_unit_words8(source,stride,vertices);
                    uint32_t expected[8]{};
                    for(unsigned i=0;i<vertices;++i)for(unsigned j=0;j<3;++j)expected[i]=(expected[i]<<8)|source[i*stride+j];
                    assert(!memcmp(&actual,expected,32));++checks;
                }
            }
            for(unsigned components=1;components<=4;++components) {
                if(stride<components*2)continue;
                for(unsigned vertices=0;vertices<=8;++vertices) {
                    const unsigned span=vertices?(vertices-1)*stride+components*2:0;
                    for(unsigned placement=0;placement<18;++placement) {
                        const unsigned offset=placement==17?page-span:placement;
                        const auto actual=buzz::decode_geometry_host_half_stream8(arena,offset,stride,components,vertices);
                        uint32_t half[4][8]{},prefix[8]{};
                        for(unsigned i=0;i<vertices;++i) {
                            unsigned at=offset+i*stride;prefix[i]=arena[at&~15u];
                            for(unsigned c=0;c<components;++c)half[c][i]=(uint32_t(arena[at+c*2])<<8)|arena[at+c*2+1];
                        }
                        buzz::GeometryHostSoA8 input;memcpy(&input,half,sizeof input);
                        const auto expected=buzz::decode_geometry_host_halves8(input,_mm256_loadu_si256((const v8*)prefix),components);
                        assert(!memcmp(&actual,&expected,sizeof actual));++checks;
                    }
                }
            }
        }
    }
    assert(munmap(mapping,page*3)==0);
    printf("PASS %u packed direction/half-stream comparisons, all tail lengths, alignments and guard pages\n",checks);
}
