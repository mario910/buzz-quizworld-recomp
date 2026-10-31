/* Host descriptor iteration, table routing, position padding and blend control.
 * Typed data handlers remain explicit callbacks with independently tracked coverage. */
#pragma once
#include "native_geometry_commands.h"
#include <array>

namespace buzz {
struct GeometryDescriptorStream {
    uint32_t context=0,stack=0,descriptor=0,vertices=0,bytes=0,count=0;
};
inline bool read_geometry_descriptor_stream(const spu_context& ctx,GeometryDescriptorStream& output)
{
    GeometryDescriptorStream p;
    p.context=ctx.gpr[3]._u32[0];p.stack=ctx.gpr[1]._u32[0];p.descriptor=ctx.gpr[6]._u32[0];p.vertices=ctx.gpr[7]._u32[0];
    if (ctx.ls!=ctx.ls_store || p.context<0xe680 || !geometry_output_range(p.context,0x190) ||
        !geometry_output_range(p.stack,32) || p.stack<0xed80 ||
        geometry_output_overlap(p.context,0x190,p.stack-0x700,0x720)) return false;
    if (!p.vertices) {output=p;return true;}
    if (p.vertices>8192 || p.descriptor<0xe680 || !geometry_output_range(p.descriptor,16)) return false;
    p.count=ctx.ls[p.descriptor];p.bytes=((p.vertices+3)&~3u)*16;
    if (p.count>16 || !ctx.ls[p.descriptor+1] || !geometry_output_range(p.descriptor,16+p.count*8) ||
        geometry_output_overlap(p.descriptor,16+p.count*8,p.context,0x190) ||
        geometry_output_overlap(p.descriptor,16+p.count*8,p.stack-0x700,0x720)) return false;
    const uint32_t types[]={0x294,0x478,0x24,0x65c,0x840,0x9e0,0xb54,0xd04,0xdc0};
    const uint32_t routing[]={0x180,0x194,0x1a8,0x144};
    for(unsigned i=0;i<9;++i) if(geometry_job_be32(ctx.ls+0x8354+i*4)!=types[i])return false;
    for(unsigned i=0;i<4;++i) if(geometry_job_be32(ctx.ls+0x842c+i*4)!=routing[i] ||
        geometry_job_be32(ctx.ls+0xcf50+i*4)!=i*16)return false;
    const bool blend=spu_fcgt(spu_splat_u32(ctx.gpr[8]._u32[0]),spu_il(0))._u32[0]!=0;
    if(blend && !(ctx.ls[p.context]&128))return false; // Original assertion.
    const uint32_t remap=ctx.gpr[9]._u32[0],remap_bytes=((p.vertices+3)&~3u)*2;
    const uint32_t destination_bytes=blend?((geometry_job_be16(ctx.ls+p.context+8)+7)&~7u)*16:p.bytes;
    if(blend){
        if(!destination_bytes || !geometry_output_range(remap,((remap_bytes+15)&~15u)+16) ||
            geometry_output_overlap(remap,remap_bytes,p.stack-0x700,0x720))return false;
        for(unsigned at=0;at<remap_bytes;at+=2){
            const unsigned offset=geometry_job_be16(ctx.ls+remap+at);
            if((offset&15) || offset+16>destination_bytes)return false;
        }
    }
    auto valid_buffer=[&](uint32_t buffer,uint32_t bytes){
        return buffer>=0xe680 && geometry_output_range(buffer,bytes+64) &&
            !geometry_output_overlap(buffer,bytes,p.context,0x190) &&
            !geometry_output_overlap(buffer,bytes,p.stack-0x700,0x720) &&
            !geometry_output_overlap(buffer,bytes,p.descriptor,16+p.count*8) &&
            (!blend || !geometry_output_overlap(buffer,bytes,remap,remap_bytes));
    };
    std::array<uint8_t,16> slots;memcpy(slots.data(),ctx.ls+p.context+0x160,16);
    if(blend && !valid_buffer(geometry_job_be32(ctx.ls+p.context+0x10+(ctx.ls[p.context]&15)*4),p.bytes))return false;
    for(unsigned i=0;i<p.count;++i){
        const uint8_t* a=ctx.ls+p.descriptor+8+i*8;
        if(a[1]<1 || a[1]>9 || a[2]>4 || !a[4] || a[0]+a[4]>ctx.ls[p.descriptor+1])return false;
        if((ctx.ls[p.context]&16) && a[3]!=1)continue;
        unsigned slot=0;
        for(;slot<16 && slots[slot]!=(blend?a[3]:0);++slot){}
        if(slot==16){if(blend)continue;return false;}
        if(!valid_buffer(geometry_job_be32(ctx.ls+p.context+0x10+slot*4),destination_bytes))return false;
        if(!blend)slots[slot]=a[3];
    }
    output=p;return true;
}

inline void geometry_descriptor_codec(spu_context& ctx,buzz_geometry_job_fn fn,buzz_geometry_job_fn continuation)
{
    fn(&ctx);
    unsigned steps=0;
    while(ctx.pc!=0x83c8){
        if(!g_spu_trampoline_fn || ++steps>65536)buzz_geometry_job_native_fail(&ctx,"descriptor codec escaped region");
        auto next=g_spu_trampoline_fn;g_spu_trampoline_fn=nullptr;next(&ctx);
    }
    if(g_spu_trampoline_fn!=continuation)buzz_geometry_job_native_fail(&ctx,"descriptor codec continuation differs");
    g_spu_trampoline_fn=nullptr;
}

inline void geometry_descriptor_position_padding(spu_context& ctx)
{
    auto& r=ctx.gpr;
    r[23]=spu_il(15);r[32]=spu_sfi(r[20],0);r[42]=geometry_output_quad(ctx,0xcd60);r[2]=spu_sfi(r[119],0);
    r[25]=spu_fsmbi(0);r[47]=spu_ceqi(r[113],0);r[15]=r[114];r[14]=spu_ai(r[114],16);
    r[19]=spu_ls_read128(&ctx,r[114]._u32[0]+r[2]._u32[0]);r[13]=spu_ai(r[114],32);
    r[41]=spu_fsm(r[47]);r[12]=spu_ai(r[114],48);
    r[18]=spu_ls_read128(&ctx,r[14]._u32[0]+r[2]._u32[0]);r[43]=spu_rotma(r[23],r[32]);
    r[17]=spu_ls_read128(&ctx,r[13]._u32[0]+r[2]._u32[0]);r[11]=r[2];
    r[16]=spu_ls_read128(&ctx,r[12]._u32[0]+r[2]._u32[0]);r[9]=r[2];
    r[7]=spu_selb(r[25],r[42],r[41]);r[10]=spu_ai(r[2],64);r[8]=spu_fsm(r[43]);
    const uint32_t bytes=r[119]._u32[0],start=r[114]._u32[0]-bytes;
    // Native data loop owns all quads. Read the actual trailing padding for
    // final ABI; it is never assumed to contain zeros.
    for(uint32_t at=0;at<bytes;at+=16){
        const u128 value=spu_selb(geometry_output_quad(ctx,start+at),r[7],r[8]);
        spu_ls_write128(&ctx,start+at,value);
    }
    const uint32_t last=start+bytes-64;
    // Last current values have already been selected; selection is idempotent.
    r[50]=geometry_output_quad(ctx,last);r[4]=geometry_output_quad(ctx,last+16);
    r[49]=geometry_output_quad(ctx,last+32);r[22]=geometry_output_quad(ctx,last+48);
    r[19]=geometry_output_quad(ctx,start+bytes);r[18]=geometry_output_quad(ctx,start+bytes+16);
    r[17]=geometry_output_quad(ctx,start+bytes+32);r[16]=geometry_output_quad(ctx,start+bytes+48);
    r[48]=spu_ai(r[11],bytes);r[21]=spu_ai(r[10],bytes);r[11]=r[48];r[10]=r[21];r[9]=r[48];
}

inline void decode_geometry_descriptor_stream(spu_context& ctx,const GeometryDescriptorStream& p,
                                              const buzz_geometry_descriptor_functions& functions)
{
    auto& r=ctx.gpr;
    r[2]=spu_il(-1056);spu_ls_write128(&ctx,p.stack-0x2f0,r[126]);r[10]=spu_ila(0x7e08);r[126]=spu_link(0x7e08);
    r[15]=r[4];
    for(unsigned reg=80;reg<=125;++reg)spu_ls_write128(&ctx,p.stack-(reg-79)*16,r[reg]);
    r[95]=r[3];r[115]=r[6];spu_ls_write128(&ctx,p.stack+16,r[0]);r[126]=spu_sf(r[10],r[126]);r[10]=r[7];
    spu_ls_write128(&ctx,p.stack-0x300,r[127]);spu_ls_write128(&ctx,p.stack-0x420,r[1]);r[1]=spu_a(r[1],r[2]);
    spu_ls_write128(&ctx,r[1]._u32[0]+0x100,r[9]);
    if(p.vertices){
        r[11]=spu_ila(0x10203);r[127]=spu_ilhu(0x8080);r[96]=spu_fsmbi(0xfff0);r[94]=spu_il(0);
        r[9]=spu_shufb(r[8],r[8],r[11]);r[6]=spu_ila(0xddf0);spu_ls_write128(&ctx,r[1]._u32[0]+0xe0,r[127]);
        r[125]=r[127];r[7]=geometry_output_quad(ctx,r[1]._u32[0]+0xe0);r[93]=spu_ilhu(0x3800);
        r[100]=geometry_output_quad(ctx,0xd190);r[92]=spu_ilhu(0x3780);spu_ls_write128(&ctx,r[1]._u32[0]+0xb0,r[9]);
        r[91]=spu_ilhu(0x3b80);r[4]=geometry_output_quad(ctx,r[1]._u32[0]+0xb0);r[102]=spu_ilhu(0x4635);
        r[90]=geometry_output_quad(ctx,0xd1a0);r[101]=spu_ilhu(0xc635);r[99]=geometry_output_quad(ctx,0xd1b0);
        r[7]=spu_iohl(r[7],0x8006);r[98]=geometry_output_quad(ctx,0xd1c0);r[127]=spu_iohl(r[127],0x8001);
        r[125]=spu_iohl(r[125],0x8004);r[87]=geometry_output_quad(ctx,0xd1d0);r[3]=spu_fcgt(r[4],r[94]);
        r[86]=geometry_output_quad(ctx,0xd1e0);r[93]=spu_ori(r[93],128);r[85]=geometry_output_quad(ctx,0xd1f0);
        r[92]=spu_ori(r[92],128);r[89]=geometry_output_quad(ctx,0xd200);r[91]=spu_iohl(r[91],0x8080);
        r[88]=geometry_output_quad(ctx,0xd210);r[102]=spu_iohl(r[102],0x2e7c);r[97]=geometry_output_quad(ctx,0xd220);
        r[101]=spu_iohl(r[101],0x3150);spu_ls_write128(&ctx,r[1]._u32[0]+0xd0,r[5]);r[5]=spu_a(r[6],r[126]);
        spu_ls_write128(&ctx,r[1]._u32[0]+0xe0,r[7]);r[113]=r[3];r[120]=spu_ila(0x10203);
        spu_ls_write128(&ctx,r[1]._u32[0]+0xa0,r[3]);r[108]=spu_ilh(0x303);
        spu_ls_write128(&ctx,r[1]._u32[0]+0xc0,r[5]);r[110]=spu_ilhu(0x3f80);r[96]=spu_andbi(r[96],8);
        if(r[3]._u32[0]){
            r[13]=geometry_output_quad(ctx,p.context);r[11]=spu_rotqbyi(r[13],13);r[12]=spu_cgtbi(r[11],-1);r[8]=spu_xsbh(r[12]);
        }else{r[82]=geometry_output_quad(ctx,p.context);r[11]=spu_rotqbyi(r[82],13);}
        r[44]=spu_andi(r[11],15);r[3]=spu_ai(r[10],3);r[17]=geometry_output_quad(ctx,p.descriptor);
        r[81]=spu_shli(r[44],2);r[83]=spu_ila(0x10203);r[117]=spu_andi(r[3],-4);r[45]=spu_ai(r[1],32);
        r[124]=spu_shufb(r[10],r[10],r[83]);r[112]=spu_a(r[81],r[95]);r[118]=spu_shufb(r[117],r[117],r[83]);
        r[9]=spu_il(4);r[116]=spu_rotqbyi(r[17],14);r[8]=spu_il(1024);
        r[111]=spu_ls_read128(&ctx,r[112]._u32[0]+16);r[7]=spu_il(6);r[82]=spu_shufb(r[45],r[120],r[120]);
        r[119]=spu_shli(r[118],4);spu_ls_write128(&ctx,r[1]._u32[0]+0x80,r[124]);
        r[121]=spu_andi(r[116],255);r[104]=spu_andi(r[121],1);r[106]=spu_shufb(r[121],r[121],r[83]);
        r[6]=r[121];r[109]=spu_rotqby(r[111],r[112]);r[121]=spu_il(16);
        r[107]=spu_shufb(r[109],r[120],r[120]);spu_ls_write128(&ctx,r[1]._u32[0]+0x90,r[107]);r[107]=spu_il(16);
        if(!r[104]._u32[0]){
            r[4]=spu_il(0);
            do{
                r[6]=spu_rotmai(r[6],-1);r[4]=spu_ai(r[4],1);r[22]=spu_cgti(r[4],3);r[21]=spu_xori(r[6],1);
                r[20]=spu_nor(r[22],r[22]);r[19]=spu_and(r[20],r[21]);r[18]=spu_andi(r[19],1);
                r[17]=spu_ceqbi(r[18],0);r[16]=spu_xsbh(r[17]);
            }while(!r[16]._u16[0]);
            r[26]=spu_il(64);r[27]=spu_sfi(r[4],0);r[25]=spu_il(16);r[23]=spu_rotma(r[26],r[27]);
            r[24]=spu_sfi(r[4],0);r[9]=spu_sfi(r[4],4);r[121]=spu_rotma(r[25],r[24]);
            r[8]=spu_shli(r[23],4);r[7]=spu_sfi(r[4],6);r[107]=r[121];
        }
        r[40]=spu_ila(0x10203);r[118]=r[15];r[38]=geometry_output_quad(ctx,p.descriptor);r[41]=spu_ai(r[115],8);
        r[37]=spu_shufb(r[9],r[9],r[40]);r[42]=spu_andi(r[11],16);r[36]=geometry_output_quad(ctx,0xcf50);
        r[39]=spu_ai(r[118],16);r[81]=spu_shufb(r[8],r[8],r[40]);r[104]=r[41];r[33]=spu_shufb(r[7],r[7],r[40]);
        r[34]=spu_shl(r[106],r[37]);spu_ls_write128(&ctx,r[1]._u32[0]+0xf0,r[42]);r[8]=spu_rotqbyi(r[38],13);
        spu_ls_write128(&ctx,r[1]._u32[0]+0x110,r[41]);r[32]=spu_shl(r[36],r[37]);
        r[117]=spu_a(r[118],r[34]);r[35]=spu_ceqbi(r[8],0);r[116]=spu_a(r[117],r[34]);
        r[31]=spu_ai(r[32],1);r[124]=spu_a(r[116],r[34]);r[30]=spu_ai(r[117],16);
        spu_ls_write128(&ctx,r[1]._u32[0]+0x30,r[31]);r[29]=spu_ai(r[116],16);
        spu_ls_write128(&ctx,r[1]._u32[0]+0x60,r[30]);r[28]=spu_ai(r[124],16);
        spu_ls_write128(&ctx,r[1]._u32[0]+0x50,r[29]);r[15]=spu_xsbh(r[35]);spu_ls_write128(&ctx,r[1]._u32[0]+0x40,r[28]);
        r[83]=spu_shl(r[106],r[33]);spu_ls_write128(&ctx,r[1]._u32[0]+0x70,r[39]);
        if(!r[15]._u16[0]){
            r[111]=spu_il(0);r[112]=geometry_output_quad(ctx,0xd980);r[109]=geometry_output_quad(ctx,0xda20);
            for(unsigned attribute=0;attribute<p.count;++attribute){
                r[45]=r[104];r[44]=spu_ls_read128(&ctx,r[104]._u32[0]);r[46]=spu_ai(r[104],8);
                r[43]=geometry_output_quad(ctx,r[1]._u32[0]+0xf0);r[104]=r[46];r[84]=spu_rotqby(r[44],r[45]);r[105]=spu_andi(r[84],255);
                bool active=true;
                if(r[43]._u32[0]){r[47]=spu_ceqi(r[105],1);active=r[47]._u32[0]!=0;}
                if(active){
                    r[69]=spu_il(0);r[67]=geometry_output_quad(ctx,p.context+0x160);r[57]=spu_ceqi(r[113],0);
                    r[66]=spu_shufb(r[84],r[108],r[108]);r[42]=spu_rotmi(r[84],-24);r[68]=spu_shufb(r[69],r[108],r[108]);
                    r[51]=geometry_output_quad(ctx,r[1]._u32[0]+0x90);r[50]=geometry_output_quad(ctx,r[1]._u32[0]+0xa0);
                    r[49]=geometry_output_quad(ctx,r[1]._u32[0]+0x30);r[65]=spu_ceqb(r[68],r[67]);
                    r[9]=spu_shufb(r[84],r[84],r[127]);r[64]=spu_ceqb(r[66],r[67]);r[25]=spu_shufb(r[84],r[84],r[125]);
                    r[63]=spu_gbb(r[65]);r[62]=spu_gbb(r[64]);r[61]=spu_clz(r[63]);r[60]=spu_clz(r[62]);
                    r[123]=spu_ai(r[61],-16);r[122]=spu_ai(r[60],-16);r[59]=spu_shli(r[123],2);r[58]=spu_ceqi(r[122],16);
                    r[56]=spu_nor(r[58],r[58]);r[55]=spu_a(r[59],r[95]);r[48]=spu_or(r[57],r[56]);
                    r[54]=spu_ls_read128(&ctx,r[55]._u32[0]+16);r[53]=spu_rotqby(r[54],r[55]);
                    r[52]=spu_shufb(r[53],r[120],r[120]);r[103]=spu_selb(r[52],r[51],r[50]);r[114]=spu_a(r[103],r[119]);
                    r[41]=spu_a(r[49],r[103]);r[80]=r[114];
                    if(r[48]._u32[0]){
                        r[38]=spu_ai(r[9],-1);r[39]=spu_ila(0x8354);r[20]=spu_shli(r[38],2);r[35]=spu_a(r[39],r[126]);
                        r[33]=spu_clgti(r[38],8);r[37]=spu_a(r[20],r[35]);r[36]=spu_ls_read128(&ctx,r[35]._u32[0]+r[20]._u32[0]);
                        r[34]=spu_rotqby(r[36],r[37]);r[2]=spu_a(r[34],r[35]);
                        const unsigned storage=r[9]._u32[0];
                        if(functions.dispatched)functions.dispatched(storage);
                        geometry_descriptor_codec(ctx,functions.codec[storage-1],functions.continuation);
                        r[38]=spu_ilhu(0x8080);r[39]=spu_ceqi(r[105],1);r[38]=spu_iohl(r[38],0x8002);
                        r[34]=spu_sfi(r[39],0);r[20]=spu_shufb(r[84],r[84],r[38]);r[37]=spu_cgti(r[20],3);
                        r[36]=spu_nor(r[37],r[37]);r[35]=spu_sfi(r[36],0);r[33]=spu_and(r[34],r[35]);r[45]=spu_ceqbi(r[33],0);r[44]=spu_xsbh(r[45]);
                        if(!r[44]._u16[0])geometry_descriptor_position_padding(ctx);
                        if(r[113]._u32[0]){
                            r[56]=spu_ai(r[80],-1);r[62]=geometry_output_quad(ctx,r[1]._u32[0]+0x80);r[53]=spu_shli(r[122],2);
                            r[123]=spu_il(0);r[3]=geometry_output_quad(ctx,r[1]._u32[0]+0x100);r[11]=spu_sfi(r[20],4);r[5]=r[114];r[54]=spu_il(15);
                            r[105]=spu_a(r[53],r[95]);r[61]=spu_shli(r[62],4);r[84]=spu_shl(r[54],r[11]);r[7]=spu_sfi(r[119],0);
                            r[59]=spu_a(r[61],r[103]);r[6]=spu_fsm(r[84]);r[16]=spu_ai(r[59],16);r[51]=spu_ai(r[59],32);
                            r[60]=spu_cgt(r[59],r[56]);r[15]=spu_cgt(r[16],r[56]);r[58]=spu_selb(r[59],r[82],r[60]);
                            r[52]=spu_cgt(r[51],r[56]);r[55]=spu_selb(r[16],r[82],r[15]);spu_ls_write128(&ctx,r[58]._u32[0],r[123]);
                            r[122]=spu_selb(r[51],r[82],r[52]);spu_ls_write128(&ctx,r[55]._u32[0],r[123]);spu_ls_write128(&ctx,r[122]._u32[0],r[123]);
                            r[103]=spu_ls_read128(&ctx,r[105]._u32[0]+16);r[8]=geometry_output_quad(ctx,r[1]._u32[0]+0xb0);r[4]=spu_rotqby(r[103],r[105]);
                            geometry_command_call(ctx,functions.blend,0x8564);r[80]=geometry_output_quad(ctx,p.descriptor);r[8]=spu_rotqbyi(r[80],13);
                        }else{
                            r[68]=spu_ai(r[105],-1);r[24]=spu_ila(0x842c);r[67]=spu_shli(r[68],2);r[64]=spu_a(r[24],r[126]);
                            r[114]=spu_clgti(r[68],3);r[66]=spu_a(r[67],r[64]);r[65]=spu_ls_read128(&ctx,r[64]._u32[0]+r[67]._u32[0]);
                            r[63]=spu_rotqby(r[65],r[66]);r[2]=spu_a(r[63],r[64]);
                            switch(r[105]._u32[0]){
                            case 1:r[26]=geometry_output_quad(ctx,p.context+0x50);r[70]=spu_cwd(r[1],12);r[57]=spu_shufb(r[103],r[26],r[70]);spu_ls_write128(&ctx,p.context+0x50,r[57]);break;
                            case 2:r[72]=geometry_output_quad(ctx,p.context+0x60);r[74]=spu_cwd(r[1],0);r[71]=spu_shufb(r[103],r[72],r[74]);spu_ls_write128(&ctx,p.context+0x60,r[71]);break;
                            case 3:r[76]=geometry_output_quad(ctx,p.context+0x60);r[78]=spu_cwd(r[1],4);r[75]=spu_shufb(r[103],r[76],r[78]);spu_ls_write128(&ctx,p.context+0x60,r[75]);break;
                            case 4:r[27]=geometry_output_quad(ctx,p.context+0x60);r[9]=spu_cwd(r[1],8);r[79]=spu_shufb(r[103],r[27],r[9]);spu_ls_write128(&ctx,p.context+0x60,r[79]);break;
                            }
                            r[30]=geometry_output_quad(ctx,p.context+0x160);r[19]=spu_cbd(r[123],0);r[18]=spu_shufb(r[84],r[30],r[19]);
                            spu_ls_write128(&ctx,p.context+0x160,r[18]);r[29]=geometry_output_quad(ctx,p.descriptor);r[8]=spu_rotqbyi(r[29],13);
                        }
                    }
                }
                r[111]=spu_ai(r[111],1);r[5]=spu_andi(r[8],255);r[7]=spu_clgt(r[5],r[111]);
            }
        }
    }
    r[85]=spu_il(1056);r[1]=spu_a(r[1],r[85]);r[0]=spu_ls_read128(&ctx,p.stack+16);
    for(unsigned reg=80;reg<=125;++reg)r[reg]=spu_ls_read128(&ctx,p.stack-(reg-79)*16);
    r[126]=spu_ls_read128(&ctx,p.stack-0x2f0);r[127]=spu_ls_read128(&ctx,p.stack-0x300);
}
} // namespace buzz
