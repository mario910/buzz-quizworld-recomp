/* Image-41 output allocation. Host-local vectors preserve the complete ABI;
 * only channel controls and ordered LS accesses touch the context mid-root.
 * A nonzero result is an original reporting continuation, never a restart. */
#pragma once
#include "native_geometry_job.h"
#include "spu_helpers.h"

namespace buzz {
struct GeometryRing {};
struct GeometryRingContinuations { buzz_geometry_job_fn initial_report,shared_report; };
// Used only against a recorded I/O slice: never execute a second real DMA arm.
inline void run_geometry_ring_reference(spu_context& c,buzz_geometry_job_fn entry,const buzz_geometry_ring_functions& f,unsigned frontier)
{
    g_spu_trampoline_fn=nullptr;entry(&c);
    for(unsigned steps=0;;++steps){
        const auto next=g_spu_trampoline_fn;
        if(c.pc==0x38d0 && next==f.tail){
            if(frontier)buzz_geometry_job_native_fail(&c,"ring reference returned instead of reporting");
            g_spu_trampoline_fn=nullptr;next(&c);return;
        }
        if((c.pc==0x3dd0 && next==f.initial_report) || (c.pc==0x4078 && next==f.shared_report)){
            if(frontier!=c.pc)buzz_geometry_job_native_fail(&c,"ring reporting boundary differs");
            return;
        }
        if(!next || steps>=1000000)buzz_geometry_job_native_fail(&c,"ring reference escaped root boundary");
        g_spu_trampoline_fn=nullptr;next(&c);
    }
}
inline bool read_geometry_ring(const spu_context& c,GeometryRing& out,const char** reason=nullptr)
{
    auto reject=[&](const char* why){if(reason)*reason=why;return false;};
    if(c.ls!=c.ls_store)return reject("foreign LS");
    if(c.image_id!=41 || c.policy_mode || c.resident_ovl)return reject("foreign image/overlay/policy");
    if(c.int_enable || c.irq_saved || c.irq_frame)return reject("unlifted interrupt-enable continuation");
    out={};return true;
}

class GeometryRingAllocation {
    spu_context& c;
    u128 r[128];
    u128 load(uint32_t address){return spu_ls_read128(&c,address);}
    void store(uint32_t address,u128 value){spu_ls_write128(&c,address,value);}
    void wr(unsigned channel,unsigned reg){buzz_geometry_job_wrch(&c,channel,r[reg]);}
    u128 rd(unsigned channel){return buzz_geometry_job_rdch(&c,channel);}
    void open()
    {
        const uint32_t sp=r[1]._u32[0];
        r[17]=spu_il(-1344);store(sp-16,r[80]);r[10]=spu_il(0);store(sp-32,r[81]);
        r[15]=spu_il(639);store(sp-48,r[82]);r[13]=spu_il(895);store(sp-96,r[85]);
        r[80]=r[6];store(sp-160,r[89]);r[81]=r[4];store(sp-176,r[90]);r[4]=spu_andi(r[4],2);
        store(sp-288,r[126]);r[18]=spu_ila(0x3800);r[126]=spu_link(0x3800);
        r[89]=r[3];store(sp+16,r[0]);store(sp-64,r[83]);store(sp-80,r[84]);r[126]=spu_sf(r[18],r[126]);
        for(unsigned reg=86;reg<=88;++reg)store(sp-(reg-79)*16,r[reg]);
        for(unsigned reg=91;reg<=96;++reg)store(sp-(reg-79)*16,r[reg]);
        store(sp-1344,r[1]);r[1]=spu_a(r[1],r[17]);
        r[11]=load(r[6]._u32[0]+16);r[16]=spu_ai(r[1],32);r[12]=spu_cbd(r[1],4);r[14]=spu_ai(r[1],32);
        r[9]=spu_a(r[16],r[15]);r[8]=spu_a(r[14],r[13]);r[6]=spu_ai(r[1],159);
        r[11]=spu_shufb(r[10],r[11],r[12]);r[2]=spu_ai(r[1],415);r[10]=spu_andi(r[6],-128);
        r[85]=spu_andi(r[2],-128);r[82]=spu_andi(r[9],-128);store(r[80]._u32[0]+16,r[11]);r[90]=spu_andi(r[8],-128);
    }
    void close()
    {
        r[80]=spu_il(1344);r[3]=r[12];r[1]=spu_a(r[1],r[80]);const uint32_t sp=r[1]._u32[0];
        r[0]=load(sp+16);for(unsigned reg=80;reg<=96;++reg)r[reg]=load(sp-(reg-79)*16);r[126]=load(sp-288);
        memcpy(c.gpr,r,sizeof r);SPU_RET(&c);
    }
    uint32_t frontier(uint32_t pc)
    {
        memcpy(c.gpr,r,sizeof r);c.pc=pc;g_spu_trampoline_fn=nullptr;return pc;
    }
    void direct()
    {
        r[30]=spu_andi(r[81],-4);r[29]=load(r[80]._u32[0]);r[24]=spu_il(-1);r[28]=spu_cwd(r[1],0);
        r[21]=spu_andi(r[81],1);r[26]=spu_cwd(r[1],4);r[19]=spu_a(r[5],r[30]);r[22]=spu_cwd(r[1],8);
        r[27]=spu_il(0);r[20]=spu_cwd(r[1],12);r[12]=spu_il(1);r[25]=spu_shufb(r[30],r[29],r[28]);
        r[23]=spu_shufb(r[27],r[11],r[28]);r[7]=spu_shufb(r[24],r[25],r[26]);store(r[80]._u32[0]+16,r[23]);
        r[5]=spu_shufb(r[21],r[7],r[22]);r[3]=spu_shufb(r[19],r[5],r[20]);store(r[80]._u32[0],r[3]);
    }
    void finish()
    {
        r[78]=spu_andi(r[95],1);r[95]=spu_ceqbi(r[78],0);r[96]=spu_xsbh(r[95]);
        // Admission excludes enabled interrupts. Do not replace 3CF4's resolver.
        r[79]=spu_xsbh(r[11]);r[12]=spu_fsmbi(0);
        if(!r[79]._u16[0]){
            r[84]=spu_ceqbi(r[3],0);r[86]=spu_xsbh(r[84]);
            if(r[86]._u16[0]){
                r[6]=spu_il(0);r[18]=load(r[80]._u32[0]+16);r[85]=spu_cwd(r[1],0);
                r[11]=spu_shufb(r[6],r[18],r[85]);store(r[80]._u32[0]+16,r[11]);
            }else{
                r[12]=load(0xe610);r[10]=load(r[80]._u32[0]+16);r[4]=spu_cwd(r[1],0);
                r[2]=spu_rotqbyi(r[12],4);r[8]=spu_shufb(r[2],r[10],r[4]);store(r[80]._u32[0]+16,r[8]);
            }
            r[12]=spu_il(1);r[19]=load(r[80]._u32[0]);r[20]=spu_cwd(r[1],12);
            r[3]=spu_a(r[83],r[19]);r[83]=spu_shufb(r[3],r[19],r[20]);store(r[80]._u32[0],r[83]);
        }
        close();
    }
    void publish_lane(bool write_result)
    {
        if(write_result){
            store(r[80]._u32[0],r[2]);r[76]=load(0xe610);r[75]=spu_shufb(r[76],r[2],r[8]);store(r[80]._u32[0],r[75]);
        }
        r[77]=r[96];wr(MFC_LSA,77);wr(MFC_EAL,93);r[93]=spu_il(176);wr(MFC_Cmd,93);r[3]=rd(MFC_RdAtomicStat);r[3]=spu_il(1);
        finish();
    }
    // Shared disabled-lane allocation: each failed PUTLLC reacquires the line.
    void shared_disabled()
    {
        r[16]=r[81];r[18]=r[4];r[15]=r[10];r[17]=spu_il(180);
        for(;;){
            wr(MFC_LSA,15);wr(MFC_EAL,16);wr(MFC_Cmd,18);r[2]=rd(MFC_RdAtomicStat);
            r[7]=spu_il(12);r[46]=spu_il(8);r[45]=spu_ai(r[10],12);r[44]=load(r[10]._u32[0]+r[7]._u32[0]);
            r[43]=spu_ai(r[10],8);r[42]=load(r[10]._u32[0]+r[46]._u32[0]);r[8]=spu_il(20);r[13]=spu_cwd(r[10],12);
            r[11]=spu_ai(r[10],20);r[12]=spu_cwd(r[10],4);r[14]=spu_cwd(r[1],8);
            r[9]=spu_rotqby(r[44],r[45]);r[41]=spu_rotqby(r[42],r[43]);r[6]=spu_a(r[83],r[9]);r[40]=spu_clgt(r[6],r[41]);
            if(r[40]._u32[0]){
                r[9]=spu_il(0);r[51]=load(r[10]._u32[0]+r[8]._u32[0]);r[48]=load(r[10]._u32[0]+r[8]._u32[0]);
                r[50]=spu_rotqby(r[51],r[11]);r[49]=spu_a(r[83],r[50]);r[47]=spu_shufb(r[49],r[48],r[12]);
                store(r[10]._u32[0]+r[8]._u32[0],r[47]);
            }else{
                r[57]=load(r[10]._u32[0]+r[7]._u32[0]);r[53]=load(r[80]._u32[0]);r[56]=spu_shufb(r[6],r[57],r[13]);
                store(r[10]._u32[0]+r[7]._u32[0],r[56]);r[55]=load(r[10]._u32[0]+16);r[54]=spu_rotqby(r[55],r[10]);
                r[52]=spu_shufb(r[54],r[53],r[14]);store(r[80]._u32[0],r[52]);
            }
            wr(MFC_LSA,15);wr(MFC_EAL,16);wr(MFC_Cmd,17);r[61]=rd(MFC_RdAtomicStat);r[60]=spu_andi(r[61],1);
            r[59]=spu_ceqbi(r[60],0);r[58]=spu_xsbh(r[59]);if(r[58]._u16[0])break;
        }
        r[77]=spu_il(4);r[74]=load(r[80]._u32[0]);r[72]=spu_ai(r[10],4);r[75]=spu_cwd(r[1],0);r[76]=spu_ceqi(r[9],0);
        r[71]=load(r[10]._u32[0]+r[77]._u32[0]);r[69]=load(r[10]._u32[0]);r[73]=spu_nor(r[76],r[76]);
        r[65]=spu_cwd(r[1],4);r[70]=spu_sfi(r[73],0);r[64]=spu_shufb(r[9],r[74],r[75]);r[11]=spu_ceqbi(r[70],0);
        r[66]=spu_rotqby(r[71],r[72]);r[68]=spu_rotqby(r[69],r[10]);r[67]=spu_sf(r[68],r[9]);r[63]=spu_a(r[66],r[67]);
        r[62]=spu_shufb(r[63],r[64],r[65]);store(r[80]._u32[0],r[62]);r[3]=spu_il(0);finish();
    }
    void shared_reserve()
    {
        r[16]=r[81];r[14]=r[82];r[15]=spu_il(208);r[17]=spu_il(180);
        for(;;){
            wr(MFC_LSA,14);wr(MFC_EAL,16);wr(MFC_Cmd,15);r[2]=rd(MFC_RdAtomicStat);
            r[7]=spu_il(12);r[63]=spu_il(8);r[62]=spu_ai(r[82],12);r[61]=load(r[82]._u32[0]+r[7]._u32[0]);
            r[60]=spu_ai(r[82],8);r[59]=load(r[82]._u32[0]+r[63]._u32[0]);r[10]=spu_il(20);r[12]=spu_cwd(r[82],12);
            r[8]=spu_ai(r[82],20);r[11]=spu_cwd(r[82],4);r[13]=spu_cwd(r[1],8);r[9]=spu_rotqby(r[61],r[62]);
            r[58]=spu_rotqby(r[59],r[60]);r[6]=spu_a(r[83],r[9]);r[57]=spu_clgt(r[6],r[58]);
            if(r[57]._u32[0]){
                r[9]=spu_il(0);r[68]=load(r[82]._u32[0]+r[10]._u32[0]);r[65]=load(r[82]._u32[0]+r[10]._u32[0]);
                r[67]=spu_rotqby(r[68],r[8]);r[66]=spu_a(r[83],r[67]);r[64]=spu_shufb(r[66],r[65],r[11]);store(r[82]._u32[0]+r[10]._u32[0],r[64]);
            }else{
                r[74]=load(r[82]._u32[0]+r[7]._u32[0]);r[70]=load(r[80]._u32[0]);r[73]=spu_shufb(r[6],r[74],r[12]);
                store(r[82]._u32[0]+r[7]._u32[0],r[73]);r[72]=load(r[82]._u32[0]+16);r[71]=spu_rotqby(r[72],r[82]);
                r[69]=spu_shufb(r[71],r[70],r[13]);store(r[80]._u32[0],r[69]);
            }
            wr(MFC_LSA,14);wr(MFC_EAL,16);wr(MFC_Cmd,17);r[78]=rd(MFC_RdAtomicStat);r[77]=spu_andi(r[78],1);
            r[76]=spu_ceqbi(r[77],0);r[75]=spu_xsbh(r[76]);if(r[75]._u16[0])break;
        }
    }
    void refresh_lane()
    {
        wr(MFC_LSA,14);wr(MFC_EAL,16);wr(MFC_Size,15);r[29]=load(r[89]._u32[0]+0x150);r[28]=spu_rotqbyi(r[29],4);
        wr(MFC_TagID,28);wr(MFC_Cmd,12);r[27]=load(r[89]._u32[0]+0x150);r[26]=spu_rotqbyi(r[27],4);r[25]=spu_shl(r[17],r[26]);
        wr(MFC_WrTagMask,25);wr(MFC_WrTagUpdate,13);r[2]=rd(MFC_RdTagStat);r[19]=spu_il(24);r[24]=load(r[10]._u32[0]);
        r[21]=load(r[92]._u32[0]+r[19]._u32[0]);r[23]=spu_cwd(r[92],8);r[22]=spu_rotqby(r[24],r[10]);
        r[20]=spu_shufb(r[22],r[21],r[23]);store(r[92]._u32[0]+r[19]._u32[0],r[20]);r[3]=load(0xe610);r[5]=spu_rotqbyi(r[3],8);
    }
    bool try_lane()
    {
        r[14]=spu_andi(r[84],127);r[16]=r[84];r[18]=spu_ceqi(r[5],0);r[7]=spu_fsmbi(0);
        r[10]=spu_a(r[85],r[14]);r[11]=spu_sfi(r[18],0);r[14]=r[10];r[9]=spu_a(r[83],r[86]);
        r[17]=spu_il(1);r[15]=spu_il(4);r[12]=spu_il(64);r[13]=spu_il(2);
        for(;;){
            r[6]=spu_ceqbi(r[11],0);r[85]=spu_xsbh(r[6]);if(!r[85]._u16[0])refresh_lane();
            r[45]=spu_clgt(r[9],r[88]);r[40]=spu_clgt(r[5],r[9]);r[44]=spu_clgt(r[5],r[91]);r[43]=spu_nor(r[45],r[45]);
            r[39]=spu_nor(r[40],r[40]);r[38]=spu_clgt(r[5],r[86]);r[42]=spu_sfi(r[44],0);r[41]=spu_sfi(r[43],0);
            r[36]=spu_sfi(r[39],0);r[35]=spu_sfi(r[38],0);r[37]=spu_or(r[41],r[42]);r[33]=spu_and(r[35],r[36]);
            r[34]=spu_ceqbi(r[37],0);r[31]=spu_ceqbi(r[33],0);r[32]=spu_nor(r[34],r[34]);r[30]=spu_xsbh(r[31]);
            r[6]=spu_xsbh(r[6]);r[4]=spu_fsm(r[32]);bool selected=false;
            if(r[30]._u16[0]){r[3]=spu_clgt(r[9],r[88]);selected=r[4]._u32[0]!=0;}
            if(selected){
                r[46]=spu_ceqi(r[3],0);r[47]=spu_il(1);r[7]=spu_il(1);r[86]=spu_selb(r[87],r[86],r[46]);
                r[9]=spu_selb(r[91],r[9],r[46]);r[7]=spu_selb(r[47],r[7],r[46]);
            }else{r[11]=spu_il(1);if(!r[6]._u16[0])return false;}
            r[53]=spu_ceqbi(r[7],0);r[52]=spu_xsbh(r[53]);if(r[52]._u16[0])continue;
            r[17]=spu_il(12);r[56]=spu_cwd(r[92],12);r[90]=spu_sf(r[87],r[86]);r[16]=spu_cwd(r[1],0);r[11]=spu_il(0);
            r[55]=load(r[92]._u32[0]+r[17]._u32[0]);r[87]=spu_cwd(r[1],4);r[8]=spu_cwd(r[1],8);
            r[54]=spu_shufb(r[9],r[55],r[56]);store(r[92]._u32[0]+r[17]._u32[0],r[54]);r[94]=load(r[80]._u32[0]);
            r[82]=spu_shufb(r[86],r[94],r[16]);store(r[80]._u32[0],r[82]);r[91]=load(0xe600);r[89]=spu_rotqbyi(r[91],4);
            r[81]=spu_a(r[89],r[90]);r[2]=spu_shufb(r[81],r[82],r[87]);return true;
        }
    }
    void refresh_after_shared()
    {
        wr(MFC_LSA,11);wr(MFC_EAL,12);wr(MFC_Size,13);r[43]=load(r[89]._u32[0]+0x150);r[42]=spu_rotqbyi(r[43],4);
        wr(MFC_TagID,42);wr(MFC_Cmd,14);r[41]=load(r[89]._u32[0]+0x150);r[40]=spu_rotqbyi(r[41],4);r[39]=spu_shl(r[17],r[40]);
        wr(MFC_WrTagMask,39);wr(MFC_WrTagUpdate,15);r[2]=rd(MFC_RdTagStat);r[33]=spu_il(24);r[38]=load(r[8]._u32[0]);
        r[35]=load(r[92]._u32[0]+r[33]._u32[0]);r[37]=spu_cwd(r[92],8);r[36]=spu_rotqby(r[38],r[8]);
        r[34]=spu_shufb(r[36],r[35],r[37]);store(r[92]._u32[0]+r[33]._u32[0],r[34]);r[32]=load(0xe610);r[5]=spu_rotqbyi(r[32],8);
    }
    void reconsider_lane()
    {
        r[29]=spu_andi(r[81],127);r[12]=r[81];r[30]=spu_ceqi(r[5],0);r[6]=spu_fsmbi(0);r[8]=spu_a(r[90],r[29]);
        r[16]=spu_sfi(r[30],0);r[11]=r[8];r[10]=spu_a(r[83],r[82]);r[17]=spu_il(1);r[13]=spu_il(4);r[14]=spu_il(64);r[15]=spu_il(2);
        for(;;){
            r[7]=spu_ceqbi(r[16],0);r[31]=spu_xsbh(r[7]);if(!r[31]._u16[0])refresh_after_shared();
            r[51]=spu_clgt(r[5],r[10]);r[50]=spu_clgt(r[5],r[82]);r[49]=spu_nor(r[51],r[51]);r[47]=spu_sfi(r[50],0);
            r[48]=spu_sfi(r[49],0);r[46]=spu_and(r[47],r[48]);r[45]=spu_ceqbi(r[46],0);r[44]=spu_xsbh(r[45]);bool selected=false;
            if(r[44]._u16[0]){
                r[57]=spu_clgt(r[10],r[88]);r[56]=spu_clgt(r[5],r[84]);r[55]=spu_nor(r[57],r[57]);r[54]=spu_sfi(r[56],0);
                r[94]=spu_sfi(r[55],0);r[91]=spu_or(r[94],r[54]);r[90]=spu_ceqbi(r[91],0);r[53]=spu_nor(r[90],r[90]);r[52]=spu_fsm(r[53]);
                selected=r[52]._u32[0]!=0;
            }
            if(selected){
                r[63]=spu_clgt(r[10],r[88]);r[59]=spu_il(1);r[58]=spu_ceqi(r[63],0);r[7]=spu_il(1);
                r[82]=spu_selb(r[87],r[82],r[58]);r[10]=spu_selb(r[84],r[10],r[58]);r[6]=spu_selb(r[59],r[7],r[58]);
            }else{
                r[64]=spu_xsbh(r[7]);
                if(r[64]._u16[0])r[16]=spu_il(1);
                else{
                    r[3]=spu_il(0);r[4]=spu_il(2400);
                    do{r[2]=rd(SPU_RdDec);r[3]=spu_ai(r[3],1);r[65]=spu_ceq(r[3],r[4]);}while(!r[65]._u32[0]);
                }
            }
            r[67]=spu_ceqbi(r[6],0);r[66]=spu_xsbh(r[67]);if(r[66]._u16[0])continue;
            r[71]=spu_il(12);r[74]=spu_cwd(r[92],12);r[17]=spu_sf(r[87],r[82]);r[70]=spu_cwd(r[1],0);r[11]=spu_il(0);
            r[73]=load(r[92]._u32[0]+r[71]._u32[0]);r[89]=spu_cwd(r[1],4);r[8]=spu_cwd(r[1],8);
            r[72]=spu_shufb(r[10],r[73],r[74]);store(r[92]._u32[0]+r[71]._u32[0],r[72]);r[69]=load(r[80]._u32[0]);
            r[87]=spu_shufb(r[82],r[69],r[70]);store(r[80]._u32[0],r[87]);r[68]=load(0xe600);r[16]=spu_rotqbyi(r[68],4);
            r[92]=spu_a(r[16],r[17]);r[2]=spu_shufb(r[92],r[87],r[89]);publish_lane(true);return;
        }
    }
    uint32_t shared_then_reconsider()
    {
        shared_reserve();r[3]=spu_il(4);r[14]=spu_ai(r[82],4);r[6]=load(r[80]._u32[0]);r[5]=spu_ceqi(r[9],0);
        r[12]=load(r[82]._u32[0]+r[3]._u32[0]);r[13]=load(r[82]._u32[0]);r[18]=spu_nor(r[5],r[5]);r[85]=spu_cwd(r[1],0);
        r[8]=spu_cwd(r[1],4);r[11]=spu_sfi(r[18],0);r[11]=spu_ceqbi(r[11],0);r[10]=spu_rotqby(r[12],r[14]);
        r[2]=spu_rotqby(r[13],r[82]);r[79]=spu_xsbh(r[11]);r[88]=spu_shufb(r[9],r[6],r[85]);r[4]=spu_sf(r[2],r[9]);
        r[84]=spu_a(r[10],r[4]);r[86]=spu_shufb(r[84],r[88],r[8]);store(r[80]._u32[0],r[86]);
        if(!r[79]._u16[0]){r[3]=spu_il(0);finish();return 0;}
        r[21]=load(0xe600);r[22]=load(0xe610);r[87]=r[21];r[88]=spu_rotqbyi(r[21],8);r[84]=spu_a(r[83],r[21]);
        r[81]=spu_rotqbyi(r[22],4);r[82]=spu_rotqbyi(r[21],12);r[20]=spu_sf(r[21],r[88]);r[19]=spu_clgt(r[94],r[20]);
        if(r[19]._u32[0]){publish_lane(false);return 0;}
        r[23]=load(0xe610);r[5]=spu_rotqbyi(r[23],8);
        if(r[5]._u32[0]){
            r[24]=spu_clgt(r[21],r[5]);if(r[24]._u32[0])return frontier(0x4078);
            r[25]=spu_clgt(r[5],r[88]);if(r[25]._u32[0])return frontier(0x4078);
        }
        reconsider_lane();return 0;
    }
public:
    explicit GeometryRingAllocation(spu_context& context):c(context){memcpy(r,c.gpr,sizeof r);}
    uint32_t run()
    {
        g_spu_trampoline_fn=nullptr;open();if(r[4]._u32[0]){direct();close();return 0;}
        r[39]=spu_ai(r[5],127);r[83]=spu_andi(r[39],-128);r[95]=rd(SPU_RdMachStat);r[2]=spu_ai(spu_link(0x3940),8);c.int_enable=0;
        r[96]=spu_ila(0xe600);r[37]=spu_shli(r[7],7);r[96]=spu_a(r[96],r[126]);r[38]=spu_ai(r[81],128);r[36]=r[96];r[93]=spu_a(r[37],r[38]);
        wr(MFC_LSA,36);wr(MFC_EAL,93);r[4]=spu_il(208);r[35]=r[4];wr(MFC_Cmd,35);r[3]=rd(MFC_RdAtomicStat);
        r[34]=load(0xe600);r[33]=load(0xe600);r[32]=spu_rotqbyi(r[34],8);r[31]=spu_ceq(r[32],r[33]);
        if(r[31]._u32[0]){shared_disabled();return 0;}
        r[94]=spu_a(r[83],r[83]);r[8]=load(0xe600);r[92]=spu_ila(0xe600);r[4]=load(0xe610);r[92]=spu_a(r[92],r[126]);
        r[87]=r[8];r[88]=spu_rotqbyi(r[8],8);r[91]=spu_a(r[83],r[8]);r[84]=spu_rotqbyi(r[4],4);r[86]=spu_rotqbyi(r[8],12);
        r[79]=spu_sf(r[8],r[88]);r[78]=spu_clgt(r[94],r[79]);if(r[78]._u32[0])return shared_then_reconsider();
        r[2]=load(0xe610);r[5]=spu_rotqbyi(r[2],8);
        if(r[5]._u32[0]){
            r[6]=spu_clgt(r[8],r[5]);if(r[6]._u32[0])return frontier(0x3dd0);
            r[11]=spu_clgt(r[5],r[88]);if(r[11]._u32[0])return frontier(0x3dd0);
        }
        if(!try_lane())return shared_then_reconsider();publish_lane(true);return 0;
    }
};
inline uint32_t allocate_geometry_ring(spu_context& c,const GeometryRing&,const GeometryRingContinuations& f)
{
    const uint32_t pc=GeometryRingAllocation(c).run();
    if(pc)g_spu_trampoline_fn=pc==0x3dd0?f.initial_report:f.shared_report;
    return pc;
}
}
