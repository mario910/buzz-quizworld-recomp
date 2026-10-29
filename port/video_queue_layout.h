#pragma once
/* BCES00647 1.02, serialized PPU producer -> image-7 SPU consumer. The caller
 * must serialize these raw guest-memory operations with SPU GETLLAR/PUTLLC.
 * Not an implementation of arbitrary CellSync/ANY2ANY queues. */
#include <stdint.h>
#include <string.h>
enum {BUZZ_VQ_OK,BUZZ_VQ_AGAIN,BUZZ_VQ_INVALID,BUZZ_VQ_UNSUPPORTED};
static inline uint16_t buzz_vq_r16(const uint8_t* p){return (uint16_t)((p[0]<<8)|p[1]);}
static inline uint32_t buzz_vq_r32(const uint8_t* p){return ((uint32_t)buzz_vq_r16(p)<<16)|buzz_vq_r16(p+2);}
static inline void buzz_vq_w16(uint8_t* p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static inline void buzz_vq_w32(uint8_t* p,uint32_t v){buzz_vq_w16(p,(uint16_t)(v>>16));buzz_vq_w16(p+2,(uint16_t)v);}
static inline int buzz_vq_range(uint64_t size,uint32_t ea,uint64_t bytes){return ea && bytes && (uint64_t)ea+bytes<=size;}
static inline int buzz_vq_overlap(uint32_t a,uint64_t an,uint32_t b,uint64_t bn){return (uint64_t)a<(uint64_t)b+bn && (uint64_t)b<(uint64_t)a+an;}
static inline int buzz_vq_init(uint8_t* vm,uint64_t size,uint32_t owner,uint32_t queue,uint32_t buffer,uint32_t depth) {
    uint64_t bytes=(uint64_t)depth*16;
    if(!vm || !depth || depth>32767 || (queue&127) || (buffer&15) || (owner&127) ||
       !buzz_vq_range(size,owner,6400) || !buzz_vq_range(size,queue,128) ||
       !buzz_vq_range(size,buffer,bytes) || buzz_vq_overlap(queue,128,buffer,bytes))return BUZZ_VQ_INVALID;
    uint8_t* q=vm+queue;memset(q,0,128);memset(vm+buffer,0,(size_t)bytes);
    buzz_vq_w32(q+0x10,16);buzz_vq_w32(q+0x14,depth);buzz_vq_w32(q+0x1c,buffer);
    buzz_vq_w32(q+0x20,0xffffffff);buzz_vq_w32(q+0x24,2);
    buzz_vq_w32(q+0x74,owner);return BUZZ_VQ_OK;
}
/* Commit one complete, in-order publication. wake_task is -1 unless an SPU
 * actually registered a waiter in the queue's 15-token notification ring. */
static inline int buzz_vq_push_locked(uint8_t* vm,uint64_t size,uint32_t queue,uint32_t data,int* wake_task) {
    *wake_task=-1;
    if(!vm || (queue&127) || (data&15) || !buzz_vq_range(size,queue,128) ||
       !buzz_vq_range(size,data,16) || buzz_vq_overlap(queue,128,data,16))return BUZZ_VQ_INVALID;
    uint8_t* q=vm+queue;
    uint32_t depth=buzz_vq_r32(q+0x14),buffer=buzz_vq_r32(q+0x1c);
    if(!depth || depth>32767 || buzz_vq_r32(q+0x10)!=16 || buzz_vq_r32(q+0x24)!=2 ||
       buzz_vq_r32(q+0x18) || !buzz_vq_range(size,buffer,(uint64_t)depth*16) ||
       buzz_vq_overlap(queue,128,buffer,(uint64_t)depth*16))return BUZZ_VQ_INVALID;
    unsigned modulus=depth*2,read=buzz_vq_r16(q),committed=buzz_vq_r16(q+8),reserved=buzz_vq_r16(q+14);
    if(read>=modulus || committed>=modulus || reserved>=modulus)return BUZZ_VQ_INVALID;
    /* Other producers / uncompleted publication masks require a different
     * completion algorithm; never trample their reserved pointers. */
    if(committed!=reserved || buzz_vq_r16(q+10) || buzz_vq_r16(q+12))return BUZZ_VQ_UNSUPPORTED;
    unsigned occupied=(reserved+modulus-read)%modulus;
    if(occupied>depth)return BUZZ_VQ_INVALID;
    if(occupied==depth)return BUZZ_VQ_AGAIN;
    uint16_t pack=buzz_vq_r16(q+0x30);
    unsigned head=(pack>>10)&31,ack=(pack>>5)&31,tail=pack&31;
    if(head>=30 || ack>=30 || tail>=30)return BUZZ_VQ_INVALID;
    unsigned pop_reserved=buzz_vq_r16(q+6),owed=(head+30-ack)%30;
    if(pop_reserved>=modulus)return BUZZ_VQ_INVALID;
    int task=-1;
    if(head!=tail && (pop_reserved+modulus-read)%modulus+owed<=15) {
        task=buzz_vq_r16(q+0x32+(head%15)*2);
        if(task>=128)return BUZZ_VQ_UNSUPPORTED;
        unsigned next=(head+1)%30;pack=(uint16_t)((pack&0x83ff)|(next<<10));
    }
    uint8_t payload[16];memcpy(payload,vm+data,16);
    unsigned slot=reserved>=depth?reserved-depth:reserved,next=(reserved+1)%modulus;
    memcpy(vm+buffer+slot*16,payload,16);
    buzz_vq_w16(q+14,(uint16_t)next);buzz_vq_w16(q+8,(uint16_t)next);
    if(task>=0)buzz_vq_w16(q+0x30,pack);
    *wake_task=task;return BUZZ_VQ_OK;
}
