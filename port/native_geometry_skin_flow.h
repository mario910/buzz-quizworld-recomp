/* AF18 skin orchestration: host palette transpose and batch dispatch.
 * Retained data routines are explicit callbacks, never native flow coverage.
 * ABI materialization preserves every vector lane and padded pipeline read. */
#pragma once
#include "native_geometry_commands.h"
#include <cstdlib>
namespace buzz {
struct GeometrySkinFlow {uint32_t stack=0,manager=0,palette=0,count=0,vertices=0,workspace=0,capacity=0;bool work=false;};
inline bool read_geometry_skin_flow(const spu_context& c,GeometrySkinFlow& out)
{
    GeometrySkinFlow p;p.stack=c.gpr[1]._u32[0];p.manager=c.gpr[3]._u32[0];p.palette=c.gpr[4]._u32[0];p.count=c.gpr[5]._u32[0];
    if(c.ls!=c.ls_store || p.manager<0xe680 || !geometry_output_range(p.manager,0x180) ||
       (p.stack&15) || p.stack<0xe680+672 || p.stack>SPU_LS_SIZE-32 ||
       geometry_output_overlap(p.manager,0x180,p.stack-672,704))return false;
    const auto* m=c.ls+p.manager;
    if((m[0]&16) || !p.count || !(m[6]&15)){out=p;return true;}
    if(!(m[0]&128) || p.count>4096 || (p.palette&~15u)<0xe680 ||
       uint64_t(p.palette&~15u)+(p.count+2ull)*48>SPU_LS_SIZE ||
       geometry_output_overlap(p.palette&~15u,p.count*48,p.manager,0x180) ||
       geometry_output_overlap(p.palette&~15u,p.count*48,p.stack-672,704))return false;
    p.vertices=(geometry_job_be16(m+8)+3)&~3u;
    if(!p.vertices || p.vertices>4096)return false;
    const uint32_t frame=p.stack-672;
    auto writes_safe=[&](uint32_t at,uint32_t size){
        at&=~15u;
        return at>=0xe680 && uint64_t(at)+size<=SPU_LS_SIZE &&
            !geometry_output_overlap(at,size,p.manager,0x180) &&
            !geometry_output_overlap(at,size,frame,80) &&
            !geometry_output_overlap(at,size,frame+272,432);
    };
    if(!geometry_job_be32(m+0x5c))return false;
    for(unsigned off=0x5c;off<=0x68;off+=4){
        const uint32_t at=geometry_job_be32(m+off);
        if(at && (!writes_safe(at,p.vertices*16) || uint64_t(at&~15u)+p.vertices*16+64>SPU_LS_SIZE))return false;
    }
    const uint32_t weights=c.gpr[6]._u32[0];
    if((weights&~15u)<0xe680 || uint64_t(weights&~15u)+p.vertices*8+16>SPU_LS_SIZE)return false;
    p.workspace=frame+80;p.capacity=4;
    if(p.vertices>11){p.capacity=(p.vertices/3)&~3u;p.workspace=geometry_job_be32(m+0x10+4*(m[0]&15));}
    if(!p.capacity || !writes_safe(p.workspace,p.capacity*48) || uint64_t(p.workspace&~15u)+p.capacity*48+192>SPU_LS_SIZE)return false;
    const uint32_t targets[]={0xbd4c,0xbe20,0xbf7c,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xb868,0xc14c,0xc280,0xb5a0};
    for(unsigned i=0;i<19;++i)if(geometry_job_be32(c.ls+0xb554+4*i)!=targets[i]-0xb554)return false;
    p.work=true;out=p;return true;
}
inline void run_geometry_skin_data(spu_context& c,buzz_geometry_job_fn root,buzz_geometry_job_fn tail)
{
    g_spu_trampoline_fn=nullptr;root(&c);
    for(unsigned steps=0;g_spu_trampoline_fn!=tail;++steps){
        if(!g_spu_trampoline_fn || steps>=1048576)std::abort();
        auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&c);
    }
    g_spu_trampoline_fn=nullptr;
}
inline void transpose_geometry_skin_palette(spu_context& ctx,const GeometrySkinFlow& p)
{
    auto& r=ctx.gpr;
    r[36] = spu_a(r[5], r[5]);
    r[21] = spu_fsmbi(0xF00);
    r[20] = spu_fsmbi(0xF0);
    r[35] = spu_a(r[36], r[5]);
    r[24] = spu_ls_read128(&ctx, 0xDA10);
    r[25] = spu_ls_read128(&ctx, 0xDA60);
    r[17] = spu_shli(r[35], 4);
    r[26] = spu_a(r[17], r[97]);
    r[13] = spu_sfi(r[17], 0);
    r[16] = spu_ai(r[26], 16);
    r[17] = spu_shlqbyi(r[26], 0);
    r[33] = spu_ori(r[13], 0);
    r[9] = spu_shlqbyi(r[13], 0);
    r[15] = spu_ai(r[26], 32);
    r[32] = spu_ai(r[33], 48);
    r[31] = spu_ls_read128(&ctx, r[26]._u32[0] + r[33]._u32[0]);
    r[22] = spu_ai(r[26], 96);
    r[29] = spu_ls_read128(&ctx, r[16]._u32[0] + r[33]._u32[0]);
    r[19] = spu_ai(r[26], 112);
    r[27] = spu_ls_read128(&ctx, r[15]._u32[0] + r[33]._u32[0]);
    r[18] = spu_ai(r[26], 128);
    r[12] = spu_ls_read128(&ctx, r[15]._u32[0] + r[32]._u32[0]);
    r[10] = spu_ls_read128(&ctx, r[26]._u32[0] + r[32]._u32[0]);
    r[11] = spu_ls_read128(&ctx, r[16]._u32[0] + r[32]._u32[0]);
    r[14] = spu_selb(r[29], r[31], r[20]);
    r[30] = spu_selb(r[27], r[31], r[21]);
    r[28] = spu_selb(r[31], r[29], r[21]);
    r[14] = spu_shufb(r[27], r[14], r[25]);
    r[3] = spu_shufb(r[29], r[30], r[24]);
    r[5] = spu_selb(r[28], r[27], r[20]);
    const u128 initial_offset=r[13];
    for(unsigned i=0;i<p.count;++i){
        const uint32_t at=p.palette+i*48;
        const u128 a=spu_ls_read128(&ctx,at),b=spu_ls_read128(&ctx,at+16),c=spu_ls_read128(&ctx,at+32);
        const u128 x=spu_selb(spu_selb(a,b,r[21]),c,r[20]);
        const u128 y=spu_shufb(b,spu_selb(c,a,r[21]),r[24]);
        const u128 z=spu_shufb(c,spu_selb(b,a,r[20]),r[25]);
        spu_ls_write128(&ctx,at,x);spu_ls_write128(&ctx,at+16,y);spu_ls_write128(&ctx,at+32,z);
    }
    const uint32_t at=p.palette+p.count*48;
    const u128 a=spu_ls_read128(&ctx,at),b=spu_ls_read128(&ctx,at+16),c=spu_ls_read128(&ctx,at+32);
    r[43]=spu_selb(b,a,r[20]);r[42]=spu_selb(c,a,r[21]);r[39]=spu_selb(a,b,r[21]);
    r[37]=spu_ls_read128(&ctx,at+48);r[40]=spu_ls_read128(&ctx,at+64);r[38]=spu_ls_read128(&ctx,at+80);
    r[41]=spu_ai(initial_offset,int(p.count*48));r[13]=r[41];r[9]=r[41];
    r[5]=spu_selb(r[39],c,r[20]);r[10]=r[37];r[14]=spu_shufb(c,r[43],r[25]);
    r[3]=spu_shufb(b,r[42],r[24]);r[12]=r[38];r[11]=r[40];
}
inline void execute_geometry_skin_flow(spu_context& ctx,const GeometrySkinFlow& p,const buzz_geometry_skin_flow_functions& f)
{
    auto& r=ctx.gpr;const uint32_t old=p.stack;g_spu_trampoline_fn=nullptr;
    r[7]=spu_il(-672);r[23]=r[6];spu_ls_write128(&ctx,old-0x70,r[86]);r[86]=r[3];
    spu_ls_write128(&ctx,old-0x120,r[97]);r[97]=r[4];spu_ls_write128(&ctx,old-0x190,r[126]);
    r[8]=spu_ila(0xaf40);r[126]=spu_link(0xaf40);
    spu_ls_write128(&ctx,old+16,r[0]);
    for(unsigned reg=80;reg<=103;++reg)if(reg!=86 && reg!=97)spu_ls_write128(&ctx,old-(reg-79)*16,r[reg]);
    r[126]=spu_sf(r[8],r[126]);spu_ls_write128(&ctx,old-672,r[1]);r[1]=spu_a(r[1],r[7]);
    r[2]=spu_ls_read128(&ctx,r[3]._u32[0]);r[4]=spu_rotqbyi(r[2],13);r[3]=spu_andi(r[4],16);
    auto finish=[&]{
        r[55]=spu_il(672);r[1]=spu_a(r[1],r[55]);r[0]=spu_ls_read128(&ctx,old+16);
        for(unsigned reg=80;reg<=103;++reg)r[reg]=spu_ls_read128(&ctx,old-(reg-79)*16);
        r[126]=spu_ls_read128(&ctx,old-0x190);
    };
    if(r[3]._u32[0] || !r[5]._u32[0]){finish();SPU_RET(&ctx);}
    r[10]=spu_ls_read128(&ctx,r[86]._u32[0]);r[9]=spu_rotqbyi(r[10],3);r[6]=spu_andi(r[9],15);
    if(!r[6]._u32[0]){finish();SPU_RET(&ctx);}
    r[12]=spu_cgtbi(r[4],-1);r[11]=spu_xsbh(r[12]);
    transpose_geometry_skin_palette(ctx,p);
    ctx.pc=0xb100;r[85]=r[23];r[3]=r[86];r[81]=r[97];
    geometry_command_call(ctx,f.position_pointer,0xb160);
    r[90]=spu_ilhu(0x3b80);r[80]=r[3];r[3]=r[86];r[89]=spu_ilhu(0x405);
    geometry_command_call(ctx,f.normal_pointer,0xb178);
    r[88]=spu_ilhu(0x809);r[83]=r[3];r[3]=r[86];r[87]=spu_ilhu(0xc0d);
    geometry_command_call(ctx,f.tangent_pointer,0xb190);
    r[98]=spu_ai(r[81],16);r[82]=r[3];r[3]=r[86];r[84]=spu_ai(r[81],32);
    geometry_command_call(ctx,f.aux_pointer,0xb1a8);
    r[38] = spu_ila(0x10203);
    r[52] = spu_ls_read128(&ctx, r[86]._u32[0] + 0x0);
    r[11] = spu_ori(r[3], 0);
    r[91] = spu_ls_read128(&ctx, 0xD930);
    r[10] = spu_ori(r[83], 0);
    r[43] = spu_ls_read128(&ctx, 0xDA00);
    r[90] = spu_iohl(r[90], 0x8080);
    r[50] = spu_ls_read128(&ctx, 0xD940);
    r[89] = spu_iohl(r[89], 0x607);
    r[92] = spu_ls_read128(&ctx, 0xD920);
    r[88] = spu_iohl(r[88], 0xA0B);
    r[95] = spu_ls_read128(&ctx, 0xD890);
    r[87] = spu_iohl(r[87], 0xE0F);
    r[51] = spu_rotqbyi(r[52], 3);
    r[55] = spu_ilhu(0x3F80);
    r[45] = spu_ls_read128(&ctx, 0xDA70);
    r[21] = spu_ls_read128(&ctx, 0xDAA0);
    r[46] = spu_rotqbyi(r[91], 8);
    r[8] = spu_andi(r[51], 15);
    r[70] = spu_fsmbi(0xFF);
    r[94] = spu_ai(r[95], 8);
    r[79] = spu_rotqbyi(r[50], 8);
    r[49] = spu_ceqbi(r[8], 3);
    r[39] = spu_shufb(r[43], r[38], r[43]);
    r[48] = spu_ceqbi(r[8], 6);
    r[51] = spu_fsmbi(0xFFF0);
    r[72] = spu_selb(r[46], r[45], r[70]);
    r[47] = spu_fsm(r[49]);
    r[73] = spu_xorbi(r[21], 16);
    r[44] = spu_fsm(r[48]);
    r[76] = spu_orbi(r[92], 4);
    r[78] = spu_fsmbi(0xFF0);
    r[75] = spu_orbi(r[92], 8);
    r[24] = spu_sfi(r[47], 0);
    r[25] = spu_sfi(r[44], 0);
    r[74] = spu_orbi(r[92], 12);
    r[23] = spu_or(r[24], r[25]);
    r[12] = spu_ahi(r[8], -4);
    r[9] = spu_ceqbi(r[23], 0);
    r[20] = spu_xsbh(r[9]);
    r[13]=spu_il(!r[20]._u16[0] && r[83]._u32[0]?1:0);
    r[19] = spu_nor(r[9], r[9]);
    r[5] = spu_ls_read128(&ctx, r[86]._u32[0] + 0x0);
    r[30] = spu_ceqbi(r[8], 4);
    r[103] = spu_fsmbi(0x0);
    r[29] = spu_ceqbi(r[8], 1);
    r[27] = spu_fsm(r[19]);
    r[28] = spu_nor(r[30], r[30]);
    spu_ls_write128(&ctx, r[1]._u32[0] + 0x20, r[83]);
    r[26] = spu_nor(r[29], r[29]);
    r[102] = spu_ceqi(r[10], 0);
    r[10] = spu_fsm(r[28]);
    r[18] = spu_sfi(r[27], 0);
    r[9] = spu_fsm(r[26]);
    r[8] = spu_ceqi(r[82], 0);
    r[71] = spu_rotqbyi(r[5], 6);
    r[3] = spu_xori(r[18], 1);
    r[4] = spu_sfi(r[10], 0);
    r[2] = spu_sfi(r[9], 0);
    r[7] = spu_andi(r[3], 255);
    r[101] = spu_and(r[2], r[4]);
    r[93] = spu_selb(r[7], r[103], r[102]);
    r[100] = spu_nor(r[8], r[8]);
    r[99] = spu_ceqbi(r[101], 0);
    r[58] = spu_sf(r[100], r[93]);
    r[68] = spu_shli(r[93], 4);
    r[69] = spu_xsbh(r[99]);
    r[77] = spu_ila(0xFFFF);
    r[63] = spu_shli(r[58], 4);
    r[64] = spu_and(r[71], r[77]);
    r[65] = spu_ceqhi(r[69], 0);
    r[67] = spu_ai(r[68], 32);
    r[61] = spu_ceqi(r[11], 0);
    r[57] = spu_fsm(r[65]);
    r[60] = spu_ai(r[64], 3);
    spu_ls_write128(&ctx, r[67]._u32[0] + r[1]._u32[0], r[82]);
    r[62] = spu_ai(r[63], 32);
    r[59] = spu_nor(r[61], r[61]);
    r[82] = spu_andi(r[60], -4);
    spu_ls_write128(&ctx, r[62]._u32[0] + r[1]._u32[0], r[11]);
    r[54] = spu_sf(r[59], r[58]);
    r[67] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x20);
    r[56] = spu_andi(r[57], 16);
    r[71] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x30);
    r[53] = spu_clgti(r[82], 11);
    r[93] = spu_ls_read128(&ctx, r[1]._u32[0] + 0x40);
    r[10] = spu_or(r[56], r[54]);
    r[96] = spu_ai(r[1], 80);
    r[99] = spu_il(4);
    if(r[53]._u32[0]){
    r[24] = spu_ilhu(0xAAAB);
    r[23] = spu_ls_read128(&ctx, r[86]._u32[0] + 0x0);
    r[21] = spu_ilhu(0xAAAA);
    r[24] = spu_iohl(r[24], 0xAAAA);
    r[21] = spu_iohl(r[21], 0xAAAB);
    r[41] = spu_mpyu(r[82], r[24]);
    r[40] = spu_mpyhhu(r[82], r[24]);
    r[36] = spu_mpyu(r[82], r[21]);
    r[20] = spu_rotqbyi(r[23], 13);
    r[17] = spu_mpyhhu(r[82], r[21]);
    r[42] = spu_andi(r[20], 15);
    r[37] = spu_shli(r[41], 16);
    r[34] = spu_shli(r[42], 2);
    r[35] = spu_rotmi(r[41], -16);
    r[16] = spu_shli(r[40], 16);
    r[33] = spu_rotmi(r[40], -16);
    r[14] = spu_a(r[34], r[86]);
    r[32] = spu_cg(r[36], r[37]);
    r[15] = spu_a(r[36], r[37]);
    r[11] = spu_ls_read128(&ctx, r[14]._u32[0] + 0x10);
    r[32] = spu_addx(r[17], r[35], r[32]);
    r[22] = spu_cg(r[15], r[16]);
    r[22] = spu_addx(r[32], r[33], r[22]);
    r[31] = spu_rotmi(r[22], -1);
    r[96] = spu_rotqby(r[11], r[14]);
    r[99] = spu_andi(r[31], -4);
    }
    r[54] = spu_ori(r[81], 0);
    r[77] = spu_ls_read128(&ctx, 0xDAB0);
    r[101] = spu_clgtbi(r[12], 2);
    r[81] = spu_ls_read128(&ctx, 0xD900);
    r[12] = spu_ila(0xB554);
    r[53] = spu_shlqbyi(r[98], 0);
    r[52] = spu_ori(r[84], 0);
    r[69] = spu_shlqbyi(r[96], 0);
    r[102] = spu_ceqbi(r[13], 0);
    r[100] = spu_ai(r[10], -1);
    r[103] = spu_ai(r[96], 192);
    r[98] = spu_a(r[12], r[126]);
    for(unsigned batch=0;;++batch){
        if(batch>=4096)std::abort();
        r[25]=spu_clgt(r[82],r[99]);r[13]=spu_xsbh(r[101]);r[84]=spu_selb(r[82],r[99],r[25]);
        const bool weighted=r[13]._u16[0]!=0;ctx.pc=weighted?0xb880:0xb348;
        run_geometry_skin_data(ctx,weighted?f.weighted:f.rigid,f.direction_entry);
    r[60] = spu_shli(r[100], 2);
    r[37] = spu_ls_read128(&ctx, r[67]._u32[0] + 0x0);
    r[8] = spu_clgti(r[100], 18);
    r[36] = spu_ls_read128(&ctx, r[67]._u32[0] + 0x10);
    r[33] = spu_ori(r[67], 0);
    r[35] = spu_ls_read128(&ctx, r[67]._u32[0] + 0x20);
    r[40] = spu_ori(r[103], 0);
    r[34] = spu_ls_read128(&ctx, r[67]._u32[0] + 0x30);
    r[29] = spu_a(r[98], r[60]);
    r[9] = spu_ls_read128(&ctx, r[98]._u32[0] + r[60]._u32[0]);
    r[58] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x0);
    r[57] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x10);
    r[56] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x20);
    r[50] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x30);
    r[49] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x40);
    r[12] = spu_rotqby(r[9], r[29]);
    r[48] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x50);
    r[47] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x60);
    r[2] = spu_a(r[12], r[98]);
    r[46] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x70);
    r[45] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x80);
    r[44] = spu_ls_read128(&ctx, r[69]._u32[0] + 0x90);
    r[42] = spu_ls_read128(&ctx, r[69]._u32[0] + 0xA0);
    r[41] = spu_ls_read128(&ctx, r[69]._u32[0] + 0xB0);
    r[65] = spu_shufb(r[37], r[38], r[43]);
    r[61] = spu_shufb(r[36], r[38], r[43]);
    r[64] = spu_shufb(r[35], r[38], r[43]);
    r[59] = spu_shufb(r[34], r[38], r[43]);
    r[66] = spu_shufb(r[37], r[38], r[39]);
    r[63] = spu_shufb(r[36], r[38], r[39]);
    r[62] = spu_shufb(r[35], r[38], r[39]);
    r[60] = spu_shufb(r[34], r[38], r[39]);
        if(!r[8]._u32[0]){
            const uint32_t target=r[2]._u32[0];const uint32_t targets[]={0xbd4c,0xbe20,0xbf7c,0xc14c,0xc280,0xb5a0};
            if(target!=0xb868){unsigned i=0;while(i<6 && target!=targets[i])++i;if(i==6)std::abort();
                ctx.pc=target;run_geometry_skin_data(ctx,f.directions[i],f.batch_entry);
            }
        }
        ctx.pc=0xb868;r[82]=spu_sf(r[84],r[82]);
        if(!r[82]._u32[0])break;
    }
    r[3]=r[86];r[4]=r[97];geometry_command_call(ctx,f.publish_palette,0xb87c);
    finish();SPU_RET(&ctx);
}
} // namespace buzz
