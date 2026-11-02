#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_animation_host_vectors.h"
#include "../port/native_animation_host_additive.h"
#include "../port/native_animation_host_hierarchy.h"
#include "../port/native_animation_job_input.h"
#include "../port/native_animation_host_matrices.h"
#include "zlib.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

struct Transfer { uint32_t kind,lsa,ea,size;std::vector<uint8_t> data; };
static std::vector<Transfer> transfers;
static size_t transfer_index=0;
static std::string current_case;
static uint64_t jobs=0,total_transfers=0,quaternion_calls=0,nonempty_calls=0;
static buzz::AnimationJobInput pending_job_input;
static unsigned planned_publication=0;
static uint64_t frontend_jobs=0,frontend_nodes[2]{},frontend_clips=0,frontend_publications=0;
static std::map<unsigned,uint64_t> quaternion_counts;
static std::filesystem::path output_directory;
static bool dump_started=false,dump_finished=false;
static bool probe_enabled=true;
static std::array<buzz::AnimationHostQuaternion8,32> pending_quaternions;
static std::vector<uint16_t> pending_indices;
static unsigned pending_base=0;
static uint64_t compared_quaternions=0;
static uint64_t replaced_quaternion_calls=0;
static bool replace_quaternions=false;
static bool corrupt_quaternions=false,corrupt_vectors=false,corrupt_matrices=false,corrupt_affines=false,corrupt_blend=false,corrupt_hierarchy=false,corrupt_extra_pose=false,corrupt_extra_curve=false,corruption_injected=false;
static uint64_t numeric_cases=0,numeric_samples=0;
static std::array<buzz::AnimationHostQuaternion8,32> pending_static_quaternions;
static std::vector<uint16_t> pending_static_indices;
static unsigned pending_static_base=0;
static uint64_t static_calls=0,static_nonempty=0,compared_static=0,replaced_static=0,numeric_static=0;
struct VectorObservation { bool started=false,finished=false;uint64_t calls=0,nonempty=0; };
static VectorObservation vector_observation[4],pose_observation[3],extra_observation[3];
static std::map<std::array<uint32_t,4>,uint64_t> extra_recipes[3];
static std::array<buzz::AnimationHostPose8,32> pending_extra_pose;
static std::array<v8,32> pending_extra_curve;
static unsigned pending_extra_base[3]{},pending_extra_weights[3]{},pending_extra_count[3]{};
static uint64_t compared_extra[3]{},replaced_extra[3]{},numeric_extra[3]{},extra_curve_flags[4]{};
static bool animation_native_extra_override(unsigned,spu_context*);
static void check_numeric_extra(const spu_context*);
static std::map<std::array<uint32_t,4>,std::pair<uint64_t,uint64_t>> blend_recipes;
static std::array<buzz::AnimationHostVector8,32> pending_vectors;
static std::vector<uint16_t> pending_vector_indices;
static unsigned pending_vector_base=0;
static uint64_t compared_vectors=0,replaced_vectors=0,numeric_vectors=0;
static std::array<buzz::AnimationHostWorldMatrix8,32> pending_matrices;
static unsigned pending_matrix_base=0,pending_matrix_count=0;
static uint64_t compared_matrices=0,replaced_matrices=0,numeric_matrices=0;
static std::array<uint8_t,256*48> pending_affines;
static std::array<uint8_t,256*64> pending_world_bytes;
static unsigned pending_affine_base=0,pending_affine_count=0;
static uint64_t composed_affines=0,compared_affines=0,replaced_affines=0;
static bool animation_native_affine_override(spu_context*);
static std::array<buzz::AnimationHostPose8,32> pending_blend;
static unsigned pending_blend_base=0,pending_blend_weights=0,pending_blend_count=0;
static uint64_t compared_blends=0,replaced_blends=0,numeric_blends=0;
static void check_numeric_blends(const spu_context*);
static bool animation_native_blend_override(spu_context*);
static buzz::AnimationHostHierarchyPlan pending_hierarchy_plan;
static buzz::AnimationHostPoseArrays pending_hierarchy_local,pending_hierarchy_world;
static unsigned pending_hierarchy_base=0;
static uint64_t compared_hierarchy=0,replaced_hierarchy=0,numeric_hierarchy=0;
static bool animation_native_hierarchy_override(spu_context*);
static void check_numeric_hierarchy(const spu_context*);
static uint64_t hierarchy_plans=0,hierarchy_entries=0,hierarchy_batches=0,hierarchy_lanes=0,hierarchy_full_batches=0,hierarchy_flags=0;
static void check_numeric_matrices(const spu_context*);
static bool animation_native_matrix_override(spu_context*);
static void check_numeric_vectors(const spu_context*);
static bool animation_native_vector_override(spu_context*);
static void check_numeric_quaternions(const spu_context*);
static void animation_original_probe(unsigned,spu_context*);
static bool animation_native_quaternion_override(spu_context*);
static bool animation_native_static_quaternion_override(spu_context*);
static void drain(spu_context*,unsigned);
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* c,uint32_t stop){drain(c,stop);}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_stop(spu_context* c){fprintf(stderr,"unexpected stop %s pc=%X\n",current_case.c_str(),c->pc);abort();}
void spu_halt(spu_context* c){fprintf(stderr,"unexpected halt %s pc=%X\n",current_case.c_str(),c->pc);abort();}
void spu_wrch(spu_context* c,uint32_t channel,u128 value)
{
    const auto v=value._u32[0];
    switch(channel) {
    case MFC_LSA:c->mfc_lsa=v;return;
    case MFC_EAH:c->mfc_eah=v;return;
    case MFC_EAL:c->mfc_eal=v;return;
    case MFC_Size:c->mfc_size=v&0xffff;return;
    case MFC_TagID:c->mfc_tag=v&31;return;
    case MFC_WrTagMask:c->mfc_tag_mask=v;return;
    case MFC_WrTagUpdate:return;
    case MFC_Cmd: {
        const unsigned command=v&0xff,kind=(command&~3u)==0x40?1:(command&~3u)==0x20?2:0;
        assert(kind&&c->mfc_eah==0&&transfer_index<transfers.size());
        const auto& expected=transfers[transfer_index++];
        const unsigned lsa=c->mfc_lsa&SPU_LS_MASK;
        if(expected.kind!=kind||expected.lsa!=lsa||expected.ea!=c->mfc_eal||expected.size!=c->mfc_size) {
            fprintf(stderr,"transfer mismatch %s index=%zu actual=%u,%X,%X,%u expected=%u,%X,%X,%u\n",current_case.c_str(),transfer_index-1,kind,lsa,c->mfc_eal,c->mfc_size,expected.kind,expected.lsa,expected.ea,expected.size);abort();
        }
        assert(uint64_t(lsa)+expected.size<=SPU_LS_SIZE);
        if(kind==2) {
            assert(planned_publication<pending_job_input.publications);
            const auto& planned=pending_job_input.output[planned_publication++];
            if(lsa!=planned.lsa||c->mfc_eal!=planned.ea||c->mfc_size!=planned.bytes||c->mfc_tag!=pending_job_input.dma_tag) {
                fprintf(stderr,"frontend publication mismatch %s publication=%u actual=%X,%X,%u,tag%u planned=%X,%X,%u,tag%u\n",current_case.c_str(),planned_publication-1,lsa,c->mfc_eal,c->mfc_size,c->mfc_tag,planned.lsa,planned.ea,planned.bytes,pending_job_input.dma_tag);abort();
            }
            ++frontend_publications;
        }
        if(kind==1)memcpy(c->ls+lsa,expected.data.data(),expected.size);
        else if(memcmp(c->ls+lsa,expected.data.data(),expected.size)) {
            fprintf(stderr,"published bytes mismatch %s transfer=%zu\n",current_case.c_str(),transfer_index-1);abort();
        }
        c->mfc_tag_status|=1u<<c->mfc_tag;return;
    }
    case SPU_WrOutMbox:spu_channel_write(&c->ch_out_mbox,v);return;
    case SPU_WrOutIntrMbox:spu_channel_write(&c->ch_out_intr_mbox,v);return;
    default:fprintf(stderr,"unexpected write channel=%u %s\n",channel,current_case.c_str());abort();
    }
}
u128 spu_rdch(spu_context* c,uint32_t channel)
{
    switch(channel) {
    case MFC_RdTagStat:return spu_il(c->mfc_tag_status&c->mfc_tag_mask);
    case SPU_RdMachStat:return spu_il(c->int_enable&1);
    case SPU_RdInMbox:assert(spu_channel_has_data(&c->ch_in_mbox));return spu_il(spu_channel_read(&c->ch_in_mbox));
    default:fprintf(stderr,"unexpected read channel=%u %s\n",channel,current_case.c_str());abort();
    }
}
uint32_t spu_rchcnt(spu_context* c,uint32_t channel){assert(channel==SPU_RdInMbox);return c->ch_in_mbox.count;}
}
#include "../analysis/native-geometry/phase4/job-oracle-v13/reference.inc"
extern "C" void spu_indirect_branch(spu_context* c)
{
    auto next=animation_original_lookup(c->pc&SPU_LS_MASK);
    if(!next){fprintf(stderr,"missing original PC=%X %s\n",c->pc,current_case.c_str());abort();}next(c);
}
static void drain(spu_context* c,unsigned stop)
{
    unsigned steps=0;
    while(g_spu_trampoline_fn) {
        assert(++steps<1048576);
        if((c->pc&SPU_LS_MASK)==stop){g_spu_trampoline_fn=nullptr;break;}
        auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(c);
    }
    if(c->pc!=stop){fprintf(stderr,"original return mismatch pc=%X stop=%X %s\n",c->pc,stop,current_case.c_str());abort();}
}
static void dump(const char* name,const void* data,size_t bytes)
{
    auto* file=fopen((output_directory/name).string().c_str(),"wb");assert(file);
    assert(fwrite(data,1,bytes,file)==bytes);assert(!fclose(file));
}
static void animation_original_probe(unsigned pc,spu_context* c)
{
    if(!probe_enabled)return;
    if(pc==0x3888||pc==0x42f0||pc==0x4838||pc==0x4ac8||pc==0x4ad0||pc==0x4d58) {
        const unsigned kind=(pc==0x3888||pc==0x42f0)?0:(pc==0x4838||pc==0x4ac8)?1:2;
        const char* names[]={"pose-operation1","curve-operation0","curve-operation1"};
        auto& observation=extra_observation[kind];
        if(pc==0x3888||pc==0x4838||pc==0x4ad0) {
            const unsigned logical=c->gpr[kind==2?12:11]._u32[0];assert(logical<=256);
            // Every aligned curve word is published, including its tail.
            const unsigned count=kind?(logical+3)&~3u:logical;
            const unsigned a=c->gpr[5]._u32[0],b=c->gpr[7]._u32[0];
            pending_extra_base[kind]=c->gpr[3]._u32[0];pending_extra_weights[kind]=c->gpr[4]._u32[0];pending_extra_count[kind]=count;
            assert(!c->gpr[8]._u32[0]);
            if(kind!=1){assert(c->gpr[kind==2?10:9]._u32[0]==0x3f800000&&c->gpr[kind==2?11:10]._u32[0]==0);}
            if(c->gpr[6]._u32[0]){assert(uint64_t(c->gpr[6]._u32[0])+count<=SPU_LS_SIZE);for(unsigned i=0;i<count;++i)assert(c->ls[c->gpr[6]._u32[0]+i]==255);}
            for(unsigned first=0;first<count;first+=8) {
                const unsigned lanes=std::min(8u,count-first);
                if(!kind) {
                    assert(uint64_t(a)+count*48<=SPU_LS_SIZE&&uint64_t(b)+count*48<=SPU_LS_SIZE);
                    pending_extra_pose[first/8]=buzz::add_animation_host_poses8(buzz::decode_animation_host_pose8(c->ls+a+first*48,lanes),buzz::decode_animation_host_pose8(c->ls+b+first*48,lanes));
                } else {
                    const unsigned flags=c->gpr[9]._u32[0];assert(uint64_t(a)+count*4<=SPU_LS_SIZE&&uint64_t(b)+count*4<=SPU_LS_SIZE&&uint64_t(flags)+count<=SPU_LS_SIZE);
                    alignas(32) uint32_t mask[8]{};
                    for(unsigned lane=0;lane<lanes;++lane){const unsigned value=c->ls[flags+first+lane];if(value>1){fprintf(stderr,"uncovered curve flag=%u %s\n",value,current_case.c_str());abort();}++extra_curve_flags[value];mask[lane]=value?~0u:0;}
                    const auto x=buzz::decode_geometry_host_floats8(c->ls+a+first*4,4,1,lanes).component[0],y=buzz::decode_geometry_host_floats8(c->ls+b+first*4,4,1,lanes).component[0];
                    const v8 clamp=_mm256_load_si256(reinterpret_cast<const v8*>(mask));
                    pending_extra_curve[first/8]=kind==1?buzz::blend_animation_host_curves8(x,y,v8_set(c->gpr[10]._u32[0]),clamp):buzz::add_animation_host_curves8(x,y,clamp);
                }
            }
            if(!kind&&count&&!observation.started)check_numeric_extra(c);
        } else if(!replace_quaternions) {
            for(unsigned first=0;first<pending_extra_count[kind];first+=8) {
                alignas(32) uint32_t values[12][8];
                const unsigned components=kind?1:12,lanes=std::min(8u,pending_extra_count[kind]-first);
                if(kind)_mm256_store_si256(reinterpret_cast<v8*>(values[0]),pending_extra_curve[first/8]);
                else for(unsigned component=0;component<4;++component){_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_extra_pose[first/8].rotation.component[component]);_mm256_store_si256(reinterpret_cast<v8*>(values[4+component]),pending_extra_pose[first/8].translation.component[component]);_mm256_store_si256(reinterpret_cast<v8*>(values[8+component]),pending_extra_pose[first/8].scale.component[component]);}
                for(unsigned lane=0;lane<lanes;++lane)for(unsigned component=0;component<components;++component){const unsigned at=pending_extra_base[kind]+(first+lane)*components*4+component*4;assert(at+4<=SPU_LS_SIZE);const auto* p=c->ls+at;const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];if(expected!=values[component][lane]){fprintf(stderr,"native extra mismatch kind=%u %s index=%u component=%u expected=%08X actual=%08X\n",kind,current_case.c_str(),first+lane,component,expected,values[component][lane]);abort();}}
                compared_extra[kind]+=lanes;
            }
            if(pending_extra_weights[kind])for(unsigned i=0;i<pending_extra_count[kind];++i)assert(c->ls[pending_extra_weights[kind]+i]==255);
        }
        if(pc==0x3888||pc==0x4838||pc==0x4ad0) {
            const unsigned count=c->gpr[kind==2?12:11]._u32[0];
            ++observation.calls;observation.nonempty+=count!=0;
            ++extra_recipes[kind][{c->gpr[kind?10:9]._u32[0],kind==1?0:c->gpr[kind==2?11:10]._u32[0],c->gpr[6]._u32[0]!=0,c->gpr[8]._u32[0]!=0}];
            if(count&&!observation.started) {
                observation.started=true;printf("first %s %s count=%u\n",names[kind],current_case.c_str(),count);
                for(unsigned r=3;r<=12;++r)printf("%s arg%u %08X %08X %08X %08X\n",names[kind],r,c->gpr[r]._u32[0],c->gpr[r]._u32[1],c->gpr[r]._u32[2],c->gpr[r]._u32[3]);
                dump((std::string(names[kind])+"-entry-arguments.bin").c_str(),c->gpr+3,10*sizeof(u128));
                dump((std::string(names[kind])+"-entry-ls.bin").c_str(),c->ls,SPU_LS_SIZE);
            }
        } else if(observation.started&&!observation.finished) {
            observation.finished=true;dump((std::string(names[kind])+"-exit-ls.bin").c_str(),c->ls,SPU_LS_SIZE);
        }
        return;
    }
    if(pc==0x4fb8) {
        const unsigned at=c->gpr[6]._u32[0],entries=c->gpr[7]._u32[0];assert(at<=SPU_LS_SIZE);
        assert(at==pending_job_input.hierarchy.lsa&&entries==pending_job_input.hierarchy_entries&&c->gpr[5]._u32[0]==pending_job_input.root_pose_lsa);
        if(!buzz::plan_animation_host_hierarchy({c->ls+at,SPU_LS_SIZE-at},entries,pending_blend_count,pending_hierarchy_plan)) {
            fprintf(stderr,"hierarchy plan rejected %s entries=%u joints=%u\n",current_case.c_str(),entries,pending_blend_count);abort();
        }
        ++hierarchy_plans;hierarchy_entries+=entries;hierarchy_batches+=pending_hierarchy_plan.batches;hierarchy_lanes+=pending_hierarchy_plan.joints;
        for(unsigned i=0;i<pending_hierarchy_plan.batches;++i)hierarchy_full_batches+=pending_hierarchy_plan.batch_size[i]==8;
        for(unsigned i=0;i<pending_hierarchy_plan.joints;++i)hierarchy_flags+=pending_hierarchy_plan.no_scale_inheritance[i];
        const unsigned source=c->gpr[4]._u32[0],root=c->gpr[5]._u32[0];pending_hierarchy_base=c->gpr[3]._u32[0];
        assert(uint64_t(source)+pending_blend_count*48<=SPU_LS_SIZE&&root+48<=SPU_LS_SIZE);
        for(unsigned first=0;first<pending_blend_count;first+=8) {
            const unsigned count=std::min(8u,pending_blend_count-first);uint16_t indices[8];for(unsigned lane=0;lane<count;++lane)indices[lane]=first+lane;
            buzz::scatter_animation_host_poses8(buzz::decode_animation_host_pose8(c->ls+source+first*48,count),pending_hierarchy_local,indices,count);
        }
        buzz::produce_animation_host_hierarchy(pending_hierarchy_plan,pending_hierarchy_local,
            buzz::decode_animation_host_pose8(c->ls+root,1),pending_hierarchy_world);
        if(!pose_observation[1].started)check_numeric_hierarchy(c);
    }
    if(pc==0x5730&&!replace_quaternions) {
        for(unsigned joint=0;joint<pending_hierarchy_plan.joints;++joint)for(unsigned component=0;component<12;++component) {
            const unsigned at=pending_hierarchy_base+joint*48+component*4;assert(at+4<=SPU_LS_SIZE);const auto* p=c->ls+at;
            const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
            if(expected!=pending_hierarchy_world.component[component][joint]){fprintf(stderr,"native hierarchy mismatch %s joint=%u component=%u expected=%08X actual=%08X\n",current_case.c_str(),joint,component,expected,pending_hierarchy_world.component[component][joint]);abort();}
        }
        compared_hierarchy+=pending_hierarchy_plan.joints;
    }
    if(pc==0x3188) {
        const std::array<uint32_t,4> shape{c->gpr[9]._u32[0],c->gpr[6]._u32[0]!=0,c->gpr[8]._u32[0]!=0,c->gpr[4]._u32[0]!=0};
        auto& record=blend_recipes[shape];
        static std::map<std::array<uint32_t,4>,bool> dumped_forms;
        auto form=shape;form[0]=shape[0]==0?0:shape[0]==0x3f800000?1:2;
        if(!dumped_forms[form]) {
            dumped_forms[form]=true;
            const auto name="blend-form-"+std::to_string(dumped_forms.size());
            dump((name+"-arguments.bin").c_str(),c->gpr+3,8*sizeof(u128));dump((name+"-ls.bin").c_str(),c->ls,SPU_LS_SIZE);
        }
        ++record.first;record.second+=c->gpr[10]._u32[0];
        assert(!shape[1]&&!shape[2]); // Only genuinely covered implicit-weight recipe.
        pending_blend_base=c->gpr[3]._u32[0];pending_blend_weights=c->gpr[4]._u32[0];pending_blend_count=c->gpr[10]._u32[0];
        const unsigned a=c->gpr[5]._u32[0],b=c->gpr[7]._u32[0];
        assert(pending_blend_count<=256&&uint64_t(a)+pending_blend_count*48<=SPU_LS_SIZE&&uint64_t(b)+pending_blend_count*48<=SPU_LS_SIZE);
        const v8 factor=v8_set(c->gpr[9]._u32[0]);
        for(unsigned first=0;first<pending_blend_count;first+=8) {
            const unsigned count=std::min(8u,pending_blend_count-first);
            pending_blend[first/8]=buzz::blend_animation_host_poses8(
                buzz::decode_animation_host_pose8(c->ls+a+first*48,count),buzz::decode_animation_host_pose8(c->ls+b+first*48,count),factor);
        }
        if(pending_blend_count&&!pose_observation[0].started)check_numeric_blends(c);
    }
    if(pc==0x3810&&!replace_quaternions) {
        for(unsigned first=0;first<pending_blend_count;first+=8) {
            alignas(32) uint32_t words[12][8];
            for(unsigned component=0;component<4;++component) {
                _mm256_store_si256(reinterpret_cast<v8*>(words[component]),pending_blend[first/8].rotation.component[component]);
                _mm256_store_si256(reinterpret_cast<v8*>(words[4+component]),pending_blend[first/8].translation.component[component]);
                _mm256_store_si256(reinterpret_cast<v8*>(words[8+component]),pending_blend[first/8].scale.component[component]);
            }
            const unsigned count=std::min(8u,pending_blend_count-first);
            for(unsigned lane=0;lane<count;++lane)for(unsigned component=0;component<12;++component) {
                const unsigned at=pending_blend_base+(first+lane)*48+component*4;assert(at+4<=SPU_LS_SIZE);const auto* p=c->ls+at;
                const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
                if(words[component][lane]!=expected){fprintf(stderr,"native blend mismatch %s joint=%u component=%u expected=%08X actual=%08X\n",current_case.c_str(),first+lane,component,expected,words[component][lane]);abort();}
            }
            compared_blends+=count;
        }
        if(pending_blend_weights){assert(uint64_t(pending_blend_weights)+pending_blend_count<=SPU_LS_SIZE);for(unsigned i=0;i<pending_blend_count;++i)assert(c->ls[pending_blend_weights+i]==255);}
    }
    if(pc==0x20) {
        pending_affine_base=c->gpr[3]._u32[0];pending_affine_count=c->gpr[6]._u32[0];
        const unsigned source=c->gpr[5]._u32[0];
        assert(source==pending_job_input.input[2].lsa&&pending_affine_base==pending_job_input.output[0].lsa);
        assert(c->gpr[4]._u32[0]==pending_matrix_base&&pending_affine_count==pending_matrix_count&&pending_affine_count<=256);
        assert(uint64_t(source)+pending_affine_count*48<=SPU_LS_SIZE);
        for(unsigned first=0;first<pending_affine_count;first+=8) {
            const unsigned count=std::min(8u,pending_affine_count-first);
            buzz::emit_animation_host_world_matrices8(pending_matrices[first/8],pending_world_bytes.data()+first*64,count);
            buzz::AnimationHostAffine8 world;
            for(unsigned coefficient=0;coefficient<12;++coefficient)world.coefficient[coefficient]=pending_matrices[first/8].coefficient[coefficient];
            const auto reference=buzz::decode_animation_host_affines8(c->ls+source+first*48,48,count);
            buzz::emit_animation_host_affines8(buzz::compose_animation_host_affines8(world,reference),pending_affines.data()+first*48,count);
        }
        composed_affines+=pending_affine_count;return;
    }
    if(pc==0x10020) {
        assert(uint64_t(pending_affine_base)+pending_affine_count*48<=SPU_LS_SIZE);
        if(!replace_quaternions) {
            if(memcmp(c->ls+pending_affine_base,pending_affines.data(),pending_affine_count*48)){fprintf(stderr,"native QTS-to-reference affine mismatch %s\n",current_case.c_str());abort();}
            if(memcmp(c->ls+pending_matrix_base,pending_world_bytes.data(),pending_matrix_count*64)){fprintf(stderr,"native published world matrix mismatch %s\n",current_case.c_str());abort();}
            compared_affines+=pending_affine_count;
        }
        return;
    }
    if(pc==0x4d60) {
        pending_matrix_base=c->gpr[3]._u32[0];pending_matrix_count=c->gpr[5]._u32[0];
        assert(pending_matrix_base==pending_job_input.output[1].lsa&&pending_matrix_count==pending_job_input.joints);
        const unsigned source=c->gpr[4]._u32[0];assert(pending_matrix_count<=256&&uint64_t(source)+pending_matrix_count*48<=SPU_LS_SIZE);
        for(unsigned first=0;first<pending_matrix_count;first+=8)pending_matrices[first/8]=buzz::make_animation_host_world_matrices8(
            buzz::decode_animation_host_pose8(c->ls+source+first*48,std::min(8u,pending_matrix_count-first)));
        if(pending_matrix_count&&!pose_observation[2].started)check_numeric_matrices(c);
    }
    if(pc==0x4fb0&&!replace_quaternions) {
        for(unsigned first=0;first<pending_matrix_count;first+=8) {
            alignas(32) uint32_t values[16][8];
            for(unsigned coefficient=0;coefficient<16;++coefficient)_mm256_store_si256(reinterpret_cast<v8*>(values[coefficient]),pending_matrices[first/8].coefficient[coefficient]);
            const unsigned count=std::min(8u,pending_matrix_count-first);
            for(unsigned lane=0;lane<count;++lane)for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column) {
                const unsigned at=pending_matrix_base+(first+lane)*64+column*16+row*4;assert(at+4<=SPU_LS_SIZE);const auto* p=c->ls+at;
                const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
                if(expected!=values[row*4+column][lane]){fprintf(stderr,"native pose matrix mismatch %s joint=%u row=%u column=%u expected=%08X actual=%08X\n",current_case.c_str(),first+lane,row,column,expected,values[row*4+column][lane]);abort();}
            }
            compared_matrices+=count;
        }
    }
    if(pc==0x3188||pc==0x3810||pc==0x4fb8||pc==0x5730||pc==0x4d60||pc==0x4fb0) {
        const unsigned kind=(pc==0x3188||pc==0x3810)?0:(pc==0x4fb8||pc==0x5730)?1:2;
        const char* names[]={"pose-blend","pose-hierarchy","pose-matrix"};
        auto& observation=pose_observation[kind];const char* name=names[kind];
        if(pc==0x3188||pc==0x4fb8||pc==0x4d60) {
            const unsigned count=c->gpr[kind==0?10:kind==1?7:5]._u32[0];
            ++observation.calls;observation.nonempty+=count!=0;
            if(count&&!observation.started) {
                observation.started=true;printf("first %s %s count=%u\n",name,current_case.c_str(),count);
                for(unsigned r=3;r<=12;++r)printf("%s arg%u %08X %08X %08X %08X\n",name,r,c->gpr[r]._u32[0],c->gpr[r]._u32[1],c->gpr[r]._u32[2],c->gpr[r]._u32[3]);
                dump((std::string(name)+"-entry-arguments.bin").c_str(),c->gpr+3,10*sizeof(u128));
                dump((std::string(name)+"-entry-ls.bin").c_str(),c->ls,SPU_LS_SIZE);
            }
        } else if(observation.started&&!observation.finished) {
            observation.finished=true;dump((std::string(name)+"-exit-ls.bin").c_str(),c->ls,SPU_LS_SIZE);
        }
        return;
    }
    if(pc==0x9a18) {
        const unsigned count=c->gpr[17]._u32[0];assert(count<=256);
        pending_vector_indices.clear();pending_vector_base=c->gpr[18]._u32[0];
        auto view=[&](unsigned at){assert(at<=SPU_LS_SIZE);return buzz::AnimationByteView{c->ls+at,SPU_LS_SIZE-at};};
        const unsigned bit=c->gpr[3]._u32[0];
        buzz::AnimationVectorTracks recipe{view(bit/8),view(c->gpr[15]._u32[0]),view(c->gpr[16]._u32[0]),view(c->gpr[14]._u32[0]),bit%8,c->gpr[20]._u32[0],count,c->gpr[12]._u32[0],c->gpr[13]._u32[0]};
        assert(buzz::sample_animation_host_vectors(recipe,pending_vectors.data()));
        for(unsigned i=0;i<count;++i){const unsigned at=c->gpr[19]._u32[0]+i*2;assert(at+2<=SPU_LS_SIZE);pending_vector_indices.push_back(uint16_t(c->ls[at])<<8|c->ls[at+1]);}
        if(count&&!vector_observation[3].started)check_numeric_vectors(c);
    }
    if(pc==0x19a18&&!replace_quaternions) {
        for(unsigned i=0;i<pending_vector_indices.size();++i) {
            alignas(32) uint32_t values[4][8];
            for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_vectors[i/8].component[component]);
            const unsigned at=pending_vector_base+pending_vector_indices[i]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){const auto* p=c->ls+at+component*4;const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];if(values[component][i%8]!=expected){fprintf(stderr,"native FLOAT3 mismatch %s track=%u component=%u expected=%08X actual=%08X\n",current_case.c_str(),i,component,expected,values[component][i%8]);abort();}}
            ++compared_vectors;
        }
    }
    if(pc==0x7790||pc==0x85d0||pc==0x8688||pc==0x8d00||pc==0x9608||pc==0x9a10||pc==0x9a18||pc==0x19a18) {
        const unsigned kind=(pc==0x8688||pc==0x8d00)?0:(pc==0x7790||pc==0x85d0)?1:(pc==0x9608||pc==0x9a10)?2:3;
        constexpr const char* names[]={"format-8688","format-7790","format-9608","animated-float3"};
        auto& observation=vector_observation[kind];const char* name=names[kind];
        if(pc==0x7790||pc==0x8688||pc==0x9608||pc==0x9a18) {
            ++observation.calls;const unsigned count=c->gpr[(kind%2)?17:4]._u32[0];observation.nonempty+=count!=0;
            if(count&&!observation.started) {
                observation.started=true;printf("first %s producer %s count=%u\n",name,current_case.c_str(),count);
                for(unsigned r=3;r<=24;++r)printf("%s arg%u %08X %08X %08X %08X\n",name,r,c->gpr[r]._u32[0],c->gpr[r]._u32[1],c->gpr[r]._u32[2],c->gpr[r]._u32[3]);
                dump((std::string(name)+"-entry-arguments.bin").c_str(),c->gpr+3,22*sizeof(u128));
                dump((std::string(name)+"-entry-ls.bin").c_str(),c->ls,SPU_LS_SIZE);
            }
        } else if(observation.started&&!observation.finished) {
            observation.finished=true;dump((std::string(name)+"-exit-ls.bin").c_str(),c->ls,SPU_LS_SIZE);
        }
        return;
    }
    if(pc==0x7490) {
        ++static_calls;const unsigned count=c->gpr[5]._u32[0];assert(count<=256);
        if(static_nonempty==0&&count)printf("first static quaternion producer count=%u destination=%X keys=%X indices=%X permutation=%X\n",count,c->gpr[3]._u32[0],c->gpr[4]._u32[0],c->gpr[6]._u32[0],c->gpr[7]._u32[0]);
        static_nonempty+=count!=0;pending_static_base=c->gpr[3]._u32[0];pending_static_indices.clear();
        const unsigned at=c->gpr[4]._u32[0];assert(at<=SPU_LS_SIZE);
        assert(buzz::decode_animation_host_static_quaternions({c->ls+at,SPU_LS_SIZE-at},count,pending_static_quaternions.data()));
        for(unsigned i=0;i<count;++i){const unsigned id=c->gpr[6]._u32[0]+2*i;assert(id+2<=SPU_LS_SIZE);pending_static_indices.push_back(uint16_t(c->ls[id])<<8|c->ls[id+1]);}
        return;
    }
    if(pc==0x7788) {
        for(unsigned i=0;!replace_quaternions&&i<pending_static_indices.size();++i) {
            alignas(32) uint32_t values[4][8];
            for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_static_quaternions[i/8].component[component]);
            const unsigned at=pending_static_base+pending_static_indices[i]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){const auto* p=c->ls+at+component*4;const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];if(values[component][i%8]!=expected){fprintf(stderr,"static quaternion mismatch %s track=%u joint=%u component=%u expected=%08X actual=%08X\n",current_case.c_str(),i,pending_static_indices[i],component,expected,values[component][i%8]);abort();}}
            ++compared_static;
        }
        return;
    }
    if(pc==0x6950) {
        ++quaternion_calls;const unsigned count=c->gpr[17]._u32[0];++quaternion_counts[count];
        if(count)++nonempty_calls;
        assert(count<=256);pending_indices.clear();pending_base=c->gpr[18]._u32[0];
        auto view=[&](unsigned at){assert(at<=SPU_LS_SIZE);return buzz::AnimationByteView{c->ls+at,SPU_LS_SIZE-at};};
        const unsigned bit=c->gpr[3]._u32[0];
        buzz::AnimationQuaternionTracks recipe{view(bit/8),view(c->gpr[15]._u32[0]),view(c->gpr[16]._u32[0]),view(c->gpr[14]._u32[0]),bit%8,c->gpr[20]._u32[0],count,c->gpr[12]._u32[0],c->gpr[13]._u32[0]};
        if(!buzz::sample_animation_host_quaternions(recipe,pending_quaternions.data())){fprintf(stderr,"native quaternion recipe rejected %s frames=%u frame=%u count=%u\n",current_case.c_str(),recipe.interior_frames,recipe.frame,count);abort();}
        for(unsigned i=0;i<count;++i){const auto at=c->gpr[19]._u32[0]+2*i;assert(at+2<=SPU_LS_SIZE);pending_indices.push_back(uint16_t(c->ls[at])<<8|c->ls[at+1]);}
        if(count&&!dump_started) {
            dump_started=true;dump("quaternion-entry-arguments.bin",c->gpr+3,18*sizeof(u128));
            dump("quaternion-entry-ls.bin",c->ls,SPU_LS_SIZE);
            check_numeric_quaternions(c);
            printf("first quaternion producer: %s count=%u\n",current_case.c_str(),count);
            for(unsigned r=3;r<=20;++r)printf("arg%u %08X %08X %08X %08X\n",r,c->gpr[r]._u32[0],c->gpr[r]._u32[1],c->gpr[r]._u32[2],c->gpr[r]._u32[3]);
            // Observe the original prelude on a private copy. This is an
            // analysis snapshot, never the native representation or a gate.
            auto shadow=std::make_unique<spu_context>();memcpy(shadow.get(),c,sizeof(*c));shadow->ls=shadow->ls_store;
            auto saved=g_spu_trampoline_fn;probe_enabled=false;g_spu_trampoline_fn=nullptr;
            buzz_job_abe1_spu_func_00006950(shadow.get());
            assert(shadow->pc==0x6db8);
            for(unsigned r:{2,22,24,25,26,27,28,29,30,31,32,54,57,60,61,77,87,98})
                printf("loop%u %08X %08X %08X %08X\n",r,shadow->gpr[r]._u32[0],shadow->gpr[r]._u32[1],shadow->gpr[r]._u32[2],shadow->gpr[r]._u32[3]);
            g_spu_trampoline_fn=saved;probe_enabled=true;
        }
    } else {
        for(unsigned i=0;!replace_quaternions&&i<pending_indices.size();++i) {
            alignas(32) uint32_t values[4][8];
            for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_quaternions[i/8].component[component]);
            const unsigned at=pending_base+pending_indices[i]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){const auto* p=c->ls+at+component*4;const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];if(values[component][i%8]!=expected){fprintf(stderr,"native quaternion mismatch %s track=%u joint=%u component=%u expected=%08X actual=%08X\n",current_case.c_str(),i,pending_indices[i],component,expected,values[component][i%8]);abort();}}
            ++compared_quaternions;
        }
        if(dump_started&&!dump_finished){dump_finished=true;dump("quaternion-exit-ls.bin",c->ls,SPU_LS_SIZE);}
    }
}
static bool animation_native_quaternion_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x6950,c);
    for(unsigned first=0;first<pending_indices.size();first+=8) {
        alignas(32) uint32_t values[4][8];
        for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_quaternions[first/8].component[component]);
        const unsigned count=std::min(8u,unsigned(pending_indices.size())-first);
        for(unsigned lane=0;lane<count;++lane) {
            const unsigned at=pending_base+pending_indices[first+lane]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){auto* p=c->ls+at+component*4;const unsigned word=values[component][lane];p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;}
        }
    }
    ++replaced_quaternion_calls;animation_original_probe(0x7408,c);
    if(corrupt_quaternions&&!corruption_injected&&!pending_indices.empty()) {
        c->ls[pending_base+pending_indices[0]*48]^=0x80;corruption_injected=true;
    }
    // Only the original test caller's continuation is exposed. No volatile
    // GPRs, prefetch state, scratch or saved SPU frames are reconstructed.
    c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_static_quaternion_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x7490,c);
    for(unsigned first=0;first<pending_static_indices.size();first+=8) {
        alignas(32) uint32_t values[4][8];
        for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_static_quaternions[first/8].component[component]);
        const unsigned count=std::min(8u,unsigned(pending_static_indices.size())-first);
        for(unsigned lane=0;lane<count;++lane) {
            const unsigned at=pending_static_base+pending_static_indices[first+lane]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){auto* p=c->ls+at+component*4;const unsigned word=values[component][lane];p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;}
        }
    }
    ++replaced_static;c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_vector_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x9a18,c);
    for(unsigned first=0;first<pending_vector_indices.size();first+=8) {
        alignas(32) uint32_t values[4][8];
        for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_vectors[first/8].component[component]);
        const unsigned count=std::min(8u,unsigned(pending_vector_indices.size())-first);
        for(unsigned lane=0;lane<count;++lane) {
            const unsigned at=pending_vector_base+pending_vector_indices[first+lane]*48;assert(at+16<=SPU_LS_SIZE);
            for(unsigned component=0;component<4;++component){auto* p=c->ls+at+component*4;const unsigned word=values[component][lane];p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;}
        }
    }
    ++replaced_vectors;animation_original_probe(0x19a18,c);
    if(corrupt_vectors&&!corruption_injected&&!pending_vector_indices.empty()) {
        c->ls[pending_vector_base+pending_vector_indices[0]*48]^=0x80;corruption_injected=true;
    }
    c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_matrix_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x4d60,c);
    // Test adapter only: the original caller consumes column-major LS data.
    // Native computation and future frontend publication use host row-major SoA.
    for(unsigned first=0;first<pending_matrix_count;first+=8) {
        alignas(32) uint32_t values[16][8];
        for(unsigned coefficient=0;coefficient<16;++coefficient)_mm256_store_si256(reinterpret_cast<v8*>(values[coefficient]),pending_matrices[first/8].coefficient[coefficient]);
        const unsigned count=std::min(8u,pending_matrix_count-first);
        for(unsigned lane=0;lane<count;++lane)for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column) {
            const unsigned at=pending_matrix_base+(first+lane)*64+column*16+row*4;assert(at+4<=SPU_LS_SIZE);
            auto* p=c->ls+at;const unsigned word=values[row*4+column][lane];p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;
        }
    }
    ++replaced_matrices;animation_original_probe(0x4fb0,c);
    if(corrupt_matrices&&!corruption_injected&&pending_matrix_count) {
        pending_matrices[0].coefficient[0]=_mm256_xor_si256(pending_matrices[0].coefficient[0],_mm256_setr_epi32(0x80000000u,0,0,0,0,0,0,0));corruption_injected=true;
    }
    c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_affine_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x20,c);
    assert(uint64_t(pending_affine_base)+pending_affine_count*48<=SPU_LS_SIZE);
    memcpy(c->ls+pending_affine_base,pending_affines.data(),pending_affine_count*48);
    assert(uint64_t(pending_matrix_base)+pending_matrix_count*64<=SPU_LS_SIZE);
    memcpy(c->ls+pending_matrix_base,pending_world_bytes.data(),pending_matrix_count*64);
    ++replaced_affines;
    if(corrupt_affines&&!corruption_injected&&pending_affine_count){c->ls[pending_affine_base]^=0x80;corruption_injected=true;}
    c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_blend_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x3188,c);
    for(unsigned first=0;first<pending_blend_count;first+=8) {
        alignas(32) uint32_t words[12][8];
        for(unsigned component=0;component<4;++component) {
            _mm256_store_si256(reinterpret_cast<v8*>(words[component]),pending_blend[first/8].rotation.component[component]);
            _mm256_store_si256(reinterpret_cast<v8*>(words[4+component]),pending_blend[first/8].translation.component[component]);
            _mm256_store_si256(reinterpret_cast<v8*>(words[8+component]),pending_blend[first/8].scale.component[component]);
        }
        const unsigned count=std::min(8u,pending_blend_count-first);
        for(unsigned lane=0;lane<count;++lane)for(unsigned component=0;component<12;++component) {
            const unsigned at=pending_blend_base+(first+lane)*48+component*4;assert(at+4<=SPU_LS_SIZE);
            const unsigned value=words[component][lane];auto* p=c->ls+at;p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value;
        }
    }
    if(pending_blend_weights){assert(uint64_t(pending_blend_weights)+pending_blend_count<=SPU_LS_SIZE);memset(c->ls+pending_blend_weights,255,pending_blend_count);}
    ++replaced_blends;animation_original_probe(0x3810,c);
    if(corrupt_blend&&!corruption_injected&&pending_blend_count){c->ls[pending_blend_base]^=0x80;corruption_injected=true;}
    c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_hierarchy_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x4fb8,c);
    for(unsigned joint=0;joint<pending_hierarchy_plan.joints;++joint)for(unsigned component=0;component<12;++component) {
        const unsigned at=pending_hierarchy_base+joint*48+component*4;assert(at+4<=SPU_LS_SIZE);auto* p=c->ls+at;
        const unsigned word=pending_hierarchy_world.component[component][joint];p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;
    }
    ++replaced_hierarchy;animation_original_probe(0x5730,c);
    if(corrupt_hierarchy&&!corruption_injected&&pending_hierarchy_plan.joints){c->ls[pending_hierarchy_base]^=0x80;corruption_injected=true;}
    c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_extra_override(unsigned pc,spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    const unsigned kind=pc==0x3888?0:pc==0x4838?1:2;
    animation_original_probe(pc,c);
    for(unsigned first=0;first<pending_extra_count[kind];first+=8) {
        alignas(32) uint32_t values[12][8];const unsigned components=kind?1:12,lanes=std::min(8u,pending_extra_count[kind]-first);
        if(kind)_mm256_store_si256(reinterpret_cast<v8*>(values[0]),pending_extra_curve[first/8]);
        else for(unsigned component=0;component<4;++component){_mm256_store_si256(reinterpret_cast<v8*>(values[component]),pending_extra_pose[first/8].rotation.component[component]);_mm256_store_si256(reinterpret_cast<v8*>(values[4+component]),pending_extra_pose[first/8].translation.component[component]);_mm256_store_si256(reinterpret_cast<v8*>(values[8+component]),pending_extra_pose[first/8].scale.component[component]);}
        for(unsigned lane=0;lane<lanes;++lane)for(unsigned component=0;component<components;++component){const unsigned at=pending_extra_base[kind]+(first+lane)*components*4+component*4;assert(at+4<=SPU_LS_SIZE);auto* p=c->ls+at;const unsigned value=values[component][lane];p[0]=value>>24;p[1]=value>>16;p[2]=value>>8;p[3]=value;}
    }
    if(pending_extra_weights[kind]){assert(uint64_t(pending_extra_weights[kind])+pending_extra_count[kind]<=SPU_LS_SIZE);memset(c->ls+pending_extra_weights[kind],255,pending_extra_count[kind]);}
    ++replaced_extra[kind];animation_original_probe(kind==0?0x42f0:kind==1?0x4ac8:0x4d58,c);
    if(!corruption_injected&&pending_extra_count[kind]&&((!kind&&corrupt_extra_pose)||(kind&&corrupt_extra_curve))){c->ls[pending_extra_base[kind]]^=0x80;corruption_injected=true;}
    c->pc=c->gpr[0]._u32[0];return true;
}
static buzz::AnimationByteView guarded_view(const uint8_t* data,unsigned bytes,unsigned slot)
{
#ifdef _WIN32
    static uint8_t* pages[4]{};
    if(!pages[slot]){pages[slot]=static_cast<uint8_t*>(VirtualAlloc(nullptr,20480,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(pages[slot]);DWORD old;assert(VirtualProtect(pages[slot]+16384,4096,PAGE_NOACCESS,&old));}
    assert(bytes<=16384);DWORD old;assert(VirtualProtect(pages[slot],16384,PAGE_READWRITE,&old));
    auto* destination=pages[slot]+16384-bytes;if(bytes)memcpy(destination,data,bytes);
    assert(VirtualProtect(pages[slot],16384,PAGE_READONLY,&old));return {destination,bytes};
#else
    return {data,bytes};
#endif
}
static void check_numeric_quaternions(const spu_context* original)
{
    const auto saved=g_spu_trampoline_fn;const auto saved_case=current_case;probe_enabled=false;
    auto c=std::make_unique<spu_context>();
    constexpr unsigned bits_at=0x1c000,ids_at=0x1f000,first_at=0x20000,last_at=0x21000,keys_at=0x22000,dst=0x24000;
    constexpr unsigned widths[]={0,1,2,7,15,31,32,33,63,64,65,79,95,96,97,127,128};
    constexpr uint32_t fractions[]={0,0x80000000,0x3e800000,0x3f000000,0x3f7fbe77,1};
    uint32_t random=0xc451b6f2;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    auto pack=[&](unsigned salt) {
        const unsigned a=salt%5==0?16384:8000+next()%16000,b=salt%5==0?16384:8000+next()%16000,d=salt%5==0?16384:8000+next()%16000;
        return uint64_t(a)<<32|uint64_t(b)<<17|uint64_t(d)<<2|(salt%4);
    };
    auto put=[&](unsigned at,uint64_t value){for(int i=5;i>=0;--i){c->ls[at+i]=uint8_t(value);value>>=8;}};
    for(unsigned test=0;test<2048;++test) {
        memcpy(c.get(),original,sizeof(*original));c->ls=c->ls_store;
        memset(c->ls+bits_at,0,0x9000);
        const unsigned width=widths[test%std::size(widths)],count=1+test%8,first_bit=test%8;
        const unsigned frame=(test/17)%(width+1),key_bytes=(first_bit+count*width+7)/8;
        unsigned keys=0;
        for(unsigned track=0;track<count;++track) {
            const uint64_t endpoint=pack(test+track);put(first_at+track*6,endpoint);put(last_at+track*6,test%3?pack(test+track+3):endpoint);
            c->ls[ids_at+track*2]=0;c->ls[ids_at+track*2+1]=uint8_t(count-1-track);
            for(unsigned i=0;i<width;++i) if((test%7==0)||(test%7!=1&&next()%5==0)) {
                const unsigned bit=first_bit+track*width+i;c->ls[bits_at+bit/8]|=0x80u>>(bit%8);
                put(keys_at+keys++*6,pack(test+track+i));
            }
        }
        // Original lookahead stores target spare joints, outside logical data.
        for(unsigned i=count;i<count+8;++i)c->ls[ids_at+i*2+1]=uint8_t(i);
        buzz::AnimationQuaternionTracks recipe{
            guarded_view(c->ls+bits_at,key_bytes,0),guarded_view(c->ls+first_at,count*6,1),
            guarded_view(c->ls+last_at,count*6,2),guarded_view(c->ls+keys_at,keys*6,3),
            first_bit,width,count,frame,fractions[test%std::size(fractions)]};
        buzz::AnimationHostQuaternion8 actual;
        assert(buzz::sample_animation_host_quaternions(recipe,&actual));
        auto top=[&](unsigned n){return n==32?~0u:n?~0u<<(32-n):0u;};
        for(unsigned word=0;word<4;++word) {
            const unsigned valid=width>word*32?std::min(32u,width-word*32):0;
            const unsigned prefix=frame>word*32?std::min(32u,frame-word*32):0;
            c->gpr[4+word]=spu_il(top(valid));c->gpr[8+word]=spu_il(top(prefix));
        }
        for(unsigned lane=0;lane<4;++lane) {
            c->gpr[3]._u32[lane]=bits_at*8+first_bit+lane*width;
            c->gpr[15]._u32[lane]=first_at+lane*6;c->gpr[16]._u32[lane]=last_at+lane*6;
        }
        c->gpr[0]=spu_link(0x1234);c->gpr[12]=spu_il(frame);c->gpr[13]=spu_il(recipe.fraction);
        c->gpr[14]=spu_il(keys_at);c->gpr[17]=spu_il(count);c->gpr[18]=spu_il(dst);c->gpr[19]=spu_il(ids_at);c->gpr[20]=spu_il(width);
        current_case=saved_case+"/numeric-"+std::to_string(test);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00006950(c.get());drain(c.get(),0x1234);
        alignas(32) uint32_t values[4][8];for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),actual.component[component]);
        for(unsigned track=0;track<count;++track)for(unsigned component=0;component<4;++component) {
            const auto* at=c->ls+dst+(count-1-track)*48+component*4;
            const unsigned expected=uint32_t(at[0])<<24|uint32_t(at[1])<<16|uint32_t(at[2])<<8|at[3];
            if(values[component][track]!=expected){fprintf(stderr,"numeric quaternion mismatch test=%u width=%u frame=%u count=%u track=%u component=%u expected=%08X actual=%08X\n",test,width,frame,count,track,component,expected,values[component][track]);abort();}
        }
        assert(buzz::decode_animation_host_static_quaternions(recipe.first,count,&actual));
        c->gpr[0]=spu_link(0x1238);c->gpr[3]=spu_il(dst);c->gpr[4]=spu_il(first_at);c->gpr[5]=spu_il(count);c->gpr[6]=spu_il(ids_at);c->gpr[7]=spu_il(0xa810);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00007490(c.get());drain(c.get(),0x1238);
        for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),actual.component[component]);
        for(unsigned track=0;track<count;++track)for(unsigned component=0;component<4;++component) {
            const auto* at=c->ls+dst+(count-1-track)*48+component*4;
            const unsigned expected=uint32_t(at[0])<<24|uint32_t(at[1])<<16|uint32_t(at[2])<<8|at[3];
            if(values[component][track]!=expected){fprintf(stderr,"numeric static quaternion mismatch test=%u track=%u component=%u expected=%08X actual=%08X key=%02X%02X%02X%02X%02X%02X\n",test,track,component,expected,values[component][track],c->ls[first_at+track*6],c->ls[first_at+track*6+1],c->ls[first_at+track*6+2],c->ls[first_at+track*6+3],c->ls[first_at+track*6+4],c->ls[first_at+track*6+5]);abort();}
        }
        numeric_static+=count;
        ++numeric_cases;numeric_samples+=count;
    }
    const uint8_t bit=0x80,key[]={0x40,0,0x80,1,0,3};
    buzz::AnimationQuaternionTracks good{{&bit,1},{key,6},{key,6},{key,6},0,1,1,0,0};
    buzz::AnimationHostQuaternion8 result;
    for(unsigned rejection=0;rejection<6;++rejection) {
        auto bad=good;
        switch(rejection){case 0:bad.first_bit=8;break;case 1:bad.frame=2;break;case 2:bad.key_bits.bytes=0;break;case 3:bad.first.bytes=5;break;case 4:bad.last.bytes=5;break;case 5:bad.interior.bytes=5;break;}
        assert(!buzz::sample_animation_host_quaternions(bad,&result));
    }
    assert(buzz::sample_animation_host_quaternions({},nullptr));
    assert(!buzz::decode_animation_host_static_quaternions({key,5},1,&result));
    assert(buzz::decode_animation_host_static_quaternions({},0,nullptr));
    current_case=saved_case;g_spu_trampoline_fn=saved;probe_enabled=true;
}
static void check_numeric_vectors(const spu_context* original)
{
    const auto saved=g_spu_trampoline_fn;const auto saved_case=current_case;probe_enabled=false;
    auto c=std::make_unique<spu_context>();
    constexpr unsigned bits_at=0x1c000,ids_at=0x1f000,first_at=0x20000,last_at=0x21000,keys_at=0x10000,dst=0x24000;
    constexpr unsigned widths[]={0,1,2,7,15,31,32,33,63,64,65,79,95,96,97,127,128};
    constexpr uint32_t fractions[]={0,0x80000000,0x3e800000,0x3f000000,0x3f7fbe77,1};
    constexpr uint32_t special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,0x80800000,
        0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    uint32_t random=0xd9e34915;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    auto put=[&](unsigned at,uint32_t word){c->ls[at]=word>>24;c->ls[at+1]=word>>16;c->ls[at+2]=word>>8;c->ls[at+3]=word;};
    for(unsigned test=0;test<2048;++test) {
        memcpy(c.get(),original,sizeof(*original));c->ls=c->ls_store;
        memset(c->ls+bits_at,0,0x9000);memset(c->ls+keys_at,0,0x4000);
        const unsigned width=widths[test%std::size(widths)],count=1+test%8,first_bit=test%8;
        const unsigned frame=(test/17)%(width+1),key_bytes=(first_bit+count*width+7)/8;
        auto key=[&](unsigned at,unsigned salt){for(unsigned component=0;component<3;++component)put(at+component*4,
            test%4==0?special[(salt+component)%std::size(special)]:test%4==1?next():(next()&0x807fffffu)|((120+next()%16)<<23));};
        unsigned keys=0;
        for(unsigned track=0;track<count;++track) {
            key(first_at+track*12,test+track);key(last_at+track*12,test+track+3);
            c->ls[ids_at+track*2+1]=uint8_t(count-1-track);
            for(unsigned i=0;i<width;++i)if((test%7==0)||(test%7!=1&&next()%5==0)) {
                const unsigned bit=first_bit+track*width+i;c->ls[bits_at+bit/8]|=0x80u>>(bit%8);
                key(keys_at+keys++*12,test+track+i);
            }
        }
        for(unsigned i=count;i<count+8;++i)c->ls[ids_at+i*2+1]=uint8_t(i);
        buzz::AnimationVectorTracks recipe{
            guarded_view(c->ls+bits_at,key_bytes,0),guarded_view(c->ls+first_at,count*12,1),
            guarded_view(c->ls+last_at,count*12,2),guarded_view(c->ls+keys_at,keys*12,3),
            first_bit,width,count,frame,fractions[test%std::size(fractions)]};
        struct alignas(32) GuardedOutput {uint32_t before[8];buzz::AnimationHostVector8 value;uint32_t after[8];} result;
        memset(&result,0xa7,sizeof(result));
        assert(buzz::sample_animation_host_vectors(recipe,&result.value));
        for(unsigned i=0;i<8;++i)assert(result.before[i]==0xa7a7a7a7&&result.after[i]==0xa7a7a7a7);
        auto top=[&](unsigned n){return n==32?~0u:n?~0u<<(32-n):0u;};
        for(unsigned word=0;word<4;++word) {
            const unsigned valid=width>word*32?std::min(32u,width-word*32):0;
            const unsigned prefix=frame>word*32?std::min(32u,frame-word*32):0;
            c->gpr[4+word]=spu_il(top(valid));c->gpr[8+word]=spu_il(top(prefix));
        }
        for(unsigned lane=0;lane<4;++lane) {
            c->gpr[3]._u32[lane]=bits_at*8+first_bit+lane*width;
            c->gpr[15]._u32[lane]=first_at+lane*12;c->gpr[16]._u32[lane]=last_at+lane*12;
        }
        c->gpr[0]=spu_link(0x123c);c->gpr[12]=spu_il(frame);c->gpr[13]=spu_il(recipe.fraction);
        c->gpr[14]=spu_il(keys_at);c->gpr[17]=spu_il(count);c->gpr[18]=spu_il(dst);c->gpr[19]=spu_il(ids_at);c->gpr[20]=spu_il(width);
        current_case=saved_case+"/numeric-float3-"+std::to_string(test);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00009A18(c.get());drain(c.get(),0x123c);
        alignas(32) uint32_t values[4][8];for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(values[component]),result.value.component[component]);
        for(unsigned track=0;track<count;++track)for(unsigned component=0;component<4;++component) {
            const auto* at=c->ls+dst+(count-1-track)*48+component*4;
            const unsigned expected=uint32_t(at[0])<<24|uint32_t(at[1])<<16|uint32_t(at[2])<<8|at[3];
            if(values[component][track]!=expected){fprintf(stderr,"numeric FLOAT3 mismatch test=%u width=%u frame=%u count=%u track=%u component=%u expected=%08X actual=%08X\n",test,width,frame,count,track,component,expected,values[component][track]);abort();}
        }
        numeric_vectors+=count;
    }
    const uint8_t bit=0x80,key[12]{};
    buzz::AnimationVectorTracks good{{&bit,1},{key,12},{key,12},{key,12},0,1,1,0,0};
    buzz::AnimationHostVector8 result;
    for(unsigned rejection=0;rejection<8;++rejection) {
        auto bad=good;
        switch(rejection){case 0:bad.first_bit=8;break;case 1:bad.frame=2;break;case 2:bad.key_bits.bytes=0;break;case 3:bad.first.bytes=11;break;case 4:bad.last.bytes=11;break;case 5:bad.interior.bytes=11;break;case 6:bad.interior_frames=0xffffffffu;break;case 7:bad.interior_frames=129;break;}
        assert(!buzz::sample_animation_host_vectors(bad,&result));
    }
    assert(buzz::sample_animation_host_vectors({},nullptr));
    current_case=saved_case;g_spu_trampoline_fn=saved;probe_enabled=true;
}
static void check_numeric_matrices(const spu_context* original)
{
    const auto saved=g_spu_trampoline_fn;const auto saved_case=current_case;probe_enabled=false;
    auto c=std::make_unique<spu_context>();
    constexpr unsigned source=0x10000,destination=0x20000;
    constexpr uint32_t special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,
        0x00800000,0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,
        0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    uint32_t random=0x25d72a38;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    for(unsigned test=0;test<2048;++test) {
        memcpy(c.get(),original,sizeof(*original));c->ls=c->ls_store;
        const unsigned count=1+test%8;memset(c->ls+source,0,512);memset(c->ls+destination,0,512);
        for(unsigned joint=0;joint<count;++joint)for(unsigned component=0;component<12;++component) {
            const uint32_t word=test<512?special[(test+joint*3+component)%std::size(special)]:
                test%2?next():(next()&0x807fffffu)|((120+next()%16)<<23);
            auto* p=c->ls+source+joint*48+component*4;p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;
        }
        const auto input=guarded_view(c->ls+source,count*48,0);
        struct alignas(32) GuardedMatrix {uint32_t before[8];buzz::AnimationHostWorldMatrix8 value;uint32_t after[8];} result;
        memset(&result,0x57,sizeof(result));
        result.value=buzz::make_animation_host_world_matrices8(buzz::decode_animation_host_pose8(input.data,count));
        for(unsigned i=0;i<8;++i)assert(result.before[i]==0x57575757&&result.after[i]==0x57575757);
        assert(!memcmp(input.data,c->ls+source,count*48));
        std::array<uint8_t,32+512+32> publication;publication.fill(0x39);
        buzz::emit_animation_host_world_matrices8(result.value,publication.data()+32,count);
        for(unsigned i=0;i<32;++i)assert(publication[i]==0x39);
        for(unsigned i=32+count*64;i<publication.size();++i)assert(publication[i]==0x39);
        c->gpr[0]=spu_link(0x1240);c->gpr[3]=spu_il(destination);c->gpr[4]=spu_il(source);c->gpr[5]=spu_il(count);
        current_case=saved_case+"/numeric-pose-matrix-"+std::to_string(test);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00004D60(c.get());drain(c.get(),0x1240);
        alignas(32) uint32_t values[16][8];
        for(unsigned coefficient=0;coefficient<16;++coefficient)_mm256_store_si256(reinterpret_cast<v8*>(values[coefficient]),result.value.coefficient[coefficient]);
        for(unsigned joint=0;joint<count;++joint)for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column) {
            const auto* p=c->ls+destination+joint*64+column*16+row*4;
            const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
            if(expected!=values[row*4+column][joint]){fprintf(stderr,"numeric pose matrix mismatch test=%u joint=%u row=%u column=%u expected=%08X actual=%08X\n",test,joint,row,column,expected,values[row*4+column][joint]);abort();}
        }
        for(unsigned joint=0;joint<count;++joint)for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column) {
            const auto* expected=c->ls+destination+joint*64+column*16+row*4;
            assert(!memcmp(publication.data()+32+joint*64+row*16+column*4,expected,4));
        }
        numeric_matrices+=count;
    }
    // No active joints: masked gathers read no byte at the guard boundary.
    const auto empty=guarded_view(nullptr,0,0);
    (void)buzz::decode_animation_host_pose8(empty.data,0);
    current_case=saved_case;g_spu_trampoline_fn=saved;probe_enabled=true;
}
static void check_numeric_blends(const spu_context* original)
{
    const auto saved=g_spu_trampoline_fn;const auto saved_case=current_case;probe_enabled=false;
    auto c=std::make_unique<spu_context>();
    constexpr unsigned a=0x10000,b=0x12000,destination=0x24000,weights=0x26000;
    constexpr uint32_t fractions[]={0,0x80000000,1,0x3b6842e0,0x3e800000,0x3f000000,0x3f7fffff,0x3f800000};
    constexpr uint32_t special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,
        0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    uint32_t random=0x981c40a6;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    auto put=[&](unsigned at,uint32_t v){auto* p=c->ls+at;p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;};
    for(unsigned test=0;test<2048;++test) {
        memcpy(c.get(),original,sizeof(*original));c->ls=c->ls_store;
        const unsigned count=1+test%8,factor=fractions[(test/8)%std::size(fractions)];
        memset(c->ls+a,0,512);memset(c->ls+b,0,512);memset(c->ls+destination,0,512);memset(c->ls+weights,0,32);
        uint64_t packed_a[8],packed_b[8];
        auto pack=[&](){return uint64_t(8000+next()%16000)<<32|uint64_t(8000+next()%16000)<<17|uint64_t(8000+next()%16000)<<2|next()%4;};
        for(unsigned lane=0;lane<8;++lane){packed_a[lane]=pack();packed_b[lane]=test%3?pack():packed_a[lane];}
        const auto qa=buzz::decode_animation_host_quaternions8(packed_a),qb=buzz::decode_animation_host_quaternions8(packed_b);
        alignas(32) uint32_t qa_words[4][8],qb_words[4][8];
        for(unsigned component=0;component<4;++component){_mm256_store_si256(reinterpret_cast<v8*>(qa_words[component]),qa.component[component]);_mm256_store_si256(reinterpret_cast<v8*>(qb_words[component]),qb.component[component]);}
        for(unsigned joint=0;joint<count;++joint)for(unsigned component=0;component<12;++component) {
            put(a+joint*48+component*4,component<4?qa_words[component][joint]:test<512?special[(test+joint*3+component)%std::size(special)]:next());
            put(b+joint*48+component*4,component<4?qb_words[component][joint]^(test%5?0:0x80000000u):test<512?special[(test*3+joint+component)%std::size(special)]:next());
        }
        const auto first=guarded_view(c->ls+a,count*48,0),last=guarded_view(c->ls+b,count*48,1);
        struct alignas(32) GuardedPose {uint32_t before[8];buzz::AnimationHostPose8 value;uint32_t after[8];} actual;
        memset(&actual,0xc4,sizeof(actual));
        actual.value=buzz::blend_animation_host_poses8(buzz::decode_animation_host_pose8(first.data,count),buzz::decode_animation_host_pose8(last.data,count),v8_set(factor));
        for(unsigned i=0;i<8;++i)assert(actual.before[i]==0xc4c4c4c4&&actual.after[i]==0xc4c4c4c4);
        assert(!memcmp(first.data,c->ls+a,count*48)&&!memcmp(last.data,c->ls+b,count*48));
        c->gpr[0]=spu_link(0x1244);c->gpr[3]=spu_il(destination);c->gpr[4]=spu_il(weights);
        c->gpr[5]=spu_il(a);c->gpr[6]=spu_il(0);c->gpr[7]=spu_il(b);c->gpr[8]=spu_il(0);c->gpr[9]=spu_il(factor);c->gpr[10]=spu_il(count);
        current_case=saved_case+"/numeric-blend-"+std::to_string(test);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00003188(c.get());
        assert(c->pc==0x3460);
        const auto expected_weight=spu_fm(spu_il(factor),spu_il(0x3f7fffff));
        for(unsigned lane=0;lane<4;++lane)assert(c->gpr[35]._u32[lane]==expected_weight._u32[lane]);
        drain(c.get(),0x1244);
        alignas(32) uint32_t words[12][8];
        for(unsigned component=0;component<4;++component) {
            _mm256_store_si256(reinterpret_cast<v8*>(words[component]),actual.value.rotation.component[component]);
            _mm256_store_si256(reinterpret_cast<v8*>(words[4+component]),actual.value.translation.component[component]);
            _mm256_store_si256(reinterpret_cast<v8*>(words[8+component]),actual.value.scale.component[component]);
        }
        for(unsigned joint=0;joint<count;++joint)for(unsigned component=0;component<12;++component) {
            const auto* p=c->ls+destination+joint*48+component*4;
            const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
            if(words[component][joint]!=expected){fprintf(stderr,"numeric pose blend mismatch test=%u factor=%08X joint=%u component=%u expected=%08X actual=%08X\n",test,factor,joint,component,expected,words[component][joint]);abort();}
        }
        for(unsigned joint=0;joint<count;++joint)assert(c->ls[weights+joint]==255);
        numeric_blends+=count;
    }
    current_case=saved_case;g_spu_trampoline_fn=saved;probe_enabled=true;
}
static void check_numeric_hierarchy(const spu_context* original)
{
    const auto saved=g_spu_trampoline_fn;const auto saved_case=current_case;probe_enabled=false;
    auto c=std::make_unique<spu_context>();
    constexpr unsigned source=0x10000,destination=0x14000,root=0x18000,schedule=0x19000;
    constexpr uint32_t special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,
        0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    uint32_t random=0x5317cd82;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    auto put=[&](unsigned at,uint32_t word){auto* p=c->ls+at;p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;};
    auto local=std::make_unique<buzz::AnimationHostPoseArrays>();
    struct alignas(32) GuardedWorld {uint32_t before[8];buzz::AnimationHostPoseArrays value;uint32_t after[8];};
    auto actual=std::make_unique<GuardedWorld>();
    for(unsigned test=0;test<2048;++test) {
        memcpy(c.get(),original,sizeof(*original));c->ls=c->ls_store;
        // At least two schedule groups select the real 52A0 producer. The
        // <=4-record alternative is uncovered and deliberately unsupported.
        const unsigned count=1+test%8,entries=8;
        memset(c->ls+source,0,512);memset(c->ls+destination,0,512);
        uint64_t packed[8];for(auto& key:packed)key=uint64_t(8000+next()%16000)<<32|uint64_t(8000+next()%16000)<<17|uint64_t(8000+next()%16000)<<2|next()%4;
        const auto q=buzz::decode_animation_host_quaternions8(packed);
        alignas(32) uint32_t words[4][8];for(unsigned component=0;component<4;++component)_mm256_store_si256(reinterpret_cast<v8*>(words[component]),q.component[component]);
        for(unsigned joint=0;joint<count;++joint)for(unsigned component=0;component<12;++component)put(source+joint*48+component*4,
            component<4?words[component][joint]:test<512?special[(test+joint+component)%std::size(special)]:next());
        for(unsigned component=0;component<12;++component)put(root+component*4,component<4?words[component][7]:
            test<512?special[(test*3+component)%std::size(special)]:next());
        for(unsigned i=0;i<entries;++i){const unsigned child=std::min(i,count-1);put(schedule+i*4,child<<16|((test+child)%2?0xffff:0x7fff));}
        buzz::AnimationHostHierarchyPlan plan;
        const auto input=guarded_view(c->ls+source,count*48,0),parent=guarded_view(c->ls+root,48,1),table=guarded_view(c->ls+schedule,entries*4,2);
        assert(buzz::plan_animation_host_hierarchy(table,entries,count,plan));
        uint16_t indices[8];for(unsigned i=0;i<count;++i)indices[i]=i;
        buzz::scatter_animation_host_poses8(buzz::decode_animation_host_pose8(input.data,count),*local,indices,count);
        memset(actual.get(),0xb6,sizeof(*actual));
        buzz::produce_animation_host_hierarchy(plan,*local,buzz::decode_animation_host_pose8(parent.data,1),actual->value);
        for(unsigned i=0;i<8;++i)assert(actual->before[i]==0xb6b6b6b6&&actual->after[i]==0xb6b6b6b6);
        for(unsigned component=0;component<12;++component)for(unsigned joint=count;joint<256;++joint)assert(actual->value.component[component][joint]==0xb6b6b6b6);
        assert(!memcmp(input.data,c->ls+source,count*48)&&!memcmp(parent.data,c->ls+root,48)&&!memcmp(table.data,c->ls+schedule,entries*4));
        c->gpr[0]=spu_link(0x1248);c->gpr[3]=spu_il(destination);c->gpr[4]=spu_il(source);
        c->gpr[5]=spu_il(root);c->gpr[6]=spu_il(schedule);c->gpr[7]=spu_il(entries);
        current_case=saved_case+"/numeric-hierarchy-"+std::to_string(test);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00004FB8(c.get());drain(c.get(),0x1248);
        for(unsigned joint=0;joint<count;++joint)for(unsigned component=0;component<12;++component) {
            const auto* p=c->ls+destination+joint*48+component*4;const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
            if(expected!=actual->value.component[component][joint]){fprintf(stderr,"numeric hierarchy mismatch test=%u joint=%u component=%u expected=%08X actual=%08X\n",test,joint,component,expected,actual->value.component[component][joint]);abort();}
        }
        numeric_hierarchy+=count;
    }
    // Admission rejects malformed or cyclic descriptors before pose mutation.
    buzz::AnimationHostHierarchyPlan plan;
    for(unsigned rejection=0;rejection<8;++rejection) {
        for(unsigned i=0;i<8;++i)put(schedule+i*4,i?0x00010000:0x0000ffff);
        unsigned entries=8,joints=2,bytes=32;
        assert(buzz::plan_animation_host_hierarchy({c->ls+schedule,bytes},entries,joints,plan));
        switch(rejection){case 0:bytes=31;break;case 1:entries=1025;break;case 2:joints=257;break;
            case 3:put(schedule+4,0x00000000);break;case 4:put(schedule,0x00000001);break;
            case 5:put(schedule+4,0x00010002);break;case 6:for(unsigned i=0;i<8;++i)put(schedule+i*4,0x0000ffff);break;case 7:put(schedule+4,0x00020000);break;}
        assert(!buzz::plan_animation_host_hierarchy({c->ls+schedule,bytes},entries,joints,plan));
    }
    assert(!buzz::plan_animation_host_hierarchy({c->ls+schedule,16},4,1,plan));
    assert(!buzz::plan_animation_host_hierarchy({c->ls+schedule,36},9,2,plan));
    assert(buzz::plan_animation_host_hierarchy({},0,0,plan)&&plan.batches==0);
    current_case=saved_case;g_spu_trampoline_fn=saved;probe_enabled=true;
}
static void check_invalid_job_inputs(const spu_context* c)
{
    const auto valid=pending_job_input;
    std::vector<uint8_t> damaged(SPU_LS_SIZE);
    auto put32=[&](unsigned at,uint32_t word){auto* p=damaged.data()+at;p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;};
    auto put16=[&](unsigned at,unsigned value){damaged[at]=value>>8;damaged[at+1]=value;};
    for(unsigned test=0;test<28;++test) {
        memcpy(damaged.data(),c->ls,SPU_LS_SIZE);
        const unsigned context=valid.context,descriptor=valid.descriptor,skeleton=valid.cache[1].lsa,
            controls=valid.input[0].lsa,clips=valid.input[1].lsa;
        switch(test) {
        case 0:put32(context+20,0);break;case 1:put16(context+24,3);break;
        case 2:put16(descriptor+10,24);break;case 3:put32(descriptor+36,8);break;
        case 4:put32(descriptor+248,1);break;case 5:put32(descriptor+48,1);break;
        case 6:put32(descriptor+56,16);break;case 7:put32(descriptor+72,16);break;
        case 8:put32(skeleton,0);break;case 9:put16(skeleton+12,0);break;
        case 10:put16(skeleton+14,65535);break;case 11:put16(skeleton+16,0);break;
        case 12:put16(skeleton+16,257);break;case 13:put32(context+28,valid.output[1].lsa+1);break;
        case 14:put32(descriptor+100,0);break;case 15:put32(descriptor+164,valid.output[0].ea+1);break;
        case 16:put16(controls,2);break;case 17:put16(controls+2,0x8003);break;
        case 18:put16(controls+2,0x4000);break;case 19:put16(controls+4,0x8000);break;
        case 20:put32(controls+8,0x3f800001);break;case 21:put32(clips,0);break;
        case 22:put16(clips+4,1);break;case 23:put16(clips+6,1);break;
        case 24:put32(descriptor+52,valid.input[0].ea+1);break;case 25:put32(context+36,32);break;
        case 26:put32(context+4,SPU_LS_SIZE-16);break;case 27:put32(context,SPU_LS_SIZE-16);break;
        }
        buzz::AnimationJobInput untouched=valid;const char* reason=nullptr;
        if(buzz::read_animation_job_input(damaged.data(),SPU_LS_SIZE,valid.context,valid.descriptor,untouched,&reason)) {
            fprintf(stderr,"invalid native frontend case accepted: %u\n",test);abort();
        }
        assert(reason&&*reason&&!memcmp(&untouched,&valid,sizeof(valid)));
    }
    buzz::AnimationJobInput result=valid;const char* reason=nullptr;
    assert(!buzz::read_animation_job_input(c->ls,SPU_LS_SIZE-1,valid.context,valid.descriptor,result,&reason));
    assert(!buzz::read_animation_job_input(c->ls,SPU_LS_SIZE,SPU_LS_SIZE-16,valid.descriptor,result,&reason));
    assert(!buzz::read_animation_job_input(c->ls,SPU_LS_SIZE,valid.context,SPU_LS_SIZE-16,result,&reason));
    assert(!memcmp(&result,&valid,sizeof(valid)));
}
static void check_numeric_extra(const spu_context* original)
{
    const auto saved=g_spu_trampoline_fn;const auto saved_case=current_case;probe_enabled=false;
    auto c=std::make_unique<spu_context>();
    constexpr unsigned a=0x10000,b=0x12000,destination=0x24000,weights=0x26000,input_weights=0x27000,curve_flags=0x28000;
    constexpr uint32_t fractions[]={0,0x80000000,1,0x3b6842e0,0x3e800000,0x3f000000,0x3f7fffff,0x3f800000};
    constexpr uint32_t special[]={0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,0x00800000,
        0x80800000,0x3f800000,0xbf800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fffffff,0xffffffff};
    uint32_t random=0x981c40a6;
    auto next=[&](){random^=random<<13;random^=random>>17;random^=random<<5;return random;};
    auto put=[&](unsigned at,uint32_t v){auto* p=c->ls+at;p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;};
    for(unsigned test=0;test<2048;++test) {
        memcpy(c.get(),original,sizeof(*original));c->ls=c->ls_store;
        const unsigned count=1+test%8,factor=fractions[(test/8)%std::size(fractions)];
        memset(c->ls+a,0,512);memset(c->ls+b,0,512);memset(c->ls+destination,0,512);memset(c->ls+weights,0,32);
        uint64_t packed_a[8],packed_b[8];
        auto pack=[&](){return uint64_t(8000+next()%16000)<<32|uint64_t(8000+next()%16000)<<17|uint64_t(8000+next()%16000)<<2|next()%4;};
        for(unsigned lane=0;lane<8;++lane){packed_a[lane]=pack();packed_b[lane]=test%3?pack():packed_a[lane];}
        const auto qa=buzz::decode_animation_host_quaternions8(packed_a),qb=buzz::decode_animation_host_quaternions8(packed_b);
        alignas(32) uint32_t qa_words[4][8],qb_words[4][8];
        for(unsigned component=0;component<4;++component){_mm256_store_si256(reinterpret_cast<v8*>(qa_words[component]),qa.component[component]);_mm256_store_si256(reinterpret_cast<v8*>(qb_words[component]),qb.component[component]);}
        for(unsigned joint=0;joint<count;++joint)for(unsigned component=0;component<12;++component) {
            put(a+joint*48+component*4,component<4?qa_words[component][joint]:test<512?special[(test+joint*3+component)%std::size(special)]:next());
            put(b+joint*48+component*4,component<4?qb_words[component][joint]^(test%5?0:0x80000000u):test<512?special[(test*3+joint+component)%std::size(special)]:next());
        }
        const auto first=guarded_view(c->ls+a,count*48,0),last=guarded_view(c->ls+b,count*48,1);
        struct alignas(32) GuardedPose {uint32_t before[8];buzz::AnimationHostPose8 value;uint32_t after[8];} actual;
        memset(&actual,0xc4,sizeof(actual));
        actual.value=buzz::add_animation_host_poses8(buzz::decode_animation_host_pose8(first.data,count),buzz::decode_animation_host_pose8(last.data,count));
        for(unsigned i=0;i<8;++i)assert(actual.before[i]==0xc4c4c4c4&&actual.after[i]==0xc4c4c4c4);
        assert(!memcmp(first.data,c->ls+a,count*48)&&!memcmp(last.data,c->ls+b,count*48));
        c->gpr[0]=spu_link(0x1244);c->gpr[3]=spu_il(destination);c->gpr[4]=spu_il(weights);
        memset(c->ls+input_weights,255,16);
        c->gpr[5]=spu_il(a);c->gpr[6]=spu_il(input_weights);c->gpr[7]=spu_il(b);c->gpr[8]=spu_il(0);c->gpr[9]=spu_il(0x3f800000);c->gpr[10]=spu_il(0);c->gpr[11]=spu_il(8);
        current_case=saved_case+"/numeric-extra-"+std::to_string(test);
        // Eight original logical slots select the covered pipelined producer;
        // only count native input/output lanes are compared at guarded tails.
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00003888(c.get());drain(c.get(),0x1244);
        alignas(32) uint32_t words[12][8];
        for(unsigned component=0;component<4;++component) {
            _mm256_store_si256(reinterpret_cast<v8*>(words[component]),actual.value.rotation.component[component]);
            _mm256_store_si256(reinterpret_cast<v8*>(words[4+component]),actual.value.translation.component[component]);
            _mm256_store_si256(reinterpret_cast<v8*>(words[8+component]),actual.value.scale.component[component]);
        }
        for(unsigned joint=0;joint<count;++joint)for(unsigned component=0;component<12;++component) {
            const auto* p=c->ls+destination+joint*48+component*4;
            const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
            if(words[component][joint]!=expected){fprintf(stderr,"numeric additive pose mismatch test=%u factor=%08X joint=%u component=%u expected=%08X actual=%08X\n",test,factor,joint,component,expected,words[component][joint]);abort();}
        }
        for(unsigned joint=0;joint<count;++joint)assert(c->ls[weights+joint]==255);
        numeric_extra[0]+=count;
        for(unsigned i=0;i<8;++i) {
            put(a+i*4,test<512?special[(test+i)%std::size(special)]:next());
            put(b+i*4,test<512?special[(test*3+i)%std::size(special)]:next());
            c->ls[curve_flags+i]=(test+i)%2;
        }
        const unsigned published=(count+3)&~3u;
        const auto x=guarded_view(c->ls+a,published*4,0),y=guarded_view(c->ls+b,published*4,1),flags=guarded_view(c->ls+curve_flags,published,2);
        const v8 xv=buzz::decode_geometry_host_floats8(x.data,4,1,published).component[0],yv=buzz::decode_geometry_host_floats8(y.data,4,1,published).component[0];
        alignas(32) uint32_t mask[8]{};for(unsigned i=0;i<published;++i)mask[i]=flags.data[i]?~0u:0;
        const v8 clamp=_mm256_load_si256(reinterpret_cast<const v8*>(mask));
        for(unsigned kind=1;kind<3;++kind) {
            const v8 actual_curve=kind==1?buzz::blend_animation_host_curves8(xv,yv,v8_set(factor),clamp):buzz::add_animation_host_curves8(xv,yv,clamp);
            c->gpr[0]=spu_link(0x124c);c->gpr[3]=spu_il(destination);c->gpr[4]=spu_il(weights);
            c->gpr[5]=spu_il(a);c->gpr[6]=spu_il(kind==1?0:input_weights);c->gpr[7]=spu_il(b);c->gpr[8]=spu_il(0);
            c->gpr[9]=spu_il(curve_flags);c->gpr[10]=spu_il(kind==1?factor:0x3f800000);c->gpr[11]=spu_il(kind==1?count:0);c->gpr[12]=spu_il(count);
            g_spu_trampoline_fn=nullptr;
            if(kind==1)buzz_job_abe1_spu_func_00004838(c.get());else buzz_job_abe1_spu_func_00004AD0(c.get());
            drain(c.get(),0x124c);
            alignas(32) uint32_t values[8];_mm256_store_si256(reinterpret_cast<v8*>(values),actual_curve);
            for(unsigned i=0;i<published;++i){const auto* p=c->ls+destination+i*4;const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];if(values[i]!=expected){fprintf(stderr,"numeric curve mismatch kind=%u test=%u lane=%u expected=%08X actual=%08X\n",kind,test,i,expected,values[i]);abort();}assert(c->ls[weights+i]==255);}
            numeric_extra[kind]+=count;
        }
        assert(!memcmp(x.data,c->ls+a,published*4)&&!memcmp(y.data,c->ls+b,published*4)&&!memcmp(flags.data,c->ls+curve_flags,published));
    }
    current_case=saved_case;g_spu_trampoline_fn=saved;probe_enabled=true;
}
int main(int argc,char** argv)
{
    assert(argc==4);output_directory=argv[2];assert(std::filesystem::is_directory(output_directory));
    assert(std::string(argv[3])=="Compare"||std::string(argv[3])=="Replace"||std::string(argv[3])=="Corrupt"||std::string(argv[3])=="CorruptVector"||std::string(argv[3])=="CorruptMatrix"||std::string(argv[3])=="CorruptAffine"||std::string(argv[3])=="CorruptBlend"||std::string(argv[3])=="CorruptHierarchy"||std::string(argv[3])=="CorruptExtraPose"||std::string(argv[3])=="CorruptExtraCurve");
    corrupt_extra_pose=std::string(argv[3])=="CorruptExtraPose";corrupt_extra_curve=std::string(argv[3])=="CorruptExtraCurve";
    corrupt_hierarchy=std::string(argv[3])=="CorruptHierarchy";
    corrupt_blend=std::string(argv[3])=="CorruptBlend";
    corrupt_affines=std::string(argv[3])=="CorruptAffine";
    corrupt_matrices=std::string(argv[3])=="CorruptMatrix";
    corrupt_vectors=std::string(argv[3])=="CorruptVector";
    corrupt_quaternions=std::string(argv[3])=="Corrupt";replace_quaternions=std::string(argv[3])!="Compare";
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_TOWARD_ZERO|0x8040);
    auto c=std::make_unique<spu_context>();std::vector<std::filesystem::path> files;
    for(auto& file:std::filesystem::directory_iterator(argv[1]))if(file.path().extension()==".gz")files.push_back(file.path());
    std::sort(files.begin(),files.end());
    std::array<uint8_t,2048+SPU_LS_SIZE> skipped;
    for(auto& path:files) {
        current_case=path.filename().string();auto* file=gzopen(path.string().c_str(),"rb");assert(file);
        auto read=[&](void* data,unsigned n){assert(gzread(file,data,n)==int(n));};
        uint32_t header[6];read(header,sizeof(header));assert(header[0]==0x434a5a42&&header[1]==1&&header[2]==40&&header[5]==SPU_LS_SIZE);
        memset(c.get(),0,sizeof(*c));c->ls=c->ls_store;c->image_id=40;c->host_depth=1;
        read(c->gpr,sizeof(c->gpr));read(c->ls,SPU_LS_SIZE);
        uint32_t n;read(&n,4);transfers.clear();transfer_index=0;
        for(unsigned i=0;i<n;++i){uint32_t h[4];read(h,16);Transfer t{h[0],h[1],h[2],h[3],{}};assert((t.kind==1||t.kind==2)&&uint64_t(t.lsa)+t.size<=SPU_LS_SIZE);t.data.resize(t.size);read(t.data.data(),t.size);transfers.push_back(std::move(t));}
        read(skipped.data(),unsigned(skipped.size()));uint32_t expected[7];read(expected,sizeof(expected));assert(gzread(file,skipped.data(),1)==0);assert(gzclose(file)==Z_OK);
        const char* rejection=nullptr;
        if(!buzz::read_animation_job_input(c->ls,SPU_LS_SIZE,c->gpr[3]._u32[0],c->gpr[4]._u32[0],pending_job_input,&rejection)) {
            fprintf(stderr,"native animation frontend rejected %s: %s\n",current_case.c_str(),rejection);abort();
        }
        if(!frontend_jobs)check_invalid_job_inputs(c.get());
        ++frontend_jobs;frontend_clips+=pending_job_input.clips;
        for(unsigned i=0;i<pending_job_input.nodes;++i)++frontend_nodes[pending_job_input.node[i].operation];
        planned_publication=0;
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00000010(c.get());drain(c.get(),expected[0]);
        assert(planned_publication==pending_job_input.publications);
        assert(transfer_index==transfers.size()&&c->host_depth==1&&c->image_id==40&&c->ls==c->ls_store&&!g_spu_trampoline_fn);
        const uint32_t exit[]={c->pc,c->status,c->stop_code,unsigned(spu_channel_has_data(&c->ch_out_mbox)),c->ch_out_mbox.value,unsigned(spu_channel_has_data(&c->ch_out_intr_mbox)),c->ch_out_intr_mbox.value};
        assert(!memcmp(exit,expected,sizeof(exit)));++jobs;total_transfers+=n;
    }
    assert(jobs==17714&&total_transfers==115162&&dump_finished);
    assert(numeric_vectors==9216&&vector_observation[3].calls==76050&&vector_observation[3].nonempty==21052);
    assert(replace_quaternions?(replaced_vectors==76050):(compared_vectors==24548));
    assert(numeric_matrices==9216&&pose_observation[2].calls==17714&&pose_observation[2].nonempty==17714);
    assert(replace_quaternions?(replaced_matrices==17714):(compared_matrices==461853));
    assert(composed_affines==461853);
    assert(replace_quaternions?(replaced_affines==17714):(compared_affines==461853));
    assert(numeric_blends==9216&&blend_recipes.size()==541);
    assert(replace_quaternions?(replaced_blends==17714):(compared_blends==461853));
    assert(numeric_hierarchy==9216&&hierarchy_plans==17714&&hierarchy_lanes==461853);
    assert(replace_quaternions?(replaced_hierarchy==17714):(compared_hierarchy==461853));
    assert(frontend_jobs==17714&&frontend_nodes[0]==17714&&frontend_nodes[1]==2597&&frontend_clips==38025&&frontend_publications==39112);
    assert(extra_observation[0].calls==2597&&extra_observation[1].calls==3684&&extra_observation[2].calls==2597);
    for(unsigned kind=0;kind<3;++kind){assert(numeric_extra[kind]==9216);if(replace_quaternions)assert(replaced_extra[kind]==extra_observation[kind].calls);else assert(compared_extra[kind]);}
    printf("PASS animation whole-job oracle mode=%s: %llu exact jobs / %llu ordered transfers; quaternion calls=%llu nonempty=%llu\n",argv[3],(unsigned long long)jobs,(unsigned long long)total_transfers,(unsigned long long)quaternion_calls,(unsigned long long)nonempty_calls);
    printf("PASS native host quaternion samples=%llu\n",(unsigned long long)compared_quaternions);
    printf("PASS native quaternion producer replacements=%llu\n",(unsigned long long)replaced_quaternion_calls);
    printf("PASS guarded quaternion numeric batches=%llu samples=%llu\n",(unsigned long long)numeric_cases,(unsigned long long)numeric_samples);
    printf("PASS static quaternion calls=%llu nonempty=%llu compared=%llu replaced=%llu numeric=%llu\n",(unsigned long long)static_calls,(unsigned long long)static_nonempty,(unsigned long long)compared_static,(unsigned long long)replaced_static,(unsigned long long)numeric_static);
    for(unsigned kind=0;kind<4;++kind)printf("vector observer kind=%u calls=%llu nonempty=%llu\n",kind,(unsigned long long)vector_observation[kind].calls,(unsigned long long)vector_observation[kind].nonempty);
    printf("PASS FLOAT3 compared=%llu replacements=%llu numeric=%llu\n",(unsigned long long)compared_vectors,(unsigned long long)replaced_vectors,(unsigned long long)numeric_vectors);
    for(unsigned kind=0;kind<3;++kind)printf("pose observer kind=%u calls=%llu nonempty=%llu\n",kind,(unsigned long long)pose_observation[kind].calls,(unsigned long long)pose_observation[kind].nonempty);
    printf("PASS pose matrices compared=%llu replacements=%llu numeric=%llu\n",(unsigned long long)compared_matrices,(unsigned long long)replaced_matrices,(unsigned long long)numeric_matrices);
    printf("PASS host QTS-to-reference affines composed=%llu compared=%llu replacements=%llu\n",(unsigned long long)composed_affines,(unsigned long long)compared_affines,(unsigned long long)replaced_affines);
    auto* recipes=fopen((output_directory/"blend-recipes.tsv").string().c_str(),"wb");assert(recipes);
    fputs("factor\tweightA\tweightB\tweightOut\tcalls\tsamples\n",recipes);
    for(const auto& [shape,n]:blend_recipes)fprintf(recipes,"%08X\t%u\t%u\t%u\t%llu\t%llu\n",shape[0],shape[1],shape[2],shape[3],(unsigned long long)n.first,(unsigned long long)n.second);
    assert(!fclose(recipes));
    printf("PASS pose blend recipes=%zu compared=%llu replacements=%llu numeric=%llu\n",blend_recipes.size(),(unsigned long long)compared_blends,(unsigned long long)replaced_blends,(unsigned long long)numeric_blends);
    printf("PASS host hierarchy plans=%llu entries=%llu joints=%llu batches=%llu full8=%llu noScaleInheritance=%llu\n",(unsigned long long)hierarchy_plans,(unsigned long long)hierarchy_entries,(unsigned long long)hierarchy_lanes,(unsigned long long)hierarchy_batches,(unsigned long long)hierarchy_full_batches,(unsigned long long)hierarchy_flags);
    printf("PASS hierarchy compared=%llu replacements=%llu numeric=%llu\n",(unsigned long long)compared_hierarchy,(unsigned long long)replaced_hierarchy,(unsigned long long)numeric_hierarchy);
    printf("PASS native frontend jobs=%llu nodes0=%llu nodes1=%llu clips=%llu ordered-publications=%llu\n",(unsigned long long)frontend_jobs,(unsigned long long)frontend_nodes[0],(unsigned long long)frontend_nodes[1],(unsigned long long)frontend_clips,(unsigned long long)frontend_publications);
    for(unsigned kind=0;kind<3;++kind) {
        printf("PASS extra producer kind=%u calls=%llu nonempty=%llu recipes=%zu compared=%llu replacements=%llu numeric=%llu\n",kind,(unsigned long long)extra_observation[kind].calls,(unsigned long long)extra_observation[kind].nonempty,extra_recipes[kind].size(),(unsigned long long)compared_extra[kind],(unsigned long long)replaced_extra[kind],(unsigned long long)numeric_extra[kind]);
        for(const auto& [shape,n]:extra_recipes[kind])printf("extra recipe kind=%u factor=%08X flags=%X weights=%u,%u calls=%llu\n",kind,shape[0],shape[1],shape[2],shape[3],(unsigned long long)n);
    }
    puts("PASS native job frontend: 31 malformed metadata/span cases rejected without changing admission output");
    puts("PASS invalid recipes rejected: quaternion=6 FLOAT3=8; empty tracks touch no data");
    for(auto [count,n]:quaternion_counts)printf("quaternion count=%u calls=%llu\n",count,(unsigned long long)n);
}
