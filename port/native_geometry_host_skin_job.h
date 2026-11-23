/* Skinning and object transforms on eight host vertices, without SPU ABI or matrix
 * scratch. Palette and influences are read once before job LS is reused.
 * Only logical vertices are handled here; publication tails are separate. */
#pragma once
#include "native_geometry_host_input_job.h"
#include "native_geometry_bounded_position.h"
#include <type_traits>

#if defined(__AVX2__) && !defined(__aarch64__)
#define BUZZ_GEOMETRY_SKIN_ROWS 1
#else
#define BUZZ_GEOMETRY_SKIN_ROWS 0
#endif

namespace buzz {
struct GeometryHostSkinRecipe {
    unsigned palette = 0, matrices = 0, weights = 0;
    std::array<unsigned, 4> field{{16,16,16,16}};
    bool active = false, object = false;
};
struct GeometryHostSkinWorkspace {
    alignas(16) uint32_t object[12];
#if BUZZ_GEOMETRY_SKIN_ROWS
    alignas(32) uint32_t row[256][16];
    uint32_t word(unsigned matrix, unsigned component) const { return row[matrix][component]; }
#else
    alignas(32) uint32_t coefficient[12][256];
    uint32_t word(unsigned matrix, unsigned component) const { return coefficient[component][matrix]; }
#endif
};
#if BUZZ_GEOMETRY_SKIN_ROWS
inline void geometry_skin_transpose8(const __m256* r, const __m128* h, v8* out)
{
    const __m256 t0=_mm256_unpacklo_ps(r[0],r[1]), t1=_mm256_unpackhi_ps(r[0],r[1]);
    const __m256 t2=_mm256_unpacklo_ps(r[2],r[3]), t3=_mm256_unpackhi_ps(r[2],r[3]);
    const __m256 t4=_mm256_unpacklo_ps(r[4],r[5]), t5=_mm256_unpackhi_ps(r[4],r[5]);
    const __m256 t6=_mm256_unpacklo_ps(r[6],r[7]), t7=_mm256_unpackhi_ps(r[6],r[7]);
    const __m256 s0=_mm256_shuffle_ps(t0,t2,_MM_SHUFFLE(1,0,1,0)), s1=_mm256_shuffle_ps(t0,t2,_MM_SHUFFLE(3,2,3,2));
    const __m256 s2=_mm256_shuffle_ps(t1,t3,_MM_SHUFFLE(1,0,1,0)), s3=_mm256_shuffle_ps(t1,t3,_MM_SHUFFLE(3,2,3,2));
    const __m256 s4=_mm256_shuffle_ps(t4,t6,_MM_SHUFFLE(1,0,1,0)), s5=_mm256_shuffle_ps(t4,t6,_MM_SHUFFLE(3,2,3,2));
    const __m256 s6=_mm256_shuffle_ps(t5,t7,_MM_SHUFFLE(1,0,1,0)), s7=_mm256_shuffle_ps(t5,t7,_MM_SHUFFLE(3,2,3,2));
    out[0]=_mm256_castps_si256(_mm256_permute2f128_ps(s0,s4,0x20));
    out[1]=_mm256_castps_si256(_mm256_permute2f128_ps(s1,s5,0x20));
    out[2]=_mm256_castps_si256(_mm256_permute2f128_ps(s2,s6,0x20));
    out[3]=_mm256_castps_si256(_mm256_permute2f128_ps(s3,s7,0x20));
    out[4]=_mm256_castps_si256(_mm256_permute2f128_ps(s0,s4,0x31));
    out[5]=_mm256_castps_si256(_mm256_permute2f128_ps(s1,s5,0x31));
    out[6]=_mm256_castps_si256(_mm256_permute2f128_ps(s2,s6,0x31));
    out[7]=_mm256_castps_si256(_mm256_permute2f128_ps(s3,s7,0x31));
    __m256 y[4];
    for (unsigned k = 0; k < 4; ++k) y[k] = _mm256_insertf128_ps(_mm256_castps128_ps256(h[k]), h[k + 4], 1);
    const __m256 u0=_mm256_unpacklo_ps(y[0],y[1]), u1=_mm256_unpackhi_ps(y[0],y[1]);
    const __m256 u2=_mm256_unpacklo_ps(y[2],y[3]), u3=_mm256_unpackhi_ps(y[2],y[3]);
    out[8]=_mm256_castps_si256(_mm256_shuffle_ps(u0,u2,_MM_SHUFFLE(1,0,1,0)));
    out[9]=_mm256_castps_si256(_mm256_shuffle_ps(u0,u2,_MM_SHUFFLE(3,2,3,2)));
    out[10]=_mm256_castps_si256(_mm256_shuffle_ps(u1,u3,_MM_SHUFFLE(1,0,1,0)));
    out[11]=_mm256_castps_si256(_mm256_shuffle_ps(u1,u3,_MM_SHUFFLE(3,2,3,2)));
}
// out[c] lane l = row[lane_index[l]][c]: the exact words
// _mm256_i32gather_epi32(coefficient[c], index, 4) returns.
inline void geometry_skin_rows8(const uint32_t (*row)[16], const uint32_t* lane_index, v8* out)
{
    __m256 r[8]; __m128 h[8];
    for (unsigned l = 0; l < 8; ++l) {
        const float* at = reinterpret_cast<const float*>(row[lane_index[l]]);
        r[l] = _mm256_load_ps(at); h[l] = _mm_load_ps(at + 8);
    }
    geometry_skin_transpose8(r, h, out);
}
#endif
inline bool geometry_host_skin_codec_matches(const spu_context& c, bool object)
{
    struct Quad {
        unsigned at, modes;
        std::array<uint8_t,16> bytes{};
        constexpr Quad(unsigned offset, unsigned mask, std::array<uint32_t,4> words)
            : at(offset), modes(mask) {
            for (unsigned i = 0; i < 16; ++i)
                bytes[i] = uint8_t(words[i / 4] >> (24 - 8 * (i % 4)));
        }
    };
    static constexpr Quad tables[] = {
        {0xd930,3,{0x00010203,0x10111213,0x04050607,0x14151617}},
        {0xcd60,1,{0,0,0,0x3f800000}},
        {0xdac0,1,{0x0c0d0e0f,0x1c1d1e1f,0x08090a0b,0x18191a1b}},
        {0xdaa0,1,{0x18191a1b,0x1c1d1e1f,0x00010203,0x04050607}},
        {0xda70,1,{0x08090a0b,0x18191a1b,0x08090a0b,0x18191a1b}},
        {0xd920,1,{0x00010203,0x00010203,0x00010203,0x10111213}},
        {0xd900,2,{0x80808080,0x0c0d0e0f,0x1c1d1e1f,0x80808080}},
        {0xdab0,2,{0x1c1d1e1f,0x04050607,0x08090a0b,0x80808080}},
        {0xda00,2,{0x04050607,0x08090a0b,0x00010203,0x0c0d0e0f}},
        {0xd940,2,{0x00010203,0x10111213,0x08090a0b,0x18191a1b}},
        {0xd890,2,{0x80808000,0x80808002,0x80808004,0x80808006}}};
    for (const auto& table : tables) if (table.modes & (object ? 1 : 2))
        if (memcmp(c.ls + table.at, table.bytes.data(), table.bytes.size())) return false;
    return true;
}
inline bool read_geometry_host_skin_recipe(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, GeometryHostSkinRecipe& output, const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (c.ls != c.ls_store || input.vertices != e.vertices || !e.vertices || (e.config[0] & 16))
        return reject("host skin input family");
    GeometryHostSkinRecipe recipe;
    for (unsigned field = 0; field < input.fields; ++field) {
        const unsigned semantic = input.field[field].semantic;
        if (semantic >= 1 && semantic <= 4) recipe.field[semantic - 1] = field;
    }
    if (e.flags & 0x10000000) {
        const auto& object = e.input[GeometryJobEntry::object];
        if ((object.lsa & 15) || object.size < 48 || object.lsa < e.io_lsa ||
            uint64_t(object.lsa) + 48 > uint64_t(e.io_lsa) + e.io_bytes)
            return reject("host object matrix span");
        recipe.palette = object.lsa; recipe.object = true;
        if (recipe.field[0] == 16 || input.field[recipe.field[0]].storage != 2)
            return reject("host object position source");
        for (unsigned semantic = 1; semantic < 3; ++semantic)
            if (recipe.field[semantic] != 16 && input.field[recipe.field[semantic]].storage != 9)
                return reject("host object direction source");
        if (!geometry_host_skin_codec_matches(c, true)) return reject("host object codec definition");
        output = recipe; if (reason) *reason = nullptr; return true;
    }
    const unsigned matrices = (e.input[4].size_flags + e.input[5].size_flags) / 48;
    if (e.skin_flavor() && matrices) {
        if (e.skin_flavor() != 1 || !(e.config[0] & 128)) return reject("host skin mode");
        recipe.palette = e.input[4].lsa; recipe.weights = e.input[6].lsa;
        recipe.matrices = matrices < 256 ? matrices : 256;
        const uint64_t palette_end = uint64_t(e.input[5].lsa) + e.input[5].size;
        const uint64_t io_end = uint64_t(e.io_lsa) + e.io_bytes;
        if ((recipe.palette & 15) || (recipe.weights & 15) || recipe.palette < e.io_lsa || recipe.weights < e.io_lsa ||
            uint64_t(recipe.palette) + uint64_t(matrices) * 48 > palette_end || palette_end > io_end ||
            uint64_t(e.vertices) * 8 > e.input[6].size || uint64_t(recipe.weights) + uint64_t(e.vertices) * 8 > io_end)
            return reject("host skin palette/weight span");
        if (recipe.field[0] == 16 || input.field[recipe.field[0]].storage != 2)
            return reject("host skin position source");
        for (unsigned semantic = 1; semantic < 4; ++semantic)
            if (recipe.field[semantic] != 16 && input.field[recipe.field[semantic]].storage != 9)
                return reject("host skin direction source");
        if (geometry_job_be32(c.ls + 0xda10) != 0x14151617 || geometry_job_be32(c.ls + 0xda14) != 0x08090a0b ||
            geometry_job_be32(c.ls + 0xda18) != 0x10111213 || geometry_job_be32(c.ls + 0xda1c) != 0x0c0d0e0f ||
            geometry_job_be32(c.ls + 0xda60) != 0x18191a1b || geometry_job_be32(c.ls + 0xda64) != 0x10111213 ||
            geometry_job_be32(c.ls + 0xda68) != 0x04050607 || geometry_job_be32(c.ls + 0xda6c) != 0x0c0d0e0f)
            return reject("host skin palette codec definition");
        if (!geometry_host_skin_codec_matches(c, false)) return reject("host weighted codec definition");
        recipe.active = true;
    }
    output = recipe; if (reason) *reason = nullptr; return true;
}

template<bool BoundedDirection = false>
inline void transform_geometry_host_object_value(GeometryHostSoA8& value,
    const v8* matrix, bool direction, v8 mask)
{
    if constexpr (BoundedDirection) {
        v8 outside = _mm256_setzero_si256();
        for (unsigned axis = 0; axis < 3; ++axis)
            outside = _mm256_or_si256(outside, _mm256_cmpgt_epi32(
                _mm256_and_si256(value.component[axis], v8_set(0x7fffffff)), v8_set(0x49800000)));
        if (_mm256_movemask_ps(_mm256_castsi256_ps(outside))) {
            transform_geometry_host_object_value<false>(value, matrix, direction, mask);
            return;
        }
    }
    // A bounded matrix (2^30) and direction (2^20) keep the squared length
    // below 2^106. Normalization still uses the complete SPU reciprocal path.
    const auto mul = [](v8 a, v8 b) {
        if constexpr (BoundedDirection) return geometry_bounded_mul(a,b);
        return v8_fm(a,b);
    };
    const auto madd = [](v8 a, v8 b, v8 c) {
        if constexpr (BoundedDirection) return geometry_bounded_fma(a,b,c);
        return v8_fma(a,b,c);
    };
    const v8 x = value.component[0], y = value.component[1], z = value.component[2], w = value.component[3];
    // Preserve each coordinate's cyclic arithmetic order. Every intermediate
    // below is already a canonical SPU result: FMA(w,+0,acc) for a direction
    // is exactly acc, even for extended w. Raw inputs are never simplified.
    v8 a = mul(x, matrix[0]);
    v8 b = mul(y, matrix[5]);
    v8 d = mul(z, matrix[10]);
    a = madd(y, matrix[1], a);
    b = madd(z, matrix[6], b);
    if (!direction) d = madd(w, matrix[11], d);
    a = madd(z, matrix[2], a);
    if (!direction) b = madd(w, matrix[7], b);
    d = madd(x, matrix[8], d);
    if (!direction) a = madd(w, matrix[3], a);
    b = madd(x, matrix[4], b);
    d = madd(y, matrix[9], d);
    if (direction) {
        v8 length = mul(a, a); length = madd(b, b, length); length = madd(d, d, length);
        const v8 inverse = v8_fi(length, v8_frsqest(length));
        a = v8_fm(a, inverse); b = v8_fm(b, inverse); d = v8_fm(d, inverse);
    }
    value.component[0] = _mm256_and_si256(a, mask);
    value.component[1] = _mm256_and_si256(b, mask);
    value.component[2] = _mm256_and_si256(d, mask);
}

template<bool Bounded = false>
inline void transform_geometry_host_weighted_value(GeometryHostSoA8& value,
    const v8* blended, bool point, v8 mask)
{
    if constexpr (Bounded) {
        v8 largest=_mm256_setzero_si256();
        for(unsigned axis=0;axis<3;++axis) {
            const v8 magnitude=_mm256_and_si256(value.component[axis],v8_set(0x7fffffff));
#if defined(__aarch64__)
            largest={vmaxq_u32(largest.lo,magnitude.lo),vmaxq_u32(largest.hi,magnitude.hi)};
#else
            largest=_mm256_max_epi32(largest,magnitude);
#endif
        }
        if(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(largest,v8_set(0x4e800000))))) {
            transform_geometry_host_weighted_value<false>(value,blended,point,mask);return;
        }
    }
    // Bounded callers proved each palette coefficient <= 2^30. Four byte
    // weights <= 1 give blended coefficients <= 2^32; input <= 2^30 keeps
    // every transform intermediate below 2^65, within ordinary FP range.
    auto fm=[](v8 a,v8 b) { if constexpr(Bounded)return geometry_bounded_mul(a,b);else return v8_fm(a,b); };
    auto fma=[](v8 a,v8 b,v8 c) { if constexpr(Bounded)return geometry_bounded_fma(a,b,c);else return v8_fma(a,b,c); };
    const v8 x = value.component[0], y = value.component[1], z = value.component[2];
    v8 a = point ? fma(x, blended[0], blended[3]) : fm(x, blended[0]);
    v8 b = point ? fma(y, blended[5], blended[7]) : fm(y, blended[5]);
    v8 d = point ? fma(z, blended[10], blended[11]) : fm(z, blended[10]);
    a = fma(y, blended[1], a); a = fma(z, blended[2], a);
    b = fma(z, blended[6], b); b = fma(x, blended[4], b);
    d = fma(x, blended[8], d); d = fma(y, blended[9], d);
    value.component[0] = _mm256_and_si256(a, mask);
    value.component[1] = _mm256_and_si256(b, mask);
    value.component[2] = _mm256_and_si256(d, mask);
}

template<bool Bounded>
inline void transform_geometry_skin_block(const GeometryHostInputRecipe& input,
    const GeometryHostSkinRecipe& recipe, GeometryHostInputData& data, unsigned block,
    const v8* blended, unsigned active)
{
    const v8 mask = _mm256_cmpgt_epi32(v8_set(active), _mm256_setr_epi32(0,1,2,3,4,5,6,7));
    for (unsigned semantic = 0; semantic < 4; ++semantic)
        if (recipe.field[semantic] != 16)
            transform_geometry_host_weighted_value<Bounded>(
                data.at(input, recipe.field[semantic], block), blended, semantic == 0, mask);
}

#if BUZZ_GEOMETRY_SKIN_ROWS
// Repeated vertices in a draw often have the same four bone/weight pairs.
// A hit reuses the matrix before the position and direction transforms. The
// palette and its bound belong to this invocation. Copy each lane's result
// before looking up the next lane, which may evict the same slot.
inline bool skin_geometry_bounded_palette(const uint8_t* records, const GeometryHostInputRecipe& input,
    const GeometryHostSkinRecipe& recipe, GeometryHostInputData& data, const GeometryHostSkinWorkspace& workspace)
{
    // Exact RTZ product of each byte with guest coefficient 0x3b808080.
    // Normalize the integer significand and discard low bits on each shift.
    static constexpr auto weight_bits = [] {
        std::array<uint32_t,256> result{};
        for (unsigned weight = 1; weight < result.size(); ++weight) {
            uint32_t significand = weight * 0x808080u, exponent = 119;
            while (significand >= 0x1000000u) { significand >>= 1; ++exponent; }
            result[weight] = (exponent << 23) | (significand & 0x7fffffu);
        }
        return result;
    }();
    struct Matrix { v8 first; __m128i last; };
    Matrix cache[64];
    uint64_t keys[64], occupied = 0;
    v8 blended[12];
    for (unsigned first = 0; first < input.vertices; first += 8) {
        const unsigned active = input.vertices - first < 8 ? input.vertices - first : 8;
        // A matching full block has the same validated influences in every lane.
        // The palette is immutable during this job. Reuse its transposed columns.
        if (first && active == 8 && !memcmp(records + 8 * first, records + 8 * (first - 8), 64)) {
            transform_geometry_skin_block<true>(input, recipe, data, first / 8, blended, active);
            continue;
        }
        __m256 rows[8];
        __m128 tails[8];
        uint64_t previous_key = 0;
        for (unsigned lane = 0; lane < active; ++lane) {
            const uint8_t* weights = records + 8 * (first + lane);
            uint64_t key;
            memcpy(&key, weights, sizeof(key));
            if (lane && key == previous_key) {
                rows[lane] = rows[lane - 1];
                tails[lane] = tails[lane - 1];
                continue;
            }
            previous_key = key;
            const unsigned slot = unsigned(((key ^ (key >> 32)) * UINT64_C(0x9e3779b97f4a7c15)) >> 58);
            const uint64_t bit = UINT64_C(1) << slot;
            Matrix& matrix = cache[slot];
            if (!(occupied & bit) || keys[slot] != key) {
                v8 lo = _mm256_setzero_si256(), hi = _mm256_setzero_si256();
                for (unsigned influence = 0; influence < 4; ++influence) {
                    const unsigned index = weights[2 * influence + 1];
                    if (index >= recipe.matrices) return false;
                    const unsigned weight = weights[2 * influence];
                    if (!weight) continue;
                    const v8 factor = v8_set(weight_bits[weight]);
                    const uint32_t* row = workspace.row[index];
                    const v8 a = _mm256_load_si256(reinterpret_cast<const v8*>(row));
                    const v8 b = _mm256_zextsi128_si256(_mm_load_si128(reinterpret_cast<const __m128i*>(row + 8)));
                    // FMA with initial +0 has the same rounded product and
                    // canonical zero as the first multiply in the lane path.
                    lo = geometry_bounded_fma(factor, a, lo);
                    hi = geometry_bounded_fma(factor, b, hi);
                }
                matrix = {lo, _mm256_castsi256_si128(hi)};
                keys[slot] = key;
                occupied |= bit;
            }
            rows[lane] = _mm256_castsi256_ps(matrix.first);
            tails[lane] = _mm_castsi128_ps(matrix.last);
        }
        for (unsigned lane = active; lane < 8; ++lane) {
            rows[lane] = _mm256_setzero_ps();
            tails[lane] = _mm_setzero_ps();
        }
        geometry_skin_transpose8(rows, tails, blended);
        transform_geometry_skin_block<true>(input, recipe, data, first / 8, blended, active);
    }
    return true;
}
#endif

template<bool Bounded, GeometryHostInputPhase Phase = GeometryHostInputPhase::All>
inline bool skin_geometry_host_inputs_impl(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, GeometryHostInputData& data,
    GeometryHostSkinWorkspace& workspace, const char** reason = nullptr,
    const GeometryHostSkinRecipe* prepared = nullptr)
{
    if (!data.ready) { if (reason) *reason = "host skin input not ready"; return false; }
    if constexpr(Phase==GeometryHostInputPhase::All) {
        if(data.pending_fields){if(reason)*reason="host skin deferred input";return false;}
    }
    data.ready = false;
    GeometryHostSkinRecipe local;
    if (!prepared && !read_geometry_host_skin_recipe(c, e, input, local, reason)) return false;
    const GeometryHostSkinRecipe& recipe = prepared ? *prepared : local;
    if constexpr(Phase!=GeometryHostInputPhase::All) {
        // Weighted palettes stay in one pass: repeating their blend would
        // penalize visible jobs. Only rigid/unskinned jobs defer attributes.
        if(recipe.active){if(reason)*reason="host deferred weighted skin";return false;}
    }
    if (recipe.object) {
        v8 matrix[12];
        for (unsigned coefficient = 0; coefficient < 12; ++coefficient) {
            workspace.object[coefficient] = geometry_job_be32(c.ls + recipe.palette + coefficient * 4);
            matrix[coefficient] = v8_set(workspace.object[coefficient]);
        }
        const bool bounded_object=geometry_object_matrix_bounded(workspace.object);
        for (unsigned first = 0; first < input.vertices; first += 8) {
            const unsigned active = input.vertices - first < 8 ? input.vertices - first : 8;
            const v8 mask = _mm256_cmpgt_epi32(v8_set(active), _mm256_setr_epi32(0,1,2,3,4,5,6,7));
            for (unsigned semantic = 0; semantic < 3; ++semantic) {
                if(!geometry_host_capture_field<Phase>(semantic+1))continue;
                if (recipe.field[semantic] == 16 || (semantic && e.position_only())) continue;
                auto& value = data.at(input, recipe.field[semantic], first / 8);
                if (semantic==0 && bounded_object && transform_geometry_bounded_position(value,matrix,mask)) continue;
                if (semantic && bounded_object)
                    transform_geometry_host_object_value<true>(value,matrix,true,mask);
                else
                    transform_geometry_host_object_value(value,matrix,semantic!=0,mask);
            }
        }
        data.ready = true; if (reason) *reason = nullptr; return true;
    }
    if (!recipe.active) { data.ready = true; return true; }
#if BUZZ_GEOMETRY_SKIN_ROWS
    // Decode exactly the twelve big-endian coefficients of each matrix.
    // Accumulate the range check while those words are in registers.
    v8 outside = _mm256_setzero_si256();
    for (unsigned i = 0; i < recipe.matrices; ++i) {
        const uint8_t* source = c.ls + recipe.palette + i * 48;
        const v8 first = v8_bswap(_mm256_loadu_si256(reinterpret_cast<const v8*>(source)));
        const v8 last = v8_bswap(_mm256_zextsi128_si256(
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(source + 32))));
        _mm256_store_si256(reinterpret_cast<v8*>(workspace.row[i]), first);
        _mm_store_si128(reinterpret_cast<__m128i*>(workspace.row[i] + 8), _mm256_castsi256_si128(last));
        if constexpr (Bounded) {
            const v8 magnitude = v8_set(0x7fffffff);
            const v8 largest = _mm256_max_epi32(_mm256_and_si256(first, magnitude),
                                                _mm256_and_si256(last, magnitude));
            outside = _mm256_or_si256(outside, _mm256_cmpgt_epi32(largest, v8_set(0x4e800000)));
        }
    }
    const bool bounded_palette = Bounded && _mm256_testz_si256(outside, outside);
#else
    uint32_t largest_palette=0;
    for (unsigned matrix = 0; matrix < recipe.matrices; ++matrix)
        for (unsigned coefficient = 0; coefficient < 12; ++coefficient) {
            const auto word=geometry_job_be32(c.ls + recipe.palette + matrix * 48 + coefficient * 4);
            workspace.coefficient[coefficient][matrix]=word;
            if constexpr(Bounded) if((word&0x7fffffff)>largest_palette)largest_palette=word&0x7fffffff;
        }
    const bool bounded_palette=Bounded && largest_palette<=0x4e800000;
#endif
#if BUZZ_GEOMETRY_SKIN_ROWS
    if (bounded_palette) {
        if (!skin_geometry_bounded_palette(c.ls + recipe.weights, input, recipe, data, workspace)) {
            if (reason) *reason = "host skin palette index";
            return false;
        }
        data.ready = true;
        if (reason) *reason = nullptr;
        return true;
    }
#endif
    for (unsigned first = 0; first < input.vertices; first += 8) {
        const unsigned active = input.vertices - first < 8 ? input.vertices - first : 8;
        alignas(32) uint32_t indices[4][8]{}, weights[4][8]{};
        for (unsigned lane = 0; lane < active; ++lane) for (unsigned influence = 0; influence < 4; ++influence) {
            const auto* at = c.ls + recipe.weights + (first + lane) * 8 + influence * 2;
            weights[influence][lane] = at[0]; indices[influence][lane] = at[1];
        }
        v8 index[4], factor[4];
        bool contributes[4];
        for (unsigned influence = 0; influence < 4; ++influence) {
            index[influence] = _mm256_load_si256(reinterpret_cast<const v8*>(indices[influence]));
            if (_mm256_movemask_epi8(_mm256_cmpgt_epi32(index[influence], v8_set(recipe.matrices - 1)))) {
                if (reason) *reason = "host skin palette index"; return false;
            }
            const v8 integer_weight=_mm256_castps_si256(_mm256_cvtepi32_ps(
                _mm256_load_si256(reinterpret_cast<const v8*>(weights[influence]))));
            // Staged weights are uint8, so this conversion is always finite.
            factor[influence] = Bounded ? geometry_bounded_mul(integer_weight,v8_set(0x3b808080))
                                        : v8_fm(integer_weight,v8_set(0x3b808080));
            contributes[influence] = _mm256_movemask_ps(_mm256_castsi256_ps(v8_nonzero(factor[influence]))) != 0;
        }
        v8 blended[12];
#if BUZZ_GEOMETRY_SKIN_ROWS
        v8 fetched[4][12];
        for (unsigned influence = 0; influence < 4; ++influence)
            if (contributes[influence]) geometry_skin_rows8(workspace.row, indices[influence], fetched[influence]);
#define BUZZ_SKIN_FETCH(influence, coefficient) fetched[influence][coefficient]
#else
#define BUZZ_SKIN_FETCH(influence, col) _mm256_i32gather_epi32(reinterpret_cast<const int*>(workspace.coefficient[col]), index[influence], 4)
#endif
        auto run=[&](auto tag) {
        constexpr bool Fast=decltype(tag)::value;
        for (unsigned coefficient = 0; coefficient < 12; ++coefficient) {
            // Validate every index above even for zero weights. A wholly zero
            // influence has exact SPU product +0; FMA(+0, x, blended) preserves
            // the already canonical SPU accumulator, including extended x.
            blended[coefficient] = contributes[0]
                ? (Fast ? geometry_bounded_mul(factor[0], BUZZ_SKIN_FETCH(0, coefficient))
                        : v8_fm(factor[0], BUZZ_SKIN_FETCH(0, coefficient))) : _mm256_setzero_si256();
            for (unsigned influence = 1; influence < 4; ++influence) if (contributes[influence])
                blended[coefficient] = Fast
                    ? geometry_bounded_fma(factor[influence], BUZZ_SKIN_FETCH(influence, coefficient), blended[coefficient])
                    : v8_fma(factor[influence], BUZZ_SKIN_FETCH(influence, coefficient), blended[coefficient]);
        }
        transform_geometry_skin_block<Fast>(input, recipe, data, first / 8, blended, active);
        };
        if constexpr(Bounded) {
            if(bounded_palette)run(std::true_type{});else run(std::false_type{});
        } else run(std::false_type{});
#undef BUZZ_SKIN_FETCH
    }
    data.ready = true; if (reason) *reason = nullptr; return true;
}
template<GeometryHostInputPhase Phase = GeometryHostInputPhase::All>
inline bool skin_geometry_host_inputs_phase(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, GeometryHostInputData& data,
    GeometryHostSkinWorkspace& workspace, const char** reason = nullptr,
    const GeometryHostSkinRecipe* prepared = nullptr)
{
#if defined(BUZZ_GEOMETRY_BOUNDED_SKIN) && BUZZ_GEOMETRY_BOUNDED_SKIN
    return skin_geometry_host_inputs_impl<true,Phase>(c,e,input,data,workspace,reason,prepared);
#else
    return skin_geometry_host_inputs_impl<false,Phase>(c,e,input,data,workspace,reason,prepared);
#endif
}
inline bool skin_geometry_host_inputs(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, GeometryHostInputData& data,
    GeometryHostSkinWorkspace& workspace, const char** reason = nullptr)
{
    return skin_geometry_host_inputs_phase(c,e,input,data,workspace,reason);
}
} // namespace buzz
