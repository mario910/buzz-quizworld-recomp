// BCES00647 1.02 EdgeZlibInflateQueue HLE.
// The 32-byte queue item layout is confirmed by the game's stores at
// 0x3332f0..0x333328. This replaces real decompression work, not its wait.
#include "ppu_recomp.h"
#include "zlib.h"
#include <cstdio>
#include <cstring>
#include <mutex>
#include <atomic>

extern "C" unsigned char* vm_base;
extern "C" int32_t cellSpursEventFlagSet(void*, uint16_t);
static std::atomic<unsigned> completedJobs{0};
static std::atomic<unsigned long long> expandedBytes{0};

extern "C" void buzz_edge_stats(unsigned* jobs, unsigned long long* bytes)
{
    *jobs = completedJobs.load();
    *bytes = expandedBytes.load();
}

bool buzz_edge_inflate(ppu_context* ctx)
{
    if ((uint32_t)ctx->lr != 0x00333340u) return false;
    const uint32_t item = (uint32_t)ctx->gpr[4];
    const uint32_t input = vm_read32(item);
    const uint32_t output = vm_read32(item + 4);
    const uint32_t inputSize = vm_read32(item + 8);
    const uint32_t outputSize = vm_read32(item + 12);
    const uint32_t counterAndMode = vm_read32(item + 16);
    const uint32_t counter = counterAndMode & ~3u;
    const uint32_t mode = counterAndMode & 3u;
    const uint32_t event = vm_read32(item + 20);
    const uint16_t eventBits = vm_read16(item + 24);
    if (!input || !output || !inputSize || !outputSize ||
        inputSize > 0x10000u || outputSize > 0x10000u || mode > 1u)
        return false;

    int result = Z_OK;
    unsigned long produced = outputSize;
    if (mode == 0) {
        if (inputSize != outputSize) return false;
        std::memmove(vm_base + output, vm_base + input, inputSize);
    } else {
        z_stream stream{};
        stream.next_in = vm_base + input;
        stream.avail_in = inputSize;
        stream.next_out = vm_base + output;
        stream.avail_out = outputSize;
        result = inflateInit2(&stream, -MAX_WBITS);
        if (result == Z_OK) {
            result = inflate(&stream, Z_FINISH);
            produced = stream.total_out;
            inflateEnd(&stream);
        }
        if (result != Z_STREAM_END || produced != outputSize) {
            fprintf(stderr, "[edge-hle] inflate failed rc=%d in=%u out=%lu/%u\n",
                    result, inputSize, produced, outputSize);
            return false;
        }
    }

    static std::mutex completionMutex;
    uint32_t remaining = 0;
    {
        const std::lock_guard<std::mutex> lock(completionMutex);
        if (counter) {
            const uint32_t before = vm_read32(counter);
            if (!before) {
                fprintf(stderr, "[edge-hle] invalid zero completion counter at %08X\n", counter);
                return false;
            }
            remaining = before - 1;
            vm_write32(counter, remaining);
        }
        if (!remaining && event && eventBits)
            cellSpursEventFlagSet(reinterpret_cast<void*>((uintptr_t)event), eventBits);
    }
    const unsigned jobs = completedJobs.fetch_add(1) + 1;
    expandedBytes.fetch_add(produced);
    if (jobs <= 30 || (jobs % 100) == 0)
        fprintf(stderr, "[edge-hle] job=%u input=%08X/%u output=%08X/%lu counter=%08X remaining=%u event=%08X/%04X\n",
                jobs, input, inputSize, output, produced, counter, remaining, event, eventBits);
    ctx->gpr[3] = 0;
    return true;
}
