/* Edge triangle indices decoded directly into host integer SoA. Explicit
 * deltas and flag-selected references are consumed on demand; there is no
 * unpack/expand/relocation scratch, SPU register bank or dummy iteration. */
#pragma once
#include "native_geometry_job_input.h"
#ifndef BUZZ_GEOMETRY_INDEX_SEQUENTIAL
#define BUZZ_GEOMETRY_INDEX_SEQUENTIAL 0
#endif
#ifndef BUZZ_GEOMETRY_INDEX_BATCHED
#define BUZZ_GEOMETRY_INDEX_BATCHED 0
#endif
#if BUZZ_GEOMETRY_INDEX_BATCHED
#include "native_geometry_index_delta.h"
#endif

namespace buzz {
struct GeometryHostIndexData {
    static constexpr unsigned capacity = (65535 / 3 + 7) & ~7u;
    alignas(32) uint16_t component[3][capacity];
    unsigned triangles = 0, maximum_index = 0;
    // Retained triangles in the last source group of four. Five means
    // decoded but not culled; the final byte serializer requires culling.
    unsigned final_group_kept = 5;
    bool ready = false;
};
inline bool geometry_host_index_codec_matches(const spu_context& c)
{
    struct Quad { unsigned at; uint32_t word[4]; };
    static constexpr Quad tables[] = {
        {0xd110,{0x00010002,0x00030004,0x00050006,0x00070008}},
        {0xd120,{0,0,0xffffffc0,0xffffffc0}},
        {0xd130,{0x00000001,0x00020003,0x00040005,0x00060007}},
        {0xd140,{0x00008080,0xc0c0e0e0,0xf0f0f8f8,0xfcfcfefe}},
        {0xd150,{0x00000101,0x02020303,0x04040505,0x06060707}},
        {0xd160,{0x00010203,0x04050607,0x08090a0b,0x10111213}},
        {0xd170,{0x04050607,0x08090a0b,0x10111213,0x14151617}},
        {0xd180,{0x08090a0b,0x10111213,0x14151617,0x18191a1b}},
        {0xd8a0,{0x80808002,0x80808080,0x80808080,0x80808080}},
        {0xd8e0,{0x80800001,0x80800405,0x80800809,0x80800c0d}},
        {0xd8f0,{0x80800203,0x80800607,0x80800a0b,0x80800e0f}},
        {0xd950,{0x00000202,0x04040606,0x08080a0a,0x0c0c0e0e}},
        {0xd980,{0x00010203,0x04050607,0x08090a0b,0x0c0d0e0f}},
        {0xd9d0,{0x00080109,0x020a030b,0x040c050d,0x060e070f}},
        {0xd9e0,{0x01010101,0x05050505,0x09090909,0x0d0d0d0d}},
        {0xda30,{0x04050607,0x08090a0b,0x0c0d0e0f,0x0c0d0e0f}}};
    for (const auto& table : tables) for (unsigned lane = 0; lane < 4; ++lane)
        if (geometry_job_be32(c.ls + table.at + lane * 4) != table.word[lane]) return false;
    // The original two-triangle shuffle tables implement precisely these
    // four edge rules. Validate that definition, without using SPU shuffles
    // or copying their data-flow implementation into the host decoder.
    static constexpr auto edge_tables = [] {
        std::array<uint8_t, 512> expected{};
        for (unsigned pair = 0; pair < 16; ++pair) {
            unsigned a = 3, b = 4, d = 5, next = 8;
            for (unsigned triangle = 0; triangle < 2; ++triangle) {
                const unsigned code = (pair >> (triangle ? 0 : 2)) & 3;
                if (code == 0) b = d;
                else if (code == 1) a = d;
                else if (code == 2) { const unsigned old = a; a = b; b = old; }
                else { a = next++; b = next++; }
                d = next++;
                const unsigned values[] = {a,b,d};
                for (unsigned corner = 0; corner < 3; ++corner) {
                    const unsigned offset = pair * 16 + triangle * 6 + corner * 2;
                    expected[offset] = uint8_t(values[corner] * 2);
                    expected[offset + 1] = uint8_t(values[corner] * 2 + 1);
                }
            }
            for (unsigned byte = 0; byte < 16; ++byte)
                expected[256 + pair * 16 + byte] = uint8_t((next - 8) * 2 + byte);
        }
        return expected;
    }();
    // Compare all guest bytes on every call; no pointer-based admission cache.
    return memcmp(c.ls + 0xd690, edge_tables.data(), edge_tables.size()) == 0;
}
inline bool decode_geometry_host_indices(const spu_context& c, const GeometryJobEntry& e,
    GeometryHostIndexData& output, const char** reason = nullptr)
{
    output.ready = false; output.final_group_kept = 5;
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (c.ls != c.ls_store || e.index_flavor() != 3 || e.index_count > 65535 || e.index_count % 3)
        return reject("host index job family");
    const unsigned triangles = e.index_count / 3;
    if (!triangles) { output.triangles = output.maximum_index = 0; output.ready = true; if (reason) *reason = nullptr; return true; }
    const auto& source = e.input[GeometryJobEntry::indices];
    const uint64_t io_end = uint64_t(e.io_lsa) + e.io_bytes;
    if ((source.lsa & 15) || source.size < 8 || source.lsa < e.io_lsa || uint64_t(source.lsa) + source.size > io_end || io_end > SPU_LS_SIZE)
        return reject("host index staged span");
    const auto* bytes = c.ls + source.lsa;
    const unsigned deltas = geometry_job_be16(bytes), bias = geometry_job_be16(bytes + 2);
    const unsigned flags = geometry_job_be16(bytes + 4), width = bytes[6], commands = (triangles + 3) / 4;
    if ((width != 0 && (width < 2 || width > 11)) || (width == 0) != (deltas == 0) || bytes[7])
        return reject("host unsupported index encoding");
    const unsigned packed = 8 + flags + commands;
    if (uint64_t(packed) + (uint64_t(deltas) * width + 7) / 8 > source.size)
        return reject("host index section spans");
    if (!geometry_host_index_codec_matches(c)) return reject("host index codec definition");
#if BUZZ_GEOMETRY_INDEX_SEQUENTIAL
    if (!width) {
        // No explicit deltas exist in this encoding. Decode its edge commands
        // with sequential references, then validate exactly the consumed flag
        // bits in bulk. Unused flags are deliberately not part of admission.
        const auto* codes = bytes + 8 + flags;
        if ((codes[0] >> 6) != 3) return reject("host index initial triangle");
        uint16_t sequential = 0, a = 0, b = 0, d = 0;
        for (unsigned triangle = 0; triangle < triangles; ++triangle) {
            const unsigned code = (codes[triangle / 4] >> (6 - triangle % 4 * 2)) & 3;
            if (code == 0) b = d;
            else if (code == 1) a = d;
            else if (code == 2) { const uint16_t old = a; a = b; b = old; }
            else { a = sequential++; b = sequential++; }
            d = sequential++;
            output.component[0][triangle] = a;
            output.component[1][triangle] = b;
            output.component[2][triangle] = d;
        }
        // index_count <= 65535 bounds the total references, so the sequential
        // counter cannot wrap, including an all-independent-triangle block.
        if (sequential > flags * 8) return reject("host index reference span");
        const unsigned whole = sequential / 8;
        unsigned at = 0;
        for (; whole - at >= 8; at += 8) {
            uint64_t word; memcpy(&word, bytes + 8 + at, sizeof word);
            if (word) return reject("host index reference span");
        }
        for (; at < whole; ++at)
            if (bytes[8 + at]) return reject("host index reference span");
        if ((sequential & 7) && (bytes[8 + whole] >> (8 - (sequential & 7))))
            return reject("host index reference span");
        for (unsigned triangle = triangles; triangle < ((triangles + 7) & ~7u); ++triangle)
            for (auto& component : output.component) component[triangle] = 0;
        output.triangles = triangles; output.maximum_index = sequential - 1;
        output.ready = true;
        if (reason) *reason = nullptr;
        return true;
    }
#endif
    uint16_t carry[8]{}, sequential = 0, a = 0, b = 0, d = 0;
#if BUZZ_GEOMETRY_INDEX_BATCHED
    unsigned flag_cursor = 0, explicit_cursor = 0, packed_cursor = packed, bit_count = 0, maximum = 0;
    const unsigned packed_end = packed + (deltas * width + 7) / 8;
    const bool bulk_deltas = deltas >= 8 && deltas <= 2048;
    // Owned by this worker, fully overwritten before use, with no guest cache.
    // Keep larger encodings on the existing bounded streaming decoder.
    alignas(32) static thread_local uint16_t decoded_deltas[2048];
    if (bulk_deltas) {
        unsigned at=packed, i=0;
        for (; deltas-i>=8; i+=8) {
            geometry_host_index_delta8(bytes+at,packed_end-at,width,bias,carry);
            memcpy(decoded_deltas+i,carry,sizeof carry);at+=width;
        }
        unsigned bits=0, value_bits=0;
        for (;i<deltas;++i) {
            while(bits<width){value_bits=(value_bits<<8)|bytes[at++];bits+=8;}
            bits-=width;
            carry[i&7]=uint16_t(carry[i&7]+((value_bits>>bits)&((1u<<width)-1))-bias);
            decoded_deltas[i]=carry[i&7];
        }
    }
    unsigned reservoir = 0;
#else
    unsigned flag_cursor = 0, explicit_cursor = 0, packed_cursor = packed, bit_count = 0, reservoir = 0, maximum = 0;
#endif
    bool valid = true;
    auto reference = [&]() __attribute__((always_inline)) -> uint16_t {
        if (flag_cursor >= flags * 8) { valid = false; return 0; }
        const bool explicit_value = (bytes[8 + flag_cursor / 8] >> (7 - flag_cursor % 8)) & 1;
        ++flag_cursor;
        if (!explicit_value) {
            const uint16_t value = sequential++;
            maximum = value > maximum ? value : maximum;
            return value;
        }
        if (explicit_cursor >= deltas) { valid = false; return 0; }
#if BUZZ_GEOMETRY_INDEX_BATCHED
        if (bulk_deltas) {
            const uint16_t value = decoded_deltas[explicit_cursor++];
            maximum = value > maximum ? value : maximum;
            return value;
        }
#endif
        while (bit_count < width) { reservoir = (reservoir << 8) | bytes[packed_cursor++]; bit_count += 8; }
        bit_count -= width;
        const unsigned raw = (reservoir >> bit_count) & ((1u << width) - 1);
        auto& value = carry[explicit_cursor++ & 7]; value = uint16_t(value + raw - bias);
        maximum = value > maximum ? value : maximum; return value;
    };
    for (unsigned triangle = 0; triangle < triangles; ++triangle) {
        const unsigned code = (bytes[8 + flags + triangle / 4] >> (6 - triangle % 4 * 2)) & 3;
        if (triangle == 0 && code != 3) return reject("host index initial triangle");
        if (code == 0) b = d;
        else if (code == 1) a = d;
        else if (code == 2) { const uint16_t old = a; a = b; b = old; }
        else { a = reference(); b = reference(); }
        d = reference();
        if (!valid) return reject("host index reference span");
        output.component[0][triangle] = a; output.component[1][triangle] = b; output.component[2][triangle] = d;
        // Every emitted index originated in reference(). Edge reuse cannot
        // increase the maximum, so don't compare reused a/b on every triangle.
    }
    for (unsigned triangle = triangles; triangle < ((triangles + 7) & ~7u); ++triangle)
        for (auto& component : output.component) component[triangle] = 0;
    output.triangles = triangles; output.maximum_index = maximum; output.ready = true;
    if (reason) *reason = nullptr; return true;
}
} // namespace buzz
