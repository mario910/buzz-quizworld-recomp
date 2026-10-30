/* Replay-only image-41 instrumentation. Included by the pinned trace generator.
 * Never use this translation unit for performance measurements. */
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char* geometry_access[SPU_LS_SIZE / 4];
static unsigned long long geometry_entries[SPU_LS_SIZE / 4];
static unsigned geometry_pcs[BUZZ_GEOMETRY_BLOCKS];
static unsigned long long geometry_edges[BUZZ_GEOMETRY_BLOCKS][BUZZ_GEOMETRY_BLOCKS];
static unsigned geometry_previous = BUZZ_GEOMETRY_BLOCKS;
static FILE* geometry_dma;
static FILE* geometry_commands;
static const char* geometry_job = "";
static unsigned char geometry_snapshot_state[BUZZ_GEOMETRY_BLOCKS];

static void geometry_snapshot(spu_context* ctx, unsigned pc, unsigned id, int exiting)
{
    const char* directory = getenv("BUZZ_GEOMETRY_SNAPSHOTS");
    const char* selected = getenv("BUZZ_GEOMETRY_SNAPSHOT_PCS");
    if (!directory || !selected) return;
    if (exiting) {
        if (geometry_snapshot_state[id] != 1) return;
    } else {
        if (geometry_snapshot_state[id]) return;
        int found = 0;
        for (const char* e = selected; *e;) {
            char* end; unsigned requested = (unsigned)strtoul(e, &end, 0);
            if (end == e || (*end && *end != ',')) abort();
            if (requested == pc) { found = 1; break; }
            e = *end ? end + 1 : end;
        }
        if (!found) return;
    }
    char path[2048]; snprintf(path, sizeof path, "%s/pc_%04X%s.bin", directory, pc, exiting ? "_exit" : "");
    FILE* f = fopen(path, "wb");
    if (!f) abort();
    if (fwrite(ctx->gpr, sizeof(ctx->gpr), 1, f) != 1 || fwrite(ctx->ls, SPU_LS_SIZE, 1, f) != 1) abort();
    fclose(f);
    char meta[2064]; snprintf(meta, sizeof meta, "%s.json", path);
    f = fopen(meta, "wb");
    if (!f) abort();
    fprintf(f, "{\"job\":\"%s\",\"block\":%u,\"exit\":%s,\"pc\":%u,\"status\":%u,\"stop\":%u}\n",
            geometry_job, pc, exiting ? "true" : "false", ctx->pc, ctx->status, ctx->stop_code);
    fclose(f);
    geometry_snapshot_state[id] = exiting ? 2 : 1;
}

void buzz_geometry_trace_job(const char* path)
{
    const char* slash = strrchr(path, '/');
    const char* backslash = strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
    geometry_job = slash ? slash + 1 : path;
    geometry_previous = BUZZ_GEOMETRY_BLOCKS;
    const char* prefix = getenv("BUZZ_GEOMETRY_TRACE");
    if (!geometry_dma && prefix) {
        char name[2048]; snprintf(name, sizeof name, "%s.dma.csv", prefix);
        geometry_dma = fopen(name, "wb");
        if (!geometry_dma) { fprintf(stderr, "[geometry-trace] cannot open %s\n", name); abort(); }
        fprintf(geometry_dma, "job,pc,cmd,lsa,ea,size,tag\n");
        snprintf(name, sizeof name, "%s.commands.csv", prefix);
        geometry_commands = fopen(name, "wb");
        if (!geometry_commands) abort();
        fprintf(geometry_commands, "job,pc,cmd,lsa,ea,size,tag\n");
    }
}

static void geometry_enter(spu_context* ctx, unsigned pc, unsigned id)
{
    geometry_entries[pc / 4]++;
    geometry_pcs[id] = pc;
    if (geometry_previous < BUZZ_GEOMETRY_BLOCKS) geometry_edges[geometry_previous][id]++;
    geometry_previous = id;
    geometry_snapshot(ctx, pc, id, 0);
}

static void geometry_leave(spu_context* ctx, unsigned pc, unsigned id)
{
    geometry_snapshot(ctx, pc, id, 1);
}

static void geometry_touch(unsigned pc, unsigned address, unsigned flag)
{
    unsigned char** lines = &geometry_access[(pc & SPU_LS_MASK) / 4];
    if (!*lines) {
        *lines = (unsigned char*)calloc(SPU_LS_SIZE / 16, 1);
        if (!*lines) abort();
    }
    (*lines)[(address & 0x3FFF0u) / 16] |= (unsigned char)flag;
}

static u128 geometry_read128(spu_context* ctx, unsigned pc, unsigned address)
{
    geometry_touch(pc, address, 1);
    return spu_ls_read128(ctx, address);
}
static void geometry_write128(spu_context* ctx, unsigned pc, unsigned address, u128 value)
{
    geometry_touch(pc, address, 2);
    spu_ls_write128(ctx, address, value);
}
static void geometry_wrch(spu_context* ctx, unsigned pc, unsigned channel, u128 value)
{
    if (geometry_dma && channel == MFC_Cmd) {
        unsigned cmd = value._u32[0] & 0xff;
        unsigned atomic = cmd == MFC_GETLLAR_CMD || cmd == MFC_PUTLLC_CMD || cmd == MFC_PUTLLUC_CMD || cmd == MFC_PUTQLLUC_CMD;
        fprintf(geometry_commands, "%s,%X,%X,%X,%X,%u,%u\n", geometry_job, pc, cmd,
                ctx->mfc_lsa, ctx->mfc_eal, atomic ? 128 : ctx->mfc_size, ctx->mfc_tag);
        if (cmd >= 0x20 && cmd <= 0x46 && (cmd & 4)) {
            unsigned dest = ctx->mfc_lsa;
            for (unsigned i = 0; i < ctx->mfc_size / 8; ++i) {
                unsigned el = (ctx->mfc_eal + i * 8) & SPU_LS_MASK;
                geometry_touch(pc, el, 1);
                unsigned flags = spu_ls_read32(ctx, el);
                unsigned ea = spu_ls_read32(ctx, el + 4);
                unsigned size = flags & 0x7fff;
                if (flags & 0x80000000u) {
                    fprintf(stderr, "[geometry-trace] stalled DMA list needs explicit resume tracing\n"); abort();
                }
                if (!size) continue;
                unsigned lsa = (dest & ~15u) | (ea & 15u);
                fprintf(geometry_dma, "%s,%X,%X,%X,%X,%u,%u\n", geometry_job, pc, cmd & ~4u,
                        lsa, ea, size, ctx->mfc_tag);
                dest = (dest & ~15u) + ((size + 15u) & ~15u);
            }
        } else if (atomic || ctx->mfc_size) {
            fprintf(geometry_dma, "%s,%X,%X,%X,%X,%u,%u\n", geometry_job, pc, cmd,
                    ctx->mfc_lsa, ctx->mfc_eal, atomic ? 128 : ctx->mfc_size, ctx->mfc_tag);
        }
    }
    spu_wrch(ctx, channel, value);
}

void buzz_geometry_trace_report(void)
{
    const char* prefix = getenv("BUZZ_GEOMETRY_TRACE");
    if (!prefix) return;
    char name[2048]; snprintf(name, sizeof name, "%s.ls.csv", prefix);
    FILE* f = fopen(name, "wb");
    if (!f) { fprintf(stderr, "[geometry-trace] cannot open %s\n", name); abort(); }
    fprintf(f, "pc,begin,end,access\n");
    for (unsigned pc = 0; pc < SPU_LS_SIZE; pc += 4) {
        unsigned char* lines = geometry_access[pc / 4];
        if (!lines) continue;
        for (unsigned i = 0; i < SPU_LS_SIZE / 16;) {
            unsigned begin = i, flag = lines[i++];
            while (i < SPU_LS_SIZE / 16 && lines[i] == flag) ++i;
            if (flag) fprintf(f, "%X,%X,%X,%u\n", pc, begin * 16, i * 16, flag);
        }
    }
    fclose(f);
    snprintf(name, sizeof name, "%s.edges.csv", prefix);
    f = fopen(name, "wb");
    if (!f) abort();
    fprintf(f, "source,target,count\n");
    for (unsigned i = 0; i < BUZZ_GEOMETRY_BLOCKS; ++i)
        for (unsigned j = 0; j < BUZZ_GEOMETRY_BLOCKS; ++j)
            if (geometry_edges[i][j]) fprintf(f, "%X,%X,%llu\n", geometry_pcs[i], geometry_pcs[j], geometry_edges[i][j]);
    fclose(f);
    snprintf(name, sizeof name, "%s.entries.csv", prefix);
    f = fopen(name, "wb");
    if (!f) abort();
    fprintf(f, "pc,entries\n");
    for (unsigned pc = 0; pc < SPU_LS_SIZE; pc += 4)
        if (geometry_entries[pc / 4]) fprintf(f, "%X,%llu\n", pc, geometry_entries[pc / 4]);
    fclose(f);
    if (geometry_dma) { fclose(geometry_dma); geometry_dma = NULL; }
    if (geometry_commands) { fclose(geometry_commands); geometry_commands = NULL; }
}
