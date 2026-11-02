// Differential check of the closed-form output admission against the exact
// group walk it short-cuts. Every accept must match the walk's decision and
// counts; the proof may decline (the exact walk then runs), never disagree.
#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_geometry_half_output.h"
#include "../port/native_geometry_cmp_output.h"
#include "../port/native_geometry_i16_output.h"
#include "../port/native_geometry_snorm_output.h"
#include "../port/native_geometry_unorm_output.h"
#include "../port/native_geometry_u8_integer_output.h"
#include "../port/native_geometry_float_output.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
using namespace buzz;
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*){abort();}
void spu_stop(spu_context*){abort();}
}
struct Walk { bool accept; unsigned columns, groups, cross; };
// Verbatim walk of the integer encoders before the proof was added.
static Walk integer_walk(const u128* r)
{
    if(!r[77]._u32[0])return {true,0,0,0};
    const unsigned columns=r[60]._u32[0]?r[60]._u32[0]:1;
    if(columns>256)return {false};
    const auto limit=spu_a(spu_xshw(r[29]),r[3]);unsigned total=0,cross_columns=0;
    for(unsigned col=0;col<columns;++col){
        auto cursor=spu_a(spu_a(spu_il(col*16),r[52]),r[29]);auto route=spu_mpya(r[55],spu_a(spu_il(col),r[54]),r[53]);
        const bool cross=spu_cgti(spu_a(spu_andi(route,15),r[48]),16)._u32[0]!=0;cross_columns+=cross;unsigned groups=0;
        for(;;){
            if(++groups>65536 || ++total>1048576)return {false};
            const auto after=spu_cgt(cursor,limit),chosen=spu_selb(route,r[30],after);
            for(unsigned lane=0;lane<4;++lane){
                if(((r[67]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return {false};
                if(cross && ((r[76]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return {false};
            }
            if(after._u32[0])break;cursor=spu_a(cursor,r[29]);route=spu_a(route,r[31]);
        }
    }
    return {true,columns,total,cross_columns};
}
// Verbatim walk of the float encoder before the proof was added.
static Walk float_walk(const u128* arguments)
{
    if(!arguments[77]._u32[0])return {true,0,0,0};
    const unsigned columns=arguments[60]._u32[0]?arguments[60]._u32[0]:1;
    if(columns>256)return {false};
    static u128 r[128];memcpy(r,arguments,sizeof r);open_geometry_float_output(r);unsigned total=0,cross_columns=0;
    for(unsigned col=0;col<columns;++col){
        const bool cross=prepare_geometry_float_output_column(r);cross_columns+=cross;unsigned groups=0;
        for(;;){
            if(++groups>65536 || ++total>1048576)return {false};
            const auto after=spu_cgt(r[21],r[25]),chosen=spu_selb(r[23],r[30],after);
            for(unsigned lane=0;lane<4;++lane){
                if(((r[19]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return {false};
                if(cross && ((r[22]._u32[0]+chosen._u32[lane])&0x3fff0)<0xe680)return {false};
            }
            if(after._u32[0])break;
            r[21]=spu_a(r[21],r[29]);r[23]=spu_a(r[23],r[31]);
        }
        r[32]=spu_ai(r[32],1);r[44]=spu_ai(r[44],16);
    }
    return {true,columns,total,cross_columns};
}
static uint32_t rng(uint64_t& s){s^=s<<13;s^=s>>7;s^=s<<17;return uint32_t(s>>11);}
static uint32_t pick(uint64_t& s,std::initializer_list<uint32_t> v){return *(v.begin()+rng(s)%v.size());}
static void seed(u128* r,uint64_t& s)
{
    for(unsigned i=0;i<128;++i)for(auto& w:r[i]._u32)w=rng(s);
    r[77]._u32[0]=rng(s)%16?1:0;
    r[60]._u32[0]=pick(s,{0,1,2,3,7,16,64,256,257})%(rng(s)%4?17:300);
    // Small steps with groups from 1 to beyond the 65536 budget, plus
    // zero, negative and huge steps that the proof must decline.
    const uint32_t step=pick(s,{16,48,64,192,1024,1,0,0xfffffff0u,0x7fff0000u,rng(s)});
    for(auto& w:r[29]._u32)w=rng(s)%4?step:rng(s);
    const uint32_t start=pick(s,{0x10000,0x7fffff00u,0xfffffff0u,0x80000000u,rng(s)});
    const uint32_t groups=pick(s,{0,1,2,5,100,4000,65535,65536,70000,rng(s)%300});
    r[52]._u32[0]=start;
    r[3]._u32[0]=start+groups*step+pick(s,{0,1,15,0xffffffffu,rng(s)%64});
    if(rng(s)%3==0)for(unsigned lane=1;lane<4;++lane){r[52]._u32[lane]=r[52]._u32[0]+rng(s)%64;r[3]._u32[lane]=r[3]._u32[0]-rng(s)%64;}
    // Scatter bases near the protected boundary and the 2^18 residue wrap.
    for(unsigned reg:{67u,76u}){
        const uint32_t base=pick(s,{0x28000,0xe680,0xe670,0x3fff0,0x40000,0x3e000,0x7ffe680u,rng(s)});
        for(auto& w:r[reg]._u32)w=base;
    }
    for(unsigned lane=0;lane<4;++lane){
        r[55]._u32[lane]=pick(s,{16,32,48,rng(s)%256,rng(s)});
        r[54]._u32[lane]=pick(s,{lane,lane*16,rng(s)%1024,rng(s)});
        r[53]._u32[lane]=pick(s,{0,8,rng(s)%16,rng(s)%0x40000,rng(s)});
        r[31]._u32[lane]=pick(s,{64,256,1024,0,0xffffffc0u,0x10000,0x40000,rng(s)%4096,rng(s)});
        r[30]._u32[lane]=pick(s,{0x9000,0x9040,0,0x3e000,rng(s)});
    }
    r[48]._u32[0]=pick(s,{0,1,8,15,16,17,rng(s)});
    if(rng(s)%2)return;
    // Production-like walk (the oracle tests' layout), with single perturbed
    // fields that move quads across 0xe680, the 2^18 residue and the budgets.
    const unsigned columns=1+rng(s)%16,stride=pick(s,{12,16,20,24,32,48});
    r[77]._u32[0]=1;r[60]._u32[0]=columns;r[48]._u32[0]=rng(s)%17;
    r[67]=spu_ila(pick(s,{0x28000,0x10000,0x3f000,0x3ffc0}));r[76]=spu_a(r[67],spu_il(16));
    r[52]=spu_ila(0x10000);r[3]=spu_ila(0x10000+columns*16*(1+rng(s)%300)-1);
    r[29]=spu_il(columns*64);r[31]=spu_il(columns*4*stride);r[55]=spu_il(stride);r[53]=spu_il(rng(s)%16);
    for(unsigned lane=0;lane<4;++lane){r[54]._u32[lane]=lane*columns;r[30]._u32[lane]=0x9000+lane*64;}
    switch(rng(s)%8){
    case 0:r[67]._u32[0]=0xe680-uint32_t(rng(s)%64);break;          // first quad just below the boundary
    case 1:r[67]._u32[0]=0x40000-uint32_t(rng(s)%0x2000);break;     // progression crosses the 2^18 window
    case 2:r[30]._u32[rng(s)%4]=rng(s)%0x10000;break;               // only the after-route can fail
    case 3:r[31]._u32[rng(s)%4]=0xffffff00u+rng(s)%256;break;       // descending route
    case 4:r[3]._u32[0]=r[52]._u32[0]+columns*64*uint32_t(65530+rng(s)%12);break; // group budget edge
    case 5:r[52]._u32[0]=0x7fff0000u;r[3]._u32[0]=0x7ffffff0u;break; // signed cursor wrap
    default:break;
    }
}
template<class Plan,class Read>
static void check(const char* name,const spu_context& c,const u128* r,const Walk& expected,Read read,unsigned trial,unsigned& proved)
{
    Plan plan;plan.columns=123;plan.groups=456;plan.cross_columns=789;const char* why=nullptr;
    const bool accept=read(c,plan,&why,r);
    if(accept!=expected.accept || (accept && (plan.columns!=expected.columns || plan.groups!=expected.groups || plan.cross_columns!=expected.cross))){
        fprintf(stderr,"%s trial%u: walk %d/%u/%u/%u, read %d/%u/%u/%u (%s)\n",name,trial,expected.accept,expected.columns,expected.groups,expected.cross,
            accept,plan.columns,plan.groups,plan.cross_columns,why?why:"-");
        exit(1);
    }
    proved+=accept;
}
int main(int argc,char** argv)
{
    const unsigned trials=argc>1?unsigned(atoi(argv[1])):200000;
    auto c=std::make_unique<spu_context>();memset(c.get(),0,sizeof *c);c->ls=c->ls_store;
    static u128 r[128];uint64_t state=0x9e3779b97f4a7c15ull;
    unsigned accepted[2]{},rejected[2]{},proof_accepts=0,fast=0;
    for(unsigned trial=0;trial<trials;++trial){
        seed(r,state);
        const Walk integer=integer_walk(r),floating=float_walk(r);
        ++(integer.accept?accepted:rejected)[0];++(floating.accept?accepted:rejected)[1];
        unsigned dummy=0;
        check<GeometryHalfOutput>("half",*c,r,integer,read_geometry_half_output,trial,dummy);
        check<GeometryCmpOutput>("cmp",*c,r,integer,read_geometry_cmp_output,trial,dummy);
        check<GeometryI16Output>("i16",*c,r,integer,read_geometry_i16_output,trial,dummy);
        check<GeometrySnormOutput>("snorm",*c,r,integer,read_geometry_snorm_output,trial,dummy);
        check<GeometryUnormOutput>("unorm",*c,r,integer,read_geometry_unorm_output,trial,dummy);
        check<GeometryU8IntegerOutput>("u8",*c,r,integer,read_geometry_u8_integer_output,trial,dummy);
        check<GeometryFloatOutput>("float",*c,r,floating,read_geometry_float_output,trial,dummy);
        // How often the closed form itself decides (coverage, not correctness).
        if(r[77]._u32[0]){
            const unsigned columns=r[60]._u32[0]?r[60]._u32[0]:1;unsigned total=0,cross=0;
            if(columns<=256 && prove_geometry_output_walk(geometry_integer_output_walk(r),columns,total,cross))++fast;
            if(integer.accept)++proof_accepts;
        }
    }
    printf("PASS output admission: %u trials x7 encoders; integer walk %u accept/%u reject, float walk %u accept/%u reject; closed form decided %u of %u nonempty integer accepts\n",
        trials,accepted[0],rejected[0],accepted[1],rejected[1],fast,proof_accepts);
    return 0;
}
