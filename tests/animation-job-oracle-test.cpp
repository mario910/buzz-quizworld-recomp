#define SPU_LS_DEBUG_HOOKS 0
#include "../port/native_animation_host_vectors.h"
#include "../port/native_animation_host_additive.h"
#include "../port/native_animation_host_curve_keys.h"
#include "../port/native_animation_clip_input.h"
#include "../port/native_animation_clip_time.h"
#include "../port/native_animation_host_clip.h"
#include "../port/native_animation_host_pipeline.h"
#include "../port/native_animation_job.h"
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
struct ChannelEvent {uint32_t direction,channel,value;};
static std::vector<ChannelEvent> reference_channels;
static size_t channel_index=0;
static uint64_t compared_channel_events=0,compared_channel_jobs=0;
static std::map<std::pair<unsigned,unsigned>,bool> channel_examples;
static bool oracle_reference_pass=false,corrupt_channel=false;
static size_t transfer_index=0;
static std::string current_case;
static uint64_t jobs=0,total_transfers=0,quaternion_calls=0,nonempty_calls=0;
static buzz::AnimationJobInput pending_job_input;
static unsigned planned_publication=0;
static bool native_whole_job=false,corrupt_native_output=false;
static buzz::AnimationNativeJobPlan native_job_plan;
static buzz::AnimationNativeJobWorkspace native_job_workspace;
static buzz::AnimationHostControlPlan host_control;
static buzz::AnimationHostHierarchyPlan host_hierarchy;
static buzz::AnimationHostPose8 host_root;
static buzz::AnimationHostPipelineWorkspace host_pipeline;
static std::array<uint8_t,256*48> host_reference_output;
static std::array<uint8_t,256*64> host_world_output;
static std::array<uint8_t,256*4> host_curve_output;
static bool host_pipeline_ready=false,publish_host_pipeline=false,corrupt_host_pipeline=false;
static uint64_t host_pipeline_jobs=0,host_pipeline_publications=0,host_pipeline_bytes=0;
static void check_host_control_inputs(const spu_context*);
static void check_host_pipeline_guards(const spu_context*);
static buzz::AnimationHostClipPose pending_full_clip;
static buzz::AnimationHostPoseArrays job_default_pose;
static bool replace_full_clip=false,corrupt_full_clip=false;
static uint64_t replaced_full_clips=0;
static bool animation_native_clip_override(spu_context*);
static void check_default_pose_inputs(const spu_context*);
static void check_full_clip_bounds(buzz::AnimationByteView,buzz::AnimationByteView,buzz::AnimationByteView,const uint32_t*,unsigned);
static buzz::AnimationHostClipWorkspace full_clip_workspace;
static unsigned full_clip_pose_base=0,full_clip_curve_base=0;
static uint64_t full_clip_calls=0,full_clip_checks=0,full_clip_joints=0,full_clip_curves=0;
static uint64_t full_clip_static[4]{};
static bool publication_fault=false;
struct ClipCurvePadding {unsigned lsa=0;uint32_t word[3]{};};
static ClipCurvePadding clip_curve_padding[3];
static unsigned clip_curve_padding_slots=0;
static uint64_t clip_curve_padding_reuse=0;
static buzz::AnimationClipTimeSelection clip_time[3];
static unsigned clip_header_lsa[3]{},planned_clip_headers=0,planned_clip_bodies=0;
static uint64_t clip_time_headers=0,clip_time_bodies=0,clip_time_samples=0;
static std::map<unsigned,uint64_t> clip_time_blocks;
static uint64_t numeric_clip_times=0,numeric_clip_time_batches=0;
static bool corrupt_clip_time=false;
static void check_numeric_clip_times(const spu_context*);
static void check_invalid_clip_times(const buzz::AnimationClipTimeInput&);
static void observe_clip_get(spu_context*,const Transfer&);
static buzz::AnimationClipInput pending_clip_input;
static unsigned pending_clip_vector_kind=1;
static uint64_t clip_inputs=0,clip_recipe_checks=0,clip_tail_inputs=0;
static std::array<uint8_t,SPU_LS_SIZE> entry_ls;
static std::map<unsigned,uint64_t> clip_tail_sizes;
static uint64_t clip_nonzero_tail_inputs=0;
static unsigned clip_guard_cases=0;
static void check_clip_input_bounds(buzz::AnimationByteView,buzz::AnimationByteView,buzz::AnimationByteView,unsigned,unsigned,unsigned,uint32_t,bool);
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
static bool corrupt_quaternions=false,corrupt_vectors=false,corrupt_matrices=false,corrupt_affines=false,corrupt_blend=false,corrupt_hierarchy=false,corrupt_extra_pose=false,corrupt_extra_curve=false,corrupt_scalar_keys=false,corruption_injected=false;
static uint64_t numeric_cases=0,numeric_samples=0;
static std::array<buzz::AnimationHostQuaternion8,32> pending_static_quaternions;
static std::vector<uint16_t> pending_static_indices;
static unsigned pending_static_base=0;
static uint64_t static_calls=0,static_nonempty=0,compared_static=0,replaced_static=0,numeric_static=0;
struct VectorObservation { bool started=false,finished=false;uint64_t calls=0,nonempty=0; };
static VectorObservation vector_observation[4],pose_observation[3],extra_observation[3],scalar_key_observation[2];
static std::map<unsigned,uint64_t> scalar_key_counts[2];
static buzz::AnimationHostCurveKeyPlan pending_curve_keys;
static std::array<v8,32> pending_curve_values;
static unsigned pending_curve_base=0;
static uint64_t compared_curve_keys=0,replaced_curve_keys=0,numeric_curve_keys=0,owned_curve_keys=0;
static bool animation_native_scalar_key_override(spu_context*);
static void check_numeric_curve_keys(const spu_context*);
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
static void record_channel(unsigned direction,unsigned channel,uint32_t value)
{
    const ChannelEvent event{direction,channel,value};
    if(oracle_reference_pass){reference_channels.push_back(event);return;}
    if(channel_index>=reference_channels.size()) {
        fprintf(stderr,"extra channel event %s index=%zu\n",current_case.c_str(),channel_index);abort();
    }
    const auto& expected=reference_channels[channel_index++];
    if(event.direction!=expected.direction||event.channel!=expected.channel||event.value!=expected.value) {
        auto* diagnostic=fopen((output_directory/"channel-mismatch-reference.tsv").string().c_str(),"wb");assert(diagnostic);
        for(unsigned i=0;i<reference_channels.size();++i){const auto& e=reference_channels[i];fprintf(diagnostic,"%u\t%u\t%u\t%08X\n",i,e.direction,e.channel,e.value);}assert(!fclose(diagnostic));
        fprintf(stderr,"channel transcript mismatch %s event=%zu actual=%u,%u,%08X expected=%u,%u,%08X\n",current_case.c_str(),channel_index-1,event.direction,event.channel,event.value,expected.direction,expected.channel,expected.value);abort();
    }
}
extern "C" {
SPU_THREAD_LOCAL void (*g_spu_trampoline_fn)(spu_context*);
void spu_indirect_branch(spu_context*);
void spu_drain_call(spu_context* c,uint32_t stop){drain(c,stop);}
void spu_img_restore(spu_context* c,int32_t image){assert(c->image_id==image);}
void spu_stop(spu_context* c){fprintf(stderr,"unexpected stop %s pc=%X\n",current_case.c_str(),c->pc);abort();}
void spu_halt(spu_context* c){fprintf(stderr,"unexpected halt %s pc=%X\n",current_case.c_str(),c->pc);abort();}
void spu_wrch(spu_context* c,uint32_t channel,u128 value)
{
    auto v=value._u32[0];
    if(!oracle_reference_pass&&corrupt_channel&&channel==MFC_Cmd&&!corruption_injected){v^=1;corruption_injected=true;}
    record_channel(0,channel,v);
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
        if(kind==2&&!oracle_reference_pass) {
            assert(host_pipeline_ready);
            const unsigned index=planned_publication;
            const uint8_t* bytes=index==0?host_reference_output.data():index==1?host_world_output.data():host_curve_output.data();
            if(!publication_fault&&memcmp(bytes,expected.data.data(),expected.size)) {
                fprintf(stderr,"host pipeline output mismatch %s publication=%u\n",current_case.c_str(),index);abort();
            }
            if(publish_host_pipeline)memcpy(c->ls+lsa,bytes,expected.size);
            ++host_pipeline_publications;host_pipeline_bytes+=expected.size;
            assert(planned_publication<pending_job_input.publications);
            const auto& planned=pending_job_input.output[planned_publication++];
            if(lsa!=planned.lsa||c->mfc_eal!=planned.ea||c->mfc_size!=planned.bytes||c->mfc_tag!=pending_job_input.dma_tag) {
                fprintf(stderr,"frontend publication mismatch %s publication=%u actual=%X,%X,%u,tag%u planned=%X,%X,%u,tag%u\n",current_case.c_str(),planned_publication-1,lsa,c->mfc_eal,c->mfc_size,c->mfc_tag,planned.lsa,planned.ea,planned.bytes,pending_job_input.dma_tag);abort();
            }
            ++frontend_publications;
        }
        if(kind==1){memcpy(c->ls+lsa,expected.data.data(),expected.size);if(!oracle_reference_pass)observe_clip_get(c,expected);}
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
    uint32_t value=0;
    switch(channel) {
    case MFC_RdTagStat:value=c->mfc_tag_status&c->mfc_tag_mask;break;
    case SPU_RdMachStat:value=c->int_enable&1;break;
    case SPU_RdInMbox:assert(spu_channel_has_data(&c->ch_in_mbox));value=spu_channel_read(&c->ch_in_mbox);break;
    default:fprintf(stderr,"unexpected read channel=%u %s\n",channel,current_case.c_str());abort();
    }
    record_channel(1,channel,value);return spu_il(value);
}
uint32_t spu_rchcnt(spu_context* c,uint32_t channel){assert(channel==SPU_RdInMbox);const auto value=c->ch_in_mbox.count;record_channel(2,channel,value);return value;}
}
#include "../analysis/native-geometry/phase4/job-oracle-v18/reference.inc"
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
static void observe_clip_get(spu_context* c,const Transfer& transfer)
{
    if(transfer.data.size()>=4&&buzz::animation_job_be32(transfer.data.data())==0x45413033) {
        assert(planned_clip_headers<pending_job_input.clips);
        const unsigned index=planned_clip_headers++;const auto& clip=pending_job_input.clip[index];
        assert(transfer.ea==clip.ea&&transfer.size==clip.header_bytes);
        const buzz::AnimationClipTimeInput input{{c->ls+transfer.lsa,transfer.size},clip.ea,transfer.lsa,clip.time};
        const char* reason=nullptr;
        if(!buzz::select_animation_clip_times8(&input,1,clip_time+index,&reason)) {
            fprintf(stderr,"native clip time rejected %s clip=%u: %s\n",current_case.c_str(),index,reason);abort();
        }
        if(!clip_time_headers)check_invalid_clip_times(input);
        clip_header_lsa[index]=transfer.lsa;++clip_time_headers;++clip_time_blocks[clip_time[index].block];
    } else {
        assert(planned_clip_bodies<planned_clip_headers);
        const auto& selection=clip_time[planned_clip_bodies++];
        if(transfer.ea!=selection.ea||transfer.lsa!=selection.lsa||transfer.size!=selection.bytes) {
            fprintf(stderr,"native clip body plan mismatch %s clip=%u actual=%X,%X,%u planned=%X,%X,%u\n",current_case.c_str(),planned_clip_bodies-1,transfer.ea,transfer.lsa,transfer.size,selection.ea,selection.lsa,selection.bytes);abort();
        }
        ++clip_time_bodies;
    }
}
static void check_clip_recipe(const buzz::AnimationKeyTracks& recipe,const spu_context* c)
{
    assert(recipe.tracks==c->gpr[17]._u32[0]);
    if(!recipe.tracks)return; // Unused source addresses are not observable.
    const unsigned bit=unsigned(recipe.key_bits.data-c->ls)*8+recipe.first_bit;
    if(bit!=c->gpr[3]._u32[0]||recipe.first.data!=c->ls+c->gpr[15]._u32[0]||
       recipe.last.data!=c->ls+c->gpr[16]._u32[0]||recipe.interior.data!=c->ls+c->gpr[14]._u32[0]||
       recipe.interior_frames!=c->gpr[20]._u32[0]||recipe.frame!=c->gpr[12]._u32[0]||(!corrupt_clip_time&&recipe.fraction!=c->gpr[13]._u32[0])) {
        fprintf(stderr,"clip semantic recipe mismatch %s kind=%u bits=%X/%X endpoints=%X,%X/%X,%X interior=%X/%X\n",current_case.c_str(),pending_clip_vector_kind,
            bit,c->gpr[3]._u32[0],unsigned(recipe.first.data-c->ls),unsigned(recipe.last.data-c->ls),c->gpr[15]._u32[0],c->gpr[16]._u32[0],unsigned(recipe.interior.data-c->ls),c->gpr[14]._u32[0]);abort();
    }
    ++clip_recipe_checks;
}
static void animation_original_probe(unsigned pc,spu_context* c)
{
    if(!probe_enabled)return;
    if(pc==0x5c00) {
        const unsigned header=c->gpr[5]._u32[0],block=c->gpr[6]._u32[0];
        buzz::AnimationByteView header_view,block_view;
        for(size_t i=0;i<transfer_index;++i)if(const auto& t=transfers[i];t.kind==1) {
            if(t.lsa==header)header_view={c->ls+header,t.size};
            if(t.lsa==block)block_view={c->ls+block,t.size};
        }
        unsigned slot=planned_clip_headers;
        while(slot&&clip_header_lsa[slot-1]!=header)--slot;
        assert(slot);const auto& selected=clip_time[slot-1];
        if(selected.lsa!=block||selected.frame!=c->gpr[8]._u32[0]||selected.fraction!=c->gpr[9]._u32[0]||selected.interior_frames!=c->gpr[7]._u32[0]) {
            fprintf(stderr,"native clip time sample mismatch %s slot=%u block=%X/%X frame=%u/%u fraction=%08X/%08X width=%u/%u\n",current_case.c_str(),slot-1,selected.lsa,block,selected.frame,c->gpr[8]._u32[0],selected.fraction,c->gpr[9]._u32[0],selected.interior_frames,c->gpr[7]._u32[0]);abort();
        }
        if(!clip_time_samples)check_numeric_clip_times(c);
        ++clip_time_samples;
        uint32_t fraction=selected.fraction;
        if(corrupt_clip_time&&!corruption_injected){fraction^=0x80000000u;corruption_injected=true;}
        const char* reason=nullptr;
        assert(block+block_view.bytes+1548<=SPU_LS_SIZE);
        const buzz::AnimationByteView tail{c->ls+block+block_view.bytes,1548};
        if(!buzz::read_animation_clip_input(header_view,block_view,tail,(pending_job_input.curve_floats+3)&~3u,selected.interior_frames,selected.frame,fraction,pending_clip_input,&reason)) {
            fprintf(stderr,"clip input rejected %s header=%X block=%X: %s\n",current_case.c_str(),header,block,reason);abort();
        }
        if(pending_clip_input.published_tail_bytes) {
            if(memcmp(tail.data,entry_ls.data()+block+block_view.bytes,pending_clip_input.published_tail_bytes)) {
                fprintf(stderr,"published tail depends on mutated scratch %s block=%X tail=%u\n",current_case.c_str(),block,pending_clip_input.published_tail_bytes);abort();
            }
            ++clip_tail_inputs;++clip_tail_sizes[pending_clip_input.published_tail_bytes];
            for(unsigned i=0;i<pending_clip_input.published_tail_bytes;++i)if(tail.data[i]){++clip_nonzero_tail_inputs;break;}
        }
        if(!clip_inputs||(pending_clip_input.published_tail_bytes&&clip_tail_inputs==1))
            check_clip_input_bounds(header_view,block_view,{tail.data,pending_clip_input.published_tail_bytes},(pending_job_input.curve_floats+3)&~3u,c->gpr[7]._u32[0],c->gpr[8]._u32[0],c->gpr[9]._u32[0],!clip_inputs);
        full_clip_pose_base=c->gpr[3]._u32[0];full_clip_curve_base=c->gpr[4]._u32[0];
        assert(pending_clip_input.joints==pending_job_input.joints);
        const unsigned curves=pending_job_input.curve_floats,padded=(curves+3)&~3u,tail_at=full_clip_curve_base+curves*4;
        assert(uint64_t(tail_at)+(padded-curves)*4<=SPU_LS_SIZE);
        unsigned owner=0;while(owner<clip_curve_padding_slots&&clip_curve_padding[owner].lsa!=full_clip_curve_base)++owner;
        if(owner==clip_curve_padding_slots) {
            assert(clip_curve_padding_slots<3);++clip_curve_padding_slots;clip_curve_padding[owner].lsa=full_clip_curve_base;
            for(unsigned i=0;i<padded-curves;++i)clip_curve_padding[owner].word[i]=buzz::animation_job_be32(entry_ls.data()+tail_at+i*4);
        } else ++clip_curve_padding_reuse;
        if(!publication_fault)for(unsigned i=0;i<padded-curves;++i)if(clip_curve_padding[owner].word[i]!=buzz::animation_job_be32(c->ls+tail_at+i*4)) {
            fprintf(stderr,"clip reused padding source mismatch %s base=%X tail=%u expected=%08X native=%08X\n",current_case.c_str(),full_clip_curve_base,i,buzz::animation_job_be32(c->ls+tail_at+i*4),clip_curve_padding[owner].word[i]);abort();
        }
        if(!buzz::produce_animation_host_clip(pending_clip_input,job_default_pose,curves,clip_curve_padding[owner].word,padded-curves,pending_full_clip,full_clip_workspace)) {
            fprintf(stderr,"native complete clip rejected %s\n",current_case.c_str());abort();
        }
        if(!full_clip_calls)check_full_clip_bounds(header_view,block_view,tail,clip_curve_padding[owner].word,padded-curves);
        for(unsigned i=0;i<padded-curves;++i)clip_curve_padding[owner].word[i]=pending_full_clip.curve[curves+i];
        for(unsigned component=0;component<12;++component)memcpy(host_pipeline.leaf[slot-1].pose.component[component],pending_full_clip.pose.component[component],pending_job_input.joints*4);
        memcpy(host_pipeline.leaf[slot-1].curve,pending_full_clip.curve,padded*4);
        ++full_clip_calls;
        for(unsigned kind=0;kind<4;++kind)full_clip_static[kind]+=pending_clip_input.static_count[kind];
        pending_clip_vector_kind=1;++clip_inputs;
        return;
    }
    if(pc==0x2764) {
        if(publication_fault)return;
        for(unsigned joint=0;joint<pending_job_input.joints;++joint)for(unsigned component=0;component<12;++component) {
            const unsigned at=full_clip_pose_base+joint*48+component*4;assert(at+4<=SPU_LS_SIZE);
            const uint32_t expected=buzz::animation_job_be32(c->ls+at),actual=pending_full_clip.pose.component[component][joint];
            if(expected!=actual){fprintf(stderr,"native complete clip mismatch %s joint=%u component=%u expected=%08X actual=%08X\n",current_case.c_str(),joint,component,expected,actual);abort();}
        }
        const unsigned padded=(pending_job_input.curve_floats+3)&~3u;
        for(unsigned i=0;i<padded;++i) {
            const uint32_t expected=buzz::animation_job_be32(c->ls+full_clip_curve_base+i*4),actual=pending_full_clip.curve[i];
            if(expected!=actual){fprintf(stderr,"native complete clip curve mismatch %s channel=%u expected=%08X actual=%08X\n",current_case.c_str(),i,expected,actual);abort();}
        }
        ++full_clip_checks;full_clip_joints+=pending_job_input.joints;full_clip_curves+=padded;
        return;
    }
    if(pc==0x9f50) {
        const unsigned count=c->gpr[17]._u32[0],ids=c->gpr[19]._u32[0];
        auto recipe=pending_clip_input.animated[3];check_clip_recipe(recipe,c);
        pending_curve_base=c->gpr[18]._u32[0];
        if(count) {
        assert(ids<=SPU_LS_SIZE);
        const unsigned capacity=(pending_job_input.curve_floats+3)&~3u;
        if(!buzz::plan_animation_host_curve_keys(pending_clip_input.animated_ids[3],count,capacity,pending_curve_keys)){fprintf(stderr,"curve key mapping rejected %s count=%u capacity=%u\n",current_case.c_str(),count,capacity);abort();}
        assert(pending_clip_input.animated_ids[3].data==c->ls+ids);
        recipe.tracks=pending_curve_keys.tracks;
        if(!buzz::sample_animation_host_curve_keys(recipe,pending_curve_keys,pending_curve_values.data())){fprintf(stderr,"native curve key recipe rejected %s\n",current_case.c_str());abort();}
        for(unsigned i=0;i<pending_curve_keys.tracks;++i)owned_curve_keys+=pending_curve_keys.publish[i];
        } else pending_curve_keys={};
        if(count&&!scalar_key_observation[0].started)check_numeric_curve_keys(c);
    }
    if(pc==0x19f50&&!replace_quaternions) {
        for(unsigned first=0;first<pending_curve_keys.tracks;first+=8) {
            alignas(32) uint32_t values[8];_mm256_store_si256(reinterpret_cast<v8*>(values),pending_curve_values[first/8]);
            for(unsigned lane=0;lane<8&&first+lane<pending_curve_keys.tracks;++lane)if(pending_curve_keys.publish[first+lane]) {
                const unsigned at=pending_curve_base+pending_curve_keys.destination[first+lane]*4;assert(at+4<=SPU_LS_SIZE);const auto* p=c->ls+at;
                const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
                if(expected!=values[lane]){fprintf(stderr,"native scalar-key mismatch %s track=%u destination=%u expected=%08X actual=%08X\n",current_case.c_str(),first+lane,pending_curve_keys.destination[first+lane],expected,values[lane]);abort();}
                ++compared_curve_keys;
            }
        }
    }
    if(pc==0x9f50||pc==0x19f50||pc==0x8d20||pc==0x95c0) {
        const unsigned kind=(pc==0x9f50||pc==0x19f50)?0:1;
        const char* names[]={"raw-scalar-keys","compressed-scalar-keys"};auto& observation=scalar_key_observation[kind];
        if(pc==0x9f50||pc==0x8d20) {
            const unsigned count=c->gpr[17]._u32[0];++observation.calls;observation.nonempty+=count!=0;++scalar_key_counts[kind][count];
            if(count&&!observation.started) {
                observation.started=true;printf("first %s %s count=%u\n",names[kind],current_case.c_str(),count);
                for(unsigned r=3;r<=21;++r)printf("%s arg%u %08X %08X %08X %08X\n",names[kind],r,c->gpr[r]._u32[0],c->gpr[r]._u32[1],c->gpr[r]._u32[2],c->gpr[r]._u32[3]);
                dump((std::string(names[kind])+"-entry-arguments.bin").c_str(),c->gpr+3,19*sizeof(u128));dump((std::string(names[kind])+"-entry-ls.bin").c_str(),c->ls,SPU_LS_SIZE);
            }
        } else if(observation.started&&!observation.finished) {observation.finished=true;dump((std::string(names[kind])+"-exit-ls.bin").c_str(),c->ls,SPU_LS_SIZE);}
        return;
    }
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
        assert(!host_pipeline_ready);
        buzz::produce_animation_host_outputs(host_control,host_hierarchy,host_root,c->ls+pending_job_input.input[2].lsa,
            host_pipeline,host_reference_output.data(),host_world_output.data(),host_curve_output.data());
        if(!host_pipeline_jobs)check_host_pipeline_guards(c);
        if(corrupt_host_pipeline&&!corruption_injected){host_reference_output[0]^=0x80;corruption_injected=true;}
        host_pipeline_ready=true;++host_pipeline_jobs;
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
        assert(pending_clip_vector_kind==1||pending_clip_vector_kind==2);
        const unsigned kind=pending_clip_vector_kind++;
        const auto& recipe=pending_clip_input.animated[kind];check_clip_recipe(recipe,c);
        if(count)assert(pending_clip_input.animated_ids[kind].data==c->ls+c->gpr[19]._u32[0]);
        assert(buzz::sample_animation_host_vectors(recipe,pending_vectors.data()));
        for(unsigned i=0;i<count;++i){const auto* p=pending_clip_input.animated_ids[kind].data+i*2;pending_vector_indices.push_back(uint16_t(p[0])<<8|p[1]);}
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
        assert(count==pending_clip_input.static_count[0]);
        if(count)assert(pending_clip_input.static_values[0].data==c->ls+c->gpr[4]._u32[0]&&pending_clip_input.static_ids[0].data==c->ls+c->gpr[6]._u32[0]);
        assert(buzz::decode_animation_host_static_quaternions(pending_clip_input.static_values[0],count,pending_static_quaternions.data()));
        for(unsigned i=0;i<count;++i){const auto* p=pending_clip_input.static_ids[0].data+i*2;pending_static_indices.push_back(uint16_t(p[0])<<8|p[1]);}
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
        const auto& recipe=pending_clip_input.animated[0];check_clip_recipe(recipe,c);
        if(count)assert(pending_clip_input.animated_ids[0].data==c->ls+c->gpr[19]._u32[0]);
        if(!buzz::sample_animation_host_quaternions(recipe,pending_quaternions.data())){fprintf(stderr,"native quaternion recipe rejected %s frames=%u frame=%u count=%u\n",current_case.c_str(),recipe.interior_frames,recipe.frame,count);abort();}
        for(unsigned i=0;i<count;++i){const auto* p=pending_clip_input.animated_ids[0].data+i*2;pending_indices.push_back(uint16_t(p[0])<<8|p[1]);}
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
static bool animation_native_scalar_key_override(spu_context* c)
{
    if(!replace_quaternions||!probe_enabled)return false;
    animation_original_probe(0x9f50,c);
    for(unsigned first=0;first<pending_curve_keys.tracks;first+=8) {
        alignas(32) uint32_t values[8];_mm256_store_si256(reinterpret_cast<v8*>(values),pending_curve_values[first/8]);
        for(unsigned lane=0;lane<8&&first+lane<pending_curve_keys.tracks;++lane)if(pending_curve_keys.publish[first+lane]) {
            const unsigned at=pending_curve_base+pending_curve_keys.destination[first+lane]*4;assert(at+4<=SPU_LS_SIZE);auto* p=c->ls+at;
            const unsigned word=values[lane];p[0]=word>>24;p[1]=word>>16;p[2]=word>>8;p[3]=word;
        }
    }
    ++replaced_curve_keys;animation_original_probe(0x19f50,c);
    if(corrupt_scalar_keys&&!corruption_injected&&pending_curve_keys.tracks) {
        for(unsigned i=0;i<pending_curve_keys.tracks;++i)if(pending_curve_keys.publish[i]){c->ls[pending_curve_base+pending_curve_keys.destination[i]*4]^=0x80;corruption_injected=true;break;}
    }
    c->pc=c->gpr[0]._u32[0];return true;
}
static bool animation_native_clip_override(spu_context* c)
{
    if(!replace_full_clip||!probe_enabled)return false;
    animation_original_probe(0x5c00,c);
    if(corrupt_full_clip&&!corruption_injected){pending_full_clip.pose.component[4][0]^=0x40000000;corruption_injected=true;}
    auto put=[&](unsigned at,uint32_t n){assert(at+4<=SPU_LS_SIZE);for(unsigned byte=0;byte<4;++byte)c->ls[at+byte]=n>>(24-byte*8);};
    for(unsigned joint=0;joint<pending_job_input.joints;++joint)for(unsigned component=0;component<12;++component)
        put(full_clip_pose_base+joint*48+component*4,pending_full_clip.pose.component[component][joint]);
    for(unsigned i=0;i<((pending_job_input.curve_floats+3)&~3u);++i)put(full_clip_curve_base+i*4,pending_full_clip.curve[i]);
    ++replaced_full_clips;c->pc=c->gpr[0]._u32[0];return true;
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
static void check_host_control_inputs(const spu_context* c)
{
    const auto& sk=pending_job_input.cache[1];
    const auto guarded=guarded_view(c->ls+sk.lsa,sk.bytes,0);
    buzz::AnimationHostControlPlan plan;assert(buzz::plan_animation_host_control(pending_job_input,guarded,plan));
    for(unsigned test=0;test<18;++test) {
        auto job=pending_job_input;std::vector<uint8_t> bytes(c->ls+sk.lsa,c->ls+sk.lsa+sk.bytes);
        buzz::AnimationByteView skeleton{bytes.data(),bytes.size()};
        auto put=[&](unsigned at,uint32_t n){for(unsigned i=0;i<4;++i)bytes[at+i]=n>>(24-i*8);};
        const unsigned flags=40+buzz::animation_job_be32(bytes.data()+40),count=(job.curve_floats+3)&~3u;
        switch(test) {
        case 0:job.joints=0;break;
        case 1:job.joints=257;break;
        case 2:job.curve_floats=257;break;
        case 3:job.nodes=0;break;
        case 4:job.nodes=3;break;
        case 5:job.clips=1;break;
        case 6:job.node[0].operation=2;break;
        case 7:job.node[0].factor=0x80000000;break;
        case 8:job.node[0].operation=1;job.node[0].factor=0x3f000000;break;
        case 9:job.node[0].left=0;break;
        case 10:job.node[0].left=uint16_t(0x8000|job.clips);break;
        case 11:job.node[0].left=0x4000;break;
        case 12:job.node[0].left=uint16_t(0x4000|job.nodes);break;
        case 13:skeleton.bytes=47;break;
        case 14:put(40,0);break;
        case 15:put(40,0xfffffff0);break;
        case 16:bytes[flags+count-1]=2;break;
        case 17:skeleton.bytes=flags+count-1;break;
        }
        memset(&plan,0x5a,sizeof(plan));const auto unchanged=plan;
        assert(!buzz::plan_animation_host_control(job,skeleton,plan));assert(!memcmp(&plan,&unchanged,sizeof(plan)));
    }
    puts("PASS host control admission: 18 invalid graphs/strengths/clamp spans rejected unchanged; read-only skeleton input pass");
}
static void check_host_pipeline_guards(const spu_context* c)
{
    auto workspace=std::make_unique<buzz::AnimationHostPipelineWorkspace>();memset(workspace.get(),0xa7,sizeof(*workspace));
    const unsigned joints=pending_job_input.joints,curves=(pending_job_input.curve_floats+3)&~3u;
    for(unsigned leaf=0;leaf<pending_job_input.clips;++leaf) {
        for(unsigned component=0;component<12;++component)memcpy(workspace->leaf[leaf].pose.component[component],host_pipeline.leaf[leaf].pose.component[component],joints*4);
        memcpy(workspace->leaf[leaf].curve,host_pipeline.leaf[leaf].curve,curves*4);
    }
    std::array<uint8_t,256*48> reference;reference.fill(0xa7);
    std::array<uint8_t,256*64> world;world.fill(0xa7);
    std::array<uint8_t,256*4> curve;curve.fill(0xa7);
    const auto input=guarded_view(c->ls+pending_job_input.input[2].lsa,joints*48,0);
    buzz::produce_animation_host_outputs(host_control,host_hierarchy,host_root,input.data,*workspace,reference.data(),world.data(),curve.data());
    assert(!memcmp(reference.data(),host_reference_output.data(),joints*48)&&!memcmp(world.data(),host_world_output.data(),joints*64)&&!memcmp(curve.data(),host_curve_output.data(),curves*4));
    for(unsigned i=joints*48;i<reference.size();++i)assert(reference[i]==0xa7);
    for(unsigned i=joints*64;i<world.size();++i)assert(world[i]==0xa7);
    for(unsigned i=curves*4;i<curve.size();++i)assert(curve[i]==0xa7);
    for(unsigned component=0;component<12;++component)for(unsigned i=joints;i<256;++i) {
        assert(workspace->world.component[component][i]==0xa7a7a7a7);
        for(unsigned node=0;node<host_control.nodes;++node)assert(workspace->node[node].pose.component[component][i]==0xa7a7a7a7);
    }
    for(unsigned node=0;node<host_control.nodes;++node)for(unsigned i=curves;i<256;++i)assert(workspace->node[node].curve[i]==0xa7a7a7a7);
    puts("PASS host pipeline guarded reference input, control/world unused slots and final output canaries");
}
static void check_default_pose_inputs(const spu_context* c)
{
    const auto& skeleton=pending_job_input.cache[1];
    for(unsigned test=0;test<7;++test) {
        std::vector<uint8_t> bytes(c->ls+skeleton.lsa,c->ls+skeleton.lsa+skeleton.bytes);
        unsigned joints=pending_job_input.joints;size_t n=bytes.size();
        auto put=[&](unsigned at,uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=value>>(24-i*8);};
        switch(test){case 0:n=47;break;case 1:bytes[0]^=1;break;case 2:put(8,0);break;case 3:joints=0;break;case 4:++joints;break;case 5:put(20,0);break;case 6:put(20,0xfffffff0);break;}
        buzz::AnimationByteView view{reinterpret_cast<const uint8_t*>(1),7};
        assert(!buzz::read_animation_pose_defaults({bytes.data(),n},joints,view));assert(view.data==reinterpret_cast<const uint8_t*>(1)&&view.bytes==7);
    }
    std::array<uint8_t,8*48> raw;
    for(unsigned i=0;i<raw.size();++i)raw[i]=uint8_t(i*71+19);
    buzz::AnimationHostPoseArrays pose;
    for(unsigned n=1;n<=8;++n) {
        memset(&pose,0xa7,sizeof(pose));const auto view=guarded_view(raw.data(),n*48,0);
        assert(buzz::decode_animation_pose_defaults(view,n,pose));
        for(unsigned component=0;component<12;++component)for(unsigned i=0;i<256;++i)
            assert(pose.component[component][i]==(i<n?buzz::animation_job_be32(raw.data()+i*48+component*4):0xa7a7a7a7));
        assert(!buzz::decode_animation_pose_defaults({view.data,view.bytes-1},n,pose));
    }
    assert(!buzz::decode_animation_pose_defaults({},1,pose));assert(!buzz::decode_animation_pose_defaults({},257,pose));
    assert(buzz::decode_animation_pose_defaults({},0,pose));
    puts("PASS ES01 defaults: 7 invalid headers rejected unchanged; guarded tails1..8/unused host slots/short and empty streams pass");
}
static void check_full_clip_bounds(buzz::AnimationByteView header,buzz::AnimationByteView block,
    buzz::AnimationByteView tail,const uint32_t* padding,unsigned words)
{
    const auto guarded_header=guarded_view(header.data,unsigned(header.bytes),0);
    const auto storage=guarded_view(block.data,unsigned(block.bytes+tail.bytes),1);
    buzz::AnimationClipInput clip;
    assert(buzz::read_animation_clip_input(guarded_header,{storage.data,block.bytes},{storage.data+block.bytes,tail.bytes},
        (pending_job_input.curve_floats+3)&~3u,pending_clip_input.animated[0].interior_frames,pending_clip_input.animated[0].frame,pending_clip_input.animated[0].fraction,clip));
    if(clip.static_count[1])clip.static_values[1]=guarded_view(clip.static_values[1].data,unsigned(clip.static_values[1].bytes),2);
    if(clip.static_count[3])clip.static_values[3]=guarded_view(clip.static_values[3].data,unsigned(clip.static_values[3].bytes),3);
    auto result=std::make_unique<buzz::AnimationHostClipPose>();memset(result.get(),0xa7,sizeof(*result));
    buzz::AnimationHostClipWorkspace workspace;
    assert(buzz::produce_animation_host_clip(clip,job_default_pose,pending_job_input.curve_floats,padding,words,*result,workspace));
    for(unsigned component=0;component<12;++component)for(unsigned i=0;i<256;++i)
        assert(result->pose.component[component][i]==(i<clip.joints?pending_full_clip.pose.component[component][i]:0xa7a7a7a7));
    const unsigned capacity=(pending_job_input.curve_floats+3)&~3u;
    for(unsigned i=0;i<256;++i)assert(result->curve[i]==(i<capacity?pending_full_clip.curve[i]:0xa7a7a7a7));
    assert(!buzz::produce_animation_host_clip(clip,job_default_pose,257,padding,words,*result,workspace));
    assert(words&& !buzz::produce_animation_host_clip(clip,job_default_pose,pending_job_input.curve_floats,padding,words-1,*result,workspace));
    puts("PASS complete clip guarded header/block/static T/static curve inputs and host canaries; invalid curve capacity/padding rejected");
}
static void check_invalid_clip_times(const buzz::AnimationClipTimeInput& original)
{
    constexpr unsigned cases=36;
    for(unsigned test=0;test<cases;++test) {
        std::vector<uint8_t> header(original.header.data,original.header.data+original.header.bytes);
        auto input=original;input.header={header.data(),header.size()};
        auto put16=[&](unsigned at,unsigned n){header[at]=n>>8;header[at+1]=n;};
        auto put32=[&](unsigned at,uint32_t n){for(unsigned i=0;i<4;++i)header[at+i]=n>>(24-i*8);};
        const unsigned data=52+buzz::animation_job_be32(header.data()+52),table=56+buzz::animation_job_be32(header.data()+56);
        const unsigned count=buzz::animation_job_be16(header.data()+18),size=buzz::animation_job_be32(header.data()+data);
        const unsigned last=table+(count-1)*4;
        unsigned lanes=1;bool null_input=false,null_output=false;
        switch(test) {
        case 0:lanes=9;break;
        case 1:input.header.data=nullptr;break;
        case 2:input.header.bytes=63;break;
        case 3:header[0]^=1;break;
        case 4:put16(12,unsigned(header.size())+16);break;
        case 5:input.ea=0;break;
        case 6:input.ea|=1;break;
        case 7:input.lsa|=1;break;
        case 8:input.lsa=0x40000;break;
        case 9:put16(18,1);break;
        case 10:put16(16,1);break;
        case 11:put32(52,0);break;
        case 12:put32(56,0);break;
        case 13:put32(52,0xfffffff0);break;
        case 14:put32(56,0xfffffff0);break;
        case 15:put32(8,0);break;
        case 16:put32(8,0xbf800000);break;
        case 17:put32(8,0x7f800000);break;
        case 18:put32(8,1);break;
        case 19:put16(table,1);break;
        case 20:put16(table+4,1);break;
        case 21:put16(table+2,129);break;
        case 22:put16(last+2,1);break;
        case 23:put16(16,buzz::animation_job_be16(header.data()+16)+1);break;
        case 24:put32(data,0);break;
        case 25:put32(data,16400);break;
        case 26:put32(data,size|0x8000);break;
        case 27:put32(data,size-1);break;
        case 28:put32(data+4,buzz::animation_job_be32(header.data()+data+4)|1);break;
        case 29:put32(data+4,0);break;
        case 30:input.ea=0xfffffff0;break;
        case 31:input.lsa=0x40000-unsigned(header.size());break;
        case 32:input.time=0x7f7fffff;break;
        case 33:null_input=true;break;
        case 34:null_output=true;break;
        case 35:input.header.bytes=64;put16(12,64);break;
        }
        buzz::AnimationClipTimeSelection output[8];memset(output,0x5a,sizeof(output));
        std::array<uint8_t,sizeof(output)> unchanged;memcpy(unchanged.data(),output,sizeof(output));
        const char* reason=nullptr;
        if(buzz::select_animation_clip_times8(null_input?nullptr:&input,lanes,null_output?nullptr:output,&reason)) {
            fprintf(stderr,"invalid clip timing accepted case=%u\n",test);abort();
        }
        assert(reason&&!memcmp(output,unchanged.data(),sizeof(output)));
    }
    assert(buzz::select_animation_clip_times8(nullptr,0,nullptr));
    puts("PASS native clip time: 36 invalid clocks/tables/GET spans rejected without changing output; empty lanes touch no data");
}
static void check_numeric_clip_times(const spu_context* original)
{
    const auto saved=g_spu_trampoline_fn;probe_enabled=false;
    auto c=std::make_unique<spu_context>();
    constexpr unsigned header_at=0x10000,ref_at=0x1f000,clip_at=0x1f100,stack=0x30000;
    constexpr unsigned counts[]={2,3,4,8};
    constexpr uint32_t rates[]={0x3f000000,0x3f800000,0x41c00000,0x41efffff,0x42700000};
    auto put16=[](uint8_t* p,unsigned n){p[0]=n>>8;p[1]=n;};
    auto put32=[](uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=n>>(24-i*8);};
    for(unsigned test=0;test<4096;++test) {
        std::array<uint8_t,256> header{};
        const unsigned count=counts[test%4],data=64,table=data+count*8,bytes=(table+count*4+15)&~15u;
        const uint32_t rate=rates[test%std::size(rates)];
        put32(header.data(),0x45413033);put32(header.data()+8,rate);put16(header.data()+12,bytes);
        put16(header.data()+18,count);put32(header.data()+52,data-52);put32(header.data()+56,table-56);
        unsigned starts[8]{},widths[8]{},samples=0;
        for(unsigned i=0;i<count;++i) {
            const unsigned width=i+1==count?0:(test/4+i*13)%129;
            starts[i]=samples;widths[i]=width;
            put16(header.data()+table+i*4,samples);put16(header.data()+table+i*4+2,width);
            put32(header.data()+data+i*8,i+1==count?0:256+i*16);put32(header.data()+data+i*8+4,bytes+i*1024);
            samples+=width+1;
        }
        put16(header.data()+16,samples);
        const auto view=guarded_view(header.data(),bytes,0);
        buzz::AnimationClipTimeInput input[8];
        const unsigned lanes=1+test%8;
        for(unsigned lane=0;lane<lanes;++lane) {
            const unsigned boundary=starts[(test/8+lane)%(count-1)];
            float frequency;memcpy(&frequency,&rate,4);
            const float clock=float(test%3==0?samples+1:boundary)/frequency;
            uint32_t time;memcpy(&time,&clock,4);
            if(test%3) {if(lane%3==1&&time)--time;else if(lane%3==2)++time;}
            if(test%19==0)time=lane%4==0?0x80000000:lane%4==1?1:lane%4==2?0x80000001:0xbf800000;
            input[lane]={view,0x70000000u+lane*0x10000,header_at,time};
        }
        struct GuardedOutput {uint32_t before[8];buzz::AnimationClipTimeSelection value[8];uint32_t after[8];} result;
        memset(&result,0xa7,sizeof(result));
        assert(buzz::select_animation_clip_times8(input,lanes,result.value));
        for(unsigned i=0;i<8;++i)assert(result.before[i]==0xa7a7a7a7&&result.after[i]==0xa7a7a7a7);
        for(unsigned lane=lanes;lane<8;++lane)for(unsigned byte=0;byte<sizeof(result.value[lane]);++byte)
            assert(reinterpret_cast<uint8_t*>(result.value+lane)[byte]==0xa7);
        for(unsigned lane=0;lane<lanes;++lane) {
            memcpy(c.get(),original,sizeof(*original));c->ls=c->ls_store;
            memcpy(c->ls+header_at,header.data(),bytes);put32(c->ls+ref_at+4,clip_at);put32(c->ls+clip_at+8,input[lane].time);
            c->gpr[80]=spu_il(header_at);c->gpr[85]=spu_il(ref_at);
            g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00001F58(c.get());assert(c->pc==0x2020);
            unsigned steps=0;
            while(c->gpr[5]._u32[0]>c->gpr[10]._u32[0]){assert(++steps<=16);buzz_job_abe1_spu_func_00001FE0(c.get());}
            const unsigned block=c->gpr[14]._u32[0];assert(block<count-1);
            c->gpr[1]=spu_il(stack);c->gpr[80]=spu_il(header_at);c->gpr[86]=spu_il(ref_at);c->gpr[83]=spu_il(0);
            put16(c->ls+stack+48,starts[block]);put16(c->ls+stack+50,widths[block]);
            g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_000026A8(c.get());
            if(c->pc==0x2b0c)buzz_job_abe1_spu_func_00002B0C(c.get());
            assert(c->pc==0x2734);
            const auto& actual=result.value[lane];
            if(actual.block!=block||actual.frame!=c->gpr[8]._u32[0]||actual.fraction!=c->gpr[9]._u32[0]) {
                fprintf(stderr,"numeric clip time mismatch test=%u lane=%u time=%08X block=%u/%u frame=%u/%u fraction=%08X/%08X\n",test,lane,input[lane].time,actual.block,block,actual.frame,c->gpr[8]._u32[0],actual.fraction,c->gpr[9]._u32[0]);abort();
            }
            ++numeric_clip_times;
        }
        ++numeric_clip_time_batches;
    }
    g_spu_trampoline_fn=saved;probe_enabled=true;
}
static void check_clip_input_bounds(buzz::AnimationByteView header,buzz::AnimationByteView block,
    buzz::AnimationByteView tail,unsigned capacity,unsigned width,unsigned frame,uint32_t fraction,bool malformed)
{
    const auto guarded_header=guarded_view(header.data,unsigned(header.bytes),0);
    const auto storage=guarded_view(block.data,unsigned(block.bytes+tail.bytes),1);
    buzz::AnimationClipInput plan;
    assert(buzz::read_animation_clip_input(guarded_header,{storage.data,block.bytes},
        {storage.data+block.bytes,tail.bytes},capacity,width,frame,fraction,plan));
    ++clip_guard_cases;
    if(!malformed)return;
    constexpr unsigned invalid_cases=30;
    for(unsigned test=0;test<invalid_cases;++test) {
        std::vector<uint8_t> h(header.data,header.data+header.bytes),b(block.data,block.data+block.bytes+tail.bytes);
        buzz::AnimationByteView hv{h.data(),h.size()},bv{b.data(),block.bytes},tv{b.data()+block.bytes,tail.bytes};
        unsigned cap=capacity,w=width,f=frame;
        auto set16=[](std::vector<uint8_t>& bytes,size_t at,unsigned n){bytes[at]=n>>8;bytes[at+1]=n;};
        auto set32=[](std::vector<uint8_t>& bytes,size_t at,uint32_t n){for(unsigned i=0;i<4;++i)bytes[at+i]=n>>(24-i*8);};
        const unsigned last_header=unsigned(pending_clip_input.animated[0].last.data-block.data)-16;
        switch(test) {
        case 0:h[0]^=1;break;
        case 1:set16(h,12,unsigned(h.size())+16);break;
        case 2:hv.bytes=95;break;
        case 3:bv.bytes=15;tv={};break;
        case 4:w=129;break;
        case 5:f=width+1;break;
        case 6:set16(h,14,0);break;
        case 7:set16(h,14,257);break;
        case 8:set16(h,16,0);break;
        case 9:set16(h,18,0);break;
        case 10:set16(h,38,1);break;
        case 11:set16(h,22,257);break;
        case 12:set32(h,60,0);break;
        case 13:set32(h,60,0xfffffff0);break;
        case 14:set32(h,60,unsigned(h.size())-60);break;
        case 15:set16(h,30,257);break;
        case 16:set16(b,0,65535);break;
        case 17:set16(b,8,65535);break;
        case 18:set16(b,last_header,65535);break;
        case 19:cap=257;break;
        case 20:set16(h,96,pending_clip_input.joints);break;
        case 21:set16(h,unsigned(pending_clip_input.animated_ids[0].data-header.data),pending_clip_input.joints);break;
        case 22:tv.bytes=1549;break;
        case 23:tv={b.data(),1};break;
        case 24:hv.bytes=96;set16(h,12,96);break;
        case 25:set16(b,0,pending_clip_input.animated_count[0]*6-1);break;
        case 26:set16(b,last_header,pending_clip_input.animated_count[0]*6-1);break;
        case 27:bv.bytes=block.bytes-1;tv={};break;
        case 28:hv.data=nullptr;break;
        case 29:bv.data=nullptr;break;
        }
        buzz::AnimationClipInput rejected;memset(&rejected,0x5a,sizeof(rejected));
        const auto unchanged=rejected;const char* reason=nullptr;
        if(buzz::read_animation_clip_input(hv,bv,tv,cap,w,f,fraction,rejected,&reason)) {
            fprintf(stderr,"invalid clip input accepted case=%u\n",test);abort();
        }
        assert(reason&&!memcmp(&rejected,&unchanged,sizeof(rejected)));
    }
    puts("PASS native clip input: 30 malformed spans/counts/IDs rejected without changing admission output");
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
static void check_numeric_curve_keys(const spu_context* original)
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
        const unsigned width=widths[test%std::size(widths)],count=1+test%8,stored=(count+3)&~3u,first_bit=test%8;
        const unsigned frame=(test/17)%(width+1),key_bytes=(first_bit+stored*width+7)/8;
        auto key=[&](unsigned at,unsigned salt){for(unsigned component=0;component<1;++component)put(at+component*4,
            test%4==0?special[(salt+component)%std::size(special)]:test%4==1?next():(next()&0x807fffffu)|((120+next()%16)<<23));};
        unsigned keys=0;
        for(unsigned track=0;track<stored;++track) {
            key(first_at+track*4,test+track);key(last_at+track*4,test+track+3);
            c->ls[ids_at+track*2+1]=uint8_t(track<count?count-1-track:count);
            for(unsigned i=0;i<width;++i)if((test%7==0)||(test%7!=1&&next()%5==0)) {
                const unsigned bit=first_bit+track*width+i;c->ls[bits_at+bit/8]|=0x80u>>(bit%8);
                key(keys_at+keys++*4,test+track+i);
            }
        }
        buzz::AnimationHostCurveKeyPlan plan;
        assert(buzz::plan_animation_host_curve_keys(guarded_view(c->ls+ids_at,stored*2,3),count,stored,plan));
        buzz::AnimationKeyTracks recipe{
            guarded_view(c->ls+bits_at,key_bytes,0),guarded_view(c->ls+first_at,stored*4,1),
            guarded_view(c->ls+last_at,stored*4,2),guarded_view(c->ls+keys_at,keys*4,3),
            first_bit,width,stored,frame,fractions[test%std::size(fractions)]};
        struct alignas(32) GuardedOutput {uint32_t before[8];v8 value;uint32_t after[8];} result;
        memset(&result,0xa7,sizeof(result));
        assert(buzz::sample_animation_host_curve_keys(recipe,plan,&result.value));
        for(unsigned i=0;i<8;++i)assert(result.before[i]==0xa7a7a7a7&&result.after[i]==0xa7a7a7a7);
        auto top=[&](unsigned n){return n==32?~0u:n?~0u<<(32-n):0u;};
        for(unsigned word=0;word<4;++word) {
            const unsigned valid=width>word*32?std::min(32u,width-word*32):0;
            const unsigned prefix=frame>word*32?std::min(32u,frame-word*32):0;
            c->gpr[4+word]=spu_il(top(valid));c->gpr[8+word]=spu_il(top(prefix));
        }
        for(unsigned lane=0;lane<4;++lane) {
            c->gpr[3]._u32[lane]=bits_at*8+first_bit+lane*width;
            c->gpr[15]._u32[lane]=first_at+lane*4;c->gpr[16]._u32[lane]=last_at+lane*4;
        }
        c->gpr[0]=spu_link(0x123c);c->gpr[12]=spu_il(frame);c->gpr[13]=spu_il(recipe.fraction);
        c->gpr[14]=spu_il(keys_at);c->gpr[17]=spu_il(count);c->gpr[18]=spu_il(dst);c->gpr[19]=spu_il(ids_at);c->gpr[20]=spu_il(width);
        current_case=saved_case+"/numeric-scalar-keys-"+std::to_string(test);
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00009F50(c.get());drain(c.get(),0x123c);
        alignas(32) uint32_t values[8];_mm256_store_si256(reinterpret_cast<v8*>(values),result.value);
        for(unsigned track=0;track<stored;++track)if(plan.publish[track]) {
            const auto* p=c->ls+dst+plan.destination[track]*4;
            const unsigned expected=uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];
            if(values[track]!=expected){fprintf(stderr,"numeric scalar-key mismatch test=%u width=%u frame=%u count=%u track=%u destination=%u expected=%08X actual=%08X\n",test,width,frame,count,track,plan.destination[track],expected,values[track]);abort();}
            ++numeric_curve_keys;
        }

    }
    const uint8_t bit=0x80,key[16]{},ids[8]={0,0,0,1,0,2,0,3};
    buzz::AnimationHostCurveKeyPlan plan;
    assert(buzz::plan_animation_host_curve_keys({ids,8},4,4,plan));
    buzz::AnimationKeyTracks good{{&bit,1},{key,16},{key,16},{key,16},0,1,4,0,0};
    v8 result;
    for(unsigned rejection=0;rejection<8;++rejection) {
        auto bad=good;
        switch(rejection){case 0:bad.first_bit=8;break;case 1:bad.frame=2;break;case 2:bad.key_bits.bytes=0;break;case 3:bad.first.bytes=15;break;case 4:bad.last.bytes=15;break;case 5:bad.interior.bytes=3;break;case 6:bad.interior_frames=129;break;case 7:bad.tracks=3;break;}
        assert(!buzz::sample_animation_host_curve_keys(bad,plan,&result));
    }
    assert(!buzz::plan_animation_host_curve_keys({ids,7},4,4,plan));
    assert(!buzz::plan_animation_host_curve_keys({ids,8},4,3,plan));
    assert(!buzz::plan_animation_host_curve_keys({ids,8},257,4,plan));
    assert(!buzz::plan_animation_host_curve_keys({ids,8},4,257,plan));
    assert(buzz::plan_animation_host_curve_keys({},0,0,plan));
    assert(buzz::sample_animation_host_curve_keys({},plan,nullptr));
    current_case=saved_case;g_spu_trampoline_fn=saved;probe_enabled=true;
}
template<class Resolve>
static void check_native_job_preflight(const spu_context* c,Resolve&& original,buzz::AnimationNativeJobOutputs output)
{
    constexpr unsigned cases=26;
    for(unsigned test=0;test<cases;++test) {
        auto job=pending_job_input;std::vector<uint8_t> input(c->ls,c->ls+SPU_LS_SIZE);
        auto destination=output;size_t bytes=SPU_LS_SIZE;bool interrupts_enabled=false,null_ls=false,missing_header=false,missing_body=false,bad_header=false,bad_body=false;
        auto put=[&](unsigned at,uint32_t n){for(unsigned i=0;i<4;++i)input[at+i]=n>>(24-i*8);};
        switch(test) {
        case 0:null_ls=true;break;
        case 1:--bytes;break;
        case 2:job.joints=0;break;
        case 3:job.curve_floats=257;break;
        case 4:job.publications=4;break;
        case 5:job.dma_tag=32;break;
        case 6:job.node[0].left=0x8001;break;
        case 7:job.nodes=2;job.clips=3;break;
        case 8:put(0x1c8,6);break;
        case 9:job.cache[0].bytes=0;break;
        case 10:put(job.cache[0].lsa,1);break;
        case 11:input[job.descriptor+30]=input[job.descriptor+31]=0;break;
        case 12:destination.reference=nullptr;break;
        case 13:job.output[0].bytes-=16;break;
        case 14:job.output[0].ea|=1;break;
        case 15:job.output[0].lsa=job.scratch_lsa;break;
        case 16:job.cache[1].lsa=0x40000;break;
        case 17:missing_header=true;break;
        case 18:missing_body=true;break;
        case 19:bad_header=true;break;
        case 20:bad_body=true;break;
        case 21:job.clip[0].time=0x7f7fffff;break;
        case 22:job.root_pose_lsa=0x40000;break;
        case 23:job.input[2].lsa=0x40000;break;
        case 24:interrupts_enabled=true;break;
        case 25: {
            const unsigned stride=((job.joints+4)&~3u)*48+((job.curve_floats+4)&~3u)*4+
                ((((job.joints+3)&~3u)+((job.curve_floats+3)&~3u)+15)&~15u)+16;
            const unsigned allocation=3*(8192+256)+2*stride;
            input[job.descriptor+30]=uint8_t((allocation/16)>>8);input[job.descriptor+31]=uint8_t(allocation/16);break;
        }
        }
        std::vector<uint8_t> modified;
        auto resolve=[&](uint32_t ea,unsigned n)->buzz::AnimationByteView {
            const bool header=ea==job.clip[0].ea;
            if((missing_header&&header)||(missing_body&&!header))return {};
            const auto view=original(ea,n);
            if((bad_header&&header)||(bad_body&&!header)) {
                modified.assign(view.data,view.data+view.bytes);
                if(bad_header)modified[38]=1;else modified[0]=modified[1]=0xff;
                return {modified.data(),modified.size()};
            }
            return view;
        };
        const auto before=input;const auto events=channel_index;
        buzz::AnimationNativeJobPlan rejected;memset(&rejected,0x5a,sizeof(rejected));const auto unchanged=rejected;const char* reason=nullptr;
        if(buzz::prepare_native_animation_job(job,null_ls?nullptr:input.data(),bytes,interrupts_enabled,resolve,rejected,native_job_workspace,destination,&reason)) {
            fprintf(stderr,"invalid whole animation job admitted case=%u\n",test);abort();
        }
        assert(reason&&channel_index==events&&input==before&&!memcmp(&rejected,&unchanged,sizeof(rejected)));
    }
    // Rebuild the admitted private workspace after rejected preparations.
    const char* reason=nullptr;assert(buzz::prepare_native_animation_job(pending_job_input,c->ls,SPU_LS_SIZE,c->int_enable!=0,original,native_job_plan,native_job_workspace,output,&reason));
    puts("PASS native whole-job preflight: 26 unsupported/malformed/missing-input cases rejected before LS/channel mutation; output plan unchanged");
}
struct NativeAnimationIo {
    spu_context* c;uint32_t return_pc;
    uint32_t read(unsigned channel){return spu_rdch(c,channel)._u32[0];}
    void write(unsigned channel,uint32_t value){spu_wrch(c,channel,spu_il(value));}
    void set_interrupts(bool enabled){c->int_enable=enabled;}
    uint8_t* ls(){return c->ls;}
    void finish(){c->pc=return_pc;}
};
int main(int argc,char** argv)
{
    assert(argc==4);output_directory=argv[2];assert(std::filesystem::is_directory(output_directory));
    assert(std::string(argv[3])=="Compare"||std::string(argv[3])=="Replace"||std::string(argv[3])=="Corrupt"||std::string(argv[3])=="CorruptVector"||std::string(argv[3])=="CorruptMatrix"||std::string(argv[3])=="CorruptAffine"||std::string(argv[3])=="CorruptBlend"||std::string(argv[3])=="CorruptHierarchy"||std::string(argv[3])=="CorruptExtraPose"||std::string(argv[3])=="CorruptExtraCurve"||std::string(argv[3])=="CorruptScalarKeys"||std::string(argv[3])=="CorruptClipTime"||std::string(argv[3])=="ClipReplace"||std::string(argv[3])=="CorruptFullClip"||std::string(argv[3])=="PipelinePublish"||std::string(argv[3])=="CorruptPipeline"||std::string(argv[3])=="CorruptChannel"||std::string(argv[3])=="NativeJob"||std::string(argv[3])=="CorruptNativeJobOutput"||std::string(argv[3])=="CorruptNativeJobChannel");
    publication_fault=std::string(argv[3]).rfind("Corrupt",0)==0;
    corrupt_native_output=std::string(argv[3])=="CorruptNativeJobOutput";
    native_whole_job=std::string(argv[3])=="NativeJob"||corrupt_native_output||std::string(argv[3])=="CorruptNativeJobChannel";
    corrupt_full_clip=std::string(argv[3])=="CorruptFullClip";
    corrupt_channel=std::string(argv[3])=="CorruptChannel"||std::string(argv[3])=="CorruptNativeJobChannel";
    corrupt_host_pipeline=std::string(argv[3])=="CorruptPipeline";
    publish_host_pipeline=std::string(argv[3])=="PipelinePublish"||corrupt_host_pipeline;
    replace_full_clip=std::string(argv[3])=="ClipReplace"||corrupt_full_clip||publish_host_pipeline;
    corrupt_scalar_keys=std::string(argv[3])=="CorruptScalarKeys";corrupt_clip_time=std::string(argv[3])=="CorruptClipTime";
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
    auto* channel_report=fopen((output_directory/"channel-checks.tsv").string().c_str(),"wb");assert(channel_report);
    fputs("job\tevents\tfnv64\n",channel_report);
    for(auto& path:files) {
        current_case=path.filename().string();auto* file=gzopen(path.string().c_str(),"rb");assert(file);
        auto read=[&](void* data,unsigned n){assert(gzread(file,data,n)==int(n));};
        uint32_t header[6];read(header,sizeof(header));assert(header[0]==0x434a5a42&&header[1]==1&&header[2]==40&&header[5]==SPU_LS_SIZE);
        memset(c.get(),0,sizeof(*c));c->ls=c->ls_store;c->image_id=40;c->host_depth=1;
        read(c->gpr,sizeof(c->gpr));read(c->ls,SPU_LS_SIZE);memcpy(entry_ls.data(),c->ls,SPU_LS_SIZE);
        uint32_t n;read(&n,4);transfers.clear();transfer_index=0;
        for(unsigned i=0;i<n;++i){uint32_t h[4];read(h,16);Transfer t{h[0],h[1],h[2],h[3],{}};assert((t.kind==1||t.kind==2)&&uint64_t(t.lsa)+t.size<=SPU_LS_SIZE);t.data.resize(t.size);read(t.data.data(),t.size);transfers.push_back(std::move(t));}
        read(skipped.data(),unsigned(skipped.size()));uint32_t expected[7];read(expected,sizeof(expected));assert(gzread(file,skipped.data(),1)==0);assert(gzclose(file)==Z_OK);
        const char* rejection=nullptr;
        if(!buzz::read_animation_job_input(c->ls,SPU_LS_SIZE,c->gpr[3]._u32[0],c->gpr[4]._u32[0],pending_job_input,&rejection)) {
            fprintf(stderr,"native animation frontend rejected %s: %s\n",current_case.c_str(),rejection);abort();
        }
        if(!frontend_jobs){check_invalid_job_inputs(c.get());check_default_pose_inputs(c.get());check_host_control_inputs(c.get());}
        const auto& skeleton=pending_job_input.cache[1];buzz::AnimationByteView defaults;
        assert(buzz::read_animation_pose_defaults({c->ls+skeleton.lsa,skeleton.bytes},pending_job_input.joints,defaults));
        assert(buzz::decode_animation_pose_defaults(defaults,pending_job_input.joints,job_default_pose));
        assert(buzz::plan_animation_host_control(pending_job_input,{c->ls+skeleton.lsa,skeleton.bytes},host_control));
        assert(buzz::plan_animation_host_hierarchy({c->ls+pending_job_input.hierarchy.lsa,pending_job_input.hierarchy.bytes},
            pending_job_input.hierarchy_entries,pending_job_input.joints,host_hierarchy));
        host_root=buzz::decode_animation_host_pose8(c->ls+pending_job_input.root_pose_lsa,1);host_pipeline_ready=false;
        auto reference=std::make_unique<spu_context>();memcpy(reference.get(),c.get(),sizeof(*c));reference->ls=reference->ls_store;
        reference_channels.clear();channel_index=0;oracle_reference_pass=true;probe_enabled=false;transfer_index=0;
        g_spu_trampoline_fn=nullptr;buzz_job_abe1_spu_func_00000010(reference.get());drain(reference.get(),expected[0]);
        assert(transfer_index==transfers.size()&&reference->host_depth==1&&reference->image_id==40&&reference->ls==reference->ls_store&&!g_spu_trampoline_fn);
        const uint32_t reference_exit[]={reference->pc,reference->status,reference->stop_code,unsigned(spu_channel_has_data(&reference->ch_out_mbox)),reference->ch_out_mbox.value,unsigned(spu_channel_has_data(&reference->ch_out_intr_mbox)),reference->ch_out_intr_mbox.value};
        assert(!memcmp(reference_exit,expected,sizeof(reference_exit)));
        oracle_reference_pass=false;probe_enabled=true;transfer_index=0;channel_index=0;
        ++frontend_jobs;frontend_clips+=pending_job_input.clips;
        for(unsigned i=0;i<pending_job_input.nodes;++i)++frontend_nodes[pending_job_input.node[i].operation];
        planned_publication=0;planned_clip_headers=0;planned_clip_bodies=0;clip_curve_padding_slots=0;
        g_spu_trampoline_fn=nullptr;
        if(native_whole_job) {
            auto resolve=[](uint32_t ea,unsigned bytes)->buzz::AnimationByteView {
                for(const auto& t:transfers)if(t.kind==1&&ea>=t.ea&&uint64_t(ea)+bytes<=uint64_t(t.ea)+t.size)
                    return {t.data.data()+(ea-t.ea),bytes};
                return {};
            };
            const buzz::AnimationNativeJobOutputs output{host_reference_output.data(),host_world_output.data(),host_curve_output.data()};
            const char* reason=nullptr;
            if(!buzz::prepare_native_animation_job(pending_job_input,c->ls,SPU_LS_SIZE,c->int_enable!=0,resolve,native_job_plan,native_job_workspace,output,&reason)) {
                fprintf(stderr,"native whole job rejected %s: %s\n",current_case.c_str(),reason);abort();
            }
            if(!jobs)check_native_job_preflight(c.get(),resolve,output);
            assert(!memcmp(c->ls,entry_ls.data(),SPU_LS_SIZE));
            if(corrupt_native_output&&!corruption_injected){host_reference_output[0]^=0x80;corruption_injected=true;}
            host_pipeline_ready=true;++host_pipeline_jobs;
            NativeAnimationIo io{c.get(),c->gpr[0]._u32[0]};buzz::execute_native_animation_job(native_job_plan,output,io);
        } else {buzz_job_abe1_spu_func_00000010(c.get());drain(c.get(),expected[0]);}
        assert(planned_publication==pending_job_input.publications);
        assert(planned_clip_headers==pending_job_input.clips&&planned_clip_bodies==pending_job_input.clips);
        assert(transfer_index==transfers.size()&&c->host_depth==1&&c->image_id==40&&c->ls==c->ls_store&&!g_spu_trampoline_fn);
        const uint32_t exit[]={c->pc,c->status,c->stop_code,unsigned(spu_channel_has_data(&c->ch_out_mbox)),c->ch_out_mbox.value,unsigned(spu_channel_has_data(&c->ch_out_intr_mbox)),c->ch_out_intr_mbox.value};
        assert(!memcmp(exit,expected,sizeof(exit))&&c->int_enable==reference->int_enable);++jobs;total_transfers+=n;
        assert(channel_index==reference_channels.size());compared_channel_events+=channel_index;++compared_channel_jobs;
        uint64_t digest=14695981039346656037ull;
        for(const auto& event:reference_channels)for(uint32_t word:{event.direction,event.channel,event.value})for(unsigned byte=0;byte<4;++byte){digest^=uint8_t(word>>(byte*8));digest*=1099511628211ull;}
        fprintf(channel_report,"%s\t%zu\t%016llX\n",current_case.c_str(),channel_index,(unsigned long long)digest);
        const auto channel_shape=std::make_pair(pending_job_input.nodes,pending_job_input.publications);
        if(!channel_examples[channel_shape]) {
            channel_examples[channel_shape]=true;
            const auto name="channels-n"+std::to_string(channel_shape.first)+"-p"+std::to_string(channel_shape.second)+".tsv";
            auto* example=fopen((output_directory/name).string().c_str(),"wb");assert(example);
            fprintf(example,"# %s scratch=%X tag=%u\nindex\tdirection\tchannel\tvalue\n",current_case.c_str(),pending_job_input.scratch_lsa,pending_job_input.dma_tag);
            for(unsigned i=0;i<reference_channels.size();++i){const auto& e=reference_channels[i];fprintf(example,"%u\t%u\t%u\t%08X\n",i,e.direction,e.channel,e.value);}
            assert(!fclose(example));
        }
    }
    assert(!fclose(channel_report));assert(compared_channel_jobs==17714&&compared_channel_events);
    printf("PASS paired full channel transcripts: jobs=%llu events=%llu\n",(unsigned long long)compared_channel_jobs,(unsigned long long)compared_channel_events);
    assert(host_pipeline_jobs==17714&&host_pipeline_publications==39112);
    printf("PASS host control-to-publication pipeline: jobs=%llu publications=%llu bytes=%llu direct-publish=%u\n",(unsigned long long)host_pipeline_jobs,(unsigned long long)host_pipeline_publications,(unsigned long long)host_pipeline_bytes,publish_host_pipeline);
    assert(jobs==17714&&total_transfers==115162&&(native_whole_job||replace_full_clip||dump_finished));
    if(native_whole_job) {
        assert(!full_clip_calls&&!quaternion_calls&&!static_calls&&!hierarchy_plans&&!pose_observation[2].calls);
        puts("PASS native whole animation job: 17714 jobs / 115162 ordered transfers; no lifted candidate blocks or register reconstruction; interrupt-enabled entries fall back");return 0;
    }
    assert(replaced_full_clips==(replace_full_clip?38025:0));
    printf("PASS complete clip replacements=%llu\n",(unsigned long long)replaced_full_clips);
    assert(full_clip_calls==38025&&full_clip_checks==38025);
    printf("PASS host clip curve padding reuse=%llu\n",(unsigned long long)clip_curve_padding_reuse);
    printf("PASS complete native clip: calls=%llu checks=%llu joints=%llu aligned curves=%llu static Q/T/S/C=%llu/%llu/%llu/%llu\n",(unsigned long long)full_clip_calls,(unsigned long long)full_clip_checks,(unsigned long long)full_clip_joints,(unsigned long long)full_clip_curves,(unsigned long long)full_clip_static[0],(unsigned long long)full_clip_static[1],(unsigned long long)full_clip_static[2],(unsigned long long)full_clip_static[3]);
    assert(numeric_clip_time_batches==4096&&numeric_clip_times==18432);
    printf("PASS numeric clip time selection: batches=%llu samples=%llu\n",(unsigned long long)numeric_clip_time_batches,(unsigned long long)numeric_clip_times);
    assert(clip_time_headers==38025&&clip_time_bodies==38025&&clip_time_samples==38025);
    printf("PASS native clip time/body plans: headers=%llu bodies=%llu samples=%llu\n",(unsigned long long)clip_time_headers,(unsigned long long)clip_time_bodies,(unsigned long long)clip_time_samples);
    for(auto [block,n]:clip_time_blocks)printf("clip time selected block=%u clips=%llu\n",block,(unsigned long long)n);
    assert(clip_inputs==38025&&(replace_full_clip?clip_recipe_checks==0:clip_recipe_checks!=0)&&clip_guard_cases==2);
    printf("PASS clip tail input spans=%zu nonzero=%llu guarded parser cases=%u\n",clip_tail_sizes.size(),(unsigned long long)clip_nonzero_tail_inputs,clip_guard_cases);
    for(auto [bytes,n]:clip_tail_sizes)printf("clip published input tail bytes=%u clips=%llu\n",bytes,(unsigned long long)n);
    printf("PASS bounded EA03 clip inputs=%llu nonempty semantic recipes=%llu explicit entry-LS tail inputs=%llu\n",(unsigned long long)clip_inputs,(unsigned long long)clip_recipe_checks,(unsigned long long)clip_tail_inputs);
    if(!replace_full_clip) {
        assert(numeric_vectors==9216&&vector_observation[3].calls==76050&&vector_observation[3].nonempty==21052);
        assert(replace_quaternions?(replaced_vectors==76050):(compared_vectors==24548));
    } else assert(!vector_observation[3].calls&&!quaternion_calls&&!static_calls);
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
    if(!replace_full_clip) {
        assert(scalar_key_observation[0].calls==38025&&scalar_key_observation[0].nonempty==3863&&scalar_key_observation[1].calls==0);
        assert(numeric_curve_keys&&owned_curve_keys);if(replace_quaternions)assert(replaced_curve_keys==38025);else assert(compared_curve_keys==owned_curve_keys);
    } else assert(!scalar_key_observation[0].calls);
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
    for(unsigned kind=0;kind<2;++kind){printf("scalar-key observer kind=%u calls=%llu nonempty=%llu\n",kind,(unsigned long long)scalar_key_observation[kind].calls,(unsigned long long)scalar_key_observation[kind].nonempty);for(const auto& [count,n]:scalar_key_counts[kind])printf("scalar-key count=%u calls=%llu kind=%u\n",count,(unsigned long long)n,kind);}
    printf("PASS scalar-key owners=%llu compared=%llu replacements=%llu numeric=%llu\n",(unsigned long long)owned_curve_keys,(unsigned long long)compared_curve_keys,(unsigned long long)replaced_curve_keys,(unsigned long long)numeric_curve_keys);
    puts("PASS native job frontend: 31 malformed metadata/span cases rejected without changing admission output");
    if(!replace_full_clip)puts("PASS invalid recipes rejected: quaternion=6 FLOAT3=8; empty tracks touch no data");
    for(auto [count,n]:quaternion_counts)printf("quaternion count=%u calls=%llu\n",count,(unsigned long long)n);
}
