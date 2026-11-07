/* Job clip parameters and eight-vertex projection on owned host data.
 * Records stay in host SoA for culling. No LS scratch, call frame, SPU
 * register state, lookahead or publication is produced by this helper. */
#pragma once
#include "native_geometry_host_input_job.h"

namespace buzz {
struct GeometryHostClipParameters {
    uint32_t row[4][4], minimum[3], maximum[3], quantization_mask;
};
struct GeometryHostClipData {
    alignas(32) uint32_t component[4][4096]; // X,Y float bits; flags; packed XY/W-sign.
    unsigned vertices = 0;
    bool ready = false;
};
inline bool read_geometry_host_clip_parameters(const spu_context& c, const GeometryJobEntry& e,
    GeometryHostClipParameters& output, const char** reason = nullptr)
{
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    const auto& viewport = e.input[GeometryJobEntry::viewport];
    if (c.ls != c.ls_store || (viewport.lsa & 15) || viewport.size < 128 || viewport.lsa < e.io_lsa ||
        uint64_t(viewport.lsa) + 128 > uint64_t(e.io_lsa) + e.io_bytes || uint64_t(e.io_lsa) + e.io_bytes > SPU_LS_SIZE)
        return reject("host clip viewport span");
    struct Quad { unsigned at; uint32_t word[4]; };
    static constexpr Quad tables[] = {
        {0xd930,{0x00010203,0x10111213,0x04050607,0x14151617}},
        {0xd940,{0x00010203,0x10111213,0x08090a0b,0x18191a1b}},
        {0xda40,{0x04050607,0x14151617,0x0c0d0e0f,0x1c1d1e1f}},
        {0xdaa0,{0x18191a1b,0x1c1d1e1f,0x00010203,0x04050607}},
        {0xd8d0,{0x80800001,0x80800203,0x80801415,0x80801617}},
        {0xda90,{0x08090a0b,0x0c0d0e0f,0x10111213,0x80808080}},
        {0xd960,{0x00010203,0x04050607,0x10111213,0x80808080}},
        {0xcfc0,{0x00011213,0x04051617,0x08091a1b,0x0c0d1e1f}},
        {0xcfd0,{0x80808004,0x05060707,0x80808080,0x80808080}},
        {0xd990,{0x00010203,0x04050607,0x13808080,0x0c0d0e0f}},
        {0xdae0,{0x0a0b0203,0x06071213,0x16171a1b,0x80808080}}};
    for (const auto& table : tables) for (unsigned lane = 0; lane < 4; ++lane)
        if (geometry_job_be32(c.ls + table.at + lane * 4) != table.word[lane]) return reject("host clip codec definition");
    const auto* view = c.ls + viewport.lsa;
    const unsigned format = view[112];
    if (format != 0 && format != 3) return reject("host unsupported clip format");
    uint32_t origin[4], a[4], b[4], basis[3][4], matrix[4][4];
    for (unsigned component = 0; component < 4; ++component) {
        origin[component] = geometry_job_be32(view + 64 + component * 4);
        a[component] = geometry_job_be32(view + 96 + component * 4);
        b[component] = geometry_job_be32(view + 80 + component * 4);
        for (unsigned axis = 0; axis < 3; ++axis) {
            static constexpr unsigned offset[] = {32,48,16};
            basis[axis][component] = geometry_job_be32(view + offset[axis] + component * 4);
        }
        // Job setup replaces its context object with these default columns.
        // Preserve their actual arithmetic, including zero FMAs, rather than
        // simplifying an identity matrix and changing special-value results.
        for (unsigned axis = 0; axis < 4; ++axis)
            matrix[axis][component] = geometry_job_be32(c.ls + 0xcd30 + axis * 16 + component * 4);
    }
    const v8 scale_a = _mm256_setr_epi32(a[1],a[2],a[0],0,a[1],a[2],a[0],0);
    const v8 scale_b = _mm256_setr_epi32(b[1],b[2],b[0],0,b[1],b[2],b[0],0);
    v8 projected[4];
    for (unsigned component = 0; component < 4; ++component) {
        const v8 column = _mm256_setr_epi32(basis[0][component],basis[1][component],basis[2][component],0,
            basis[0][component],basis[1][component],basis[2][component],0);
        projected[component] = v8_fma(scale_b, column, v8_fm(v8_set(origin[component]), scale_a));
        projected[component] = _mm256_blend_epi32(projected[component], v8_set(origin[component]), 0x88);
    }
    GeometryHostClipParameters params;
    for (unsigned component = 0; component < 4; ++component) {
        v8 composed = v8_fm(projected[0], v8_set(matrix[0][component]));
        for (unsigned axis = 1; axis < 4; ++axis) composed = v8_fma(projected[axis], v8_set(matrix[axis][component]), composed);
        uint32_t rows[4]; _mm_storeu_si128(reinterpret_cast<__m128i*>(rows), _mm256_castsi256_si128(composed));
        params.row[0][component] = rows[2]; params.row[1][component] = rows[0];
        params.row[2][component] = rows[1]; params.row[3][component] = rows[3];
    }
    auto integer_float = [](unsigned integer) {
        const float value = float(integer); uint32_t bits; memcpy(&bits, &value, 4); return bits;
    };
    const unsigned x = geometry_job_be16(view), y = geometry_job_be16(view + 2);
    params.minimum[0] = integer_float(x); params.minimum[1] = integer_float(y);
    params.maximum[0] = integer_float((x + geometry_job_be16(view + 4)) & 65535);
    params.maximum[1] = integer_float((y + geometry_job_be16(view + 6)) & 65535);
    const uint32_t low = geometry_job_be32(view + 8), high = geometry_job_be32(view + 12);
    const bool reversed = _mm256_movemask_epi8(v8_fcgt(v8_set(low), v8_set(high))) != 0;
    params.minimum[2] = reversed ? high : low; params.maximum[2] = reversed ? low : high;
    params.quantization_mask = format == 3 ? 0xfffffffe : 0xfffffffc;
    output = params; if (reason) *reason = nullptr; return true;
}

// Delay only exceptional-result repair until the end of this pure projection.
// The normal path keeps RZ/FZ arithmetic, operation order and signed-zero
// canonicalization after every operation. If ANY intermediate needs repair,
// discard the entire speculative block and execute the original exact path.
// No LS writes, stores to output, or guest-visible effects occur before this.
// Bounded is separate from speculation: the current job's interval proof
// already excludes exceptional intermediates, including in padded lanes.
template<bool Deferred, bool Bounded = false> struct GeometryClipArithmetic {
    v8 largest = _mm256_setzero_si256();
    inline __attribute__((always_inline)) v8 finish(__m256 value, v8 a, v8 b, v8 c, int op) {
        if constexpr (!Deferred && !Bounded) return v8_finish(value,a,b,c,op);
        const v8 bits = _mm256_castps_si256(value);
        const v8 magnitude = _mm256_and_si256(bits,v8_set(0x7fffffff));
        if constexpr (Deferred) {
#if defined(__aarch64__)
            largest = {vmaxq_u32(largest.lo,magnitude.lo),vmaxq_u32(largest.hi,magnitude.hi)};
#else
            largest = _mm256_max_epi32(largest,magnitude);
#endif
        }
        return _mm256_andnot_si256(_mm256_cmpeq_epi32(magnitude,_mm256_setzero_si256()),bits);
    }
    inline __attribute__((always_inline)) v8 fma(v8 a,v8 b,v8 c) {
        return finish(_mm256_fmadd_ps(_mm256_castsi256_ps(a),_mm256_castsi256_ps(b),_mm256_castsi256_ps(c)),a,b,c,2);
    }
    inline __attribute__((always_inline)) v8 fm(v8 a,v8 b) {
        return finish(_mm256_mul_ps(_mm256_castsi256_ps(a),_mm256_castsi256_ps(b)),a,b,a,1);
    }
    inline __attribute__((always_inline)) v8 fnms(v8 a,v8 b,v8 c) {
        return fma(_mm256_xor_si256(a,v8_set(0x80000000)),b,c);
    }
    inline __attribute__((always_inline)) bool valid() const {
#if defined(__aarch64__)
        return vmaxvq_u32(vmaxq_u32(largest.lo,largest.hi)) < 0x7f7fffff;
#else
        return _mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(v8_set(0x7f7fffff),largest))) == 255;
#endif
    }
};
template<bool Deferred, bool NativeReciprocal = false, bool Bounded = false>
inline __attribute__((always_inline)) GeometryHostSoA8 geometry_host_clip8_impl(
    const GeometryHostClipParameters& params, const GeometryHostSoA8& position)
{
    GeometryClipArithmetic<Deferred,Bounded> fp;
    auto row = [&](unsigned axis) __attribute__((always_inline)) {
        v8 value = fp.fma(position.component[0], v8_set(params.row[axis][0]), v8_set(params.row[axis][3]));
        value = fp.fma(position.component[1], v8_set(params.row[axis][1]), value);
        return fp.fma(position.component[2], v8_set(params.row[axis][2]), value);
    };
    const v8 w = row(3), sign = _mm256_and_si256(w, v8_set(0x80000000));
    v8 reciprocal;
    if constexpr (NativeReciprocal) {
        // Ordinary finite W only. Keep SPU extended floats, zero and extreme
        // exponents on the original path; native division can differ by a few
        // low bits from the SPU estimate/interpolation/refinement sequence.
        if constexpr (!Bounded) {
            const v8 magnitude = _mm256_and_si256(w, v8_set(0x7fffffff));
            const v8 ordinary = _mm256_and_si256(
                _mm256_cmpgt_epi32(magnitude, v8_set(0x1fffffff)),
                _mm256_cmpgt_epi32(v8_set(0x5f000001), magnitude));
            if (_mm256_movemask_ps(_mm256_castsi256_ps(ordinary)) != 255)
                return geometry_host_clip8_impl<Deferred, false>(params, position);
        }
#if defined(__aarch64__)
        reciprocal = {vreinterpretq_u32_f32(vdivq_f32(vdupq_n_f32(1.0f), vreinterpretq_f32_u32(w.lo))),
                      vreinterpretq_u32_f32(vdivq_f32(vdupq_n_f32(1.0f), vreinterpretq_f32_u32(w.hi)))};
#else
        reciprocal = _mm256_castps_si256(_mm256_div_ps(_mm256_set1_ps(1.0f), _mm256_castsi256_ps(w)));
#endif
    } else {
        const v8 estimate = v8_fi(w, v8_frest(w));
        reciprocal = fp.fma(fp.fnms(estimate, w, v8_set(0x3f800000)), estimate, estimate);
    }
    const v8 x = _mm256_xor_si256(sign, fp.fm(row(0), reciprocal));
    const v8 y = _mm256_xor_si256(sign, fp.fm(row(1), reciprocal));
    const v8 z = _mm256_xor_si256(sign, fp.fm(row(2), reciprocal));
    v8 flags = _mm256_and_si256(v8_fcgt(v8_set(params.minimum[2]), z), v8_set(0x80));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(v8_set(params.minimum[0]), x), v8_set(0x40)));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(v8_set(params.minimum[1]), y), v8_set(0x20)));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(x, v8_set(params.maximum[0])), v8_set(0x10)));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(y, v8_set(params.maximum[1])), v8_set(8)));
    flags = _mm256_or_si256(flags, _mm256_and_si256(v8_fcgt(z, v8_set(params.maximum[2])), v8_set(4)));
    const v8 cx = _mm256_and_si256(v8_cflts3(x), v8_set(params.quantization_mask));
    const v8 cy = _mm256_and_si256(v8_cflts3(y), v8_set(params.quantization_mask));
    v8 packed = _mm256_or_si256(_mm256_slli_epi32(cx, 16), _mm256_and_si256(cy, v8_set(65535)));
    packed = _mm256_or_si256(_mm256_and_si256(packed, v8_set(0x7fffffff)),
        _mm256_and_si256(v8_fcgt(w, _mm256_setzero_si256()), v8_set(0x80000000)));
    if constexpr (Deferred) {
        if (!fp.valid()) return geometry_host_clip8_impl<false>(params,position);
    }
    return {{x,y,_mm256_slli_epi32(flags,24),packed}};
}
inline __attribute__((always_inline)) GeometryHostSoA8 geometry_host_clip8(
    const GeometryHostClipParameters& params, const GeometryHostSoA8& position)
{
#if defined(BUZZ_GEOMETRY_NATIVE_RECIPROCAL) && BUZZ_GEOMETRY_NATIVE_RECIPROCAL
    return geometry_host_clip8_impl<false, true>(params,position);
#elif defined(BUZZ_GEOMETRY_DEFERRED_CLIP) && BUZZ_GEOMETRY_DEFERRED_CLIP
    return geometry_host_clip8_impl<true>(params,position);
#else
    return geometry_host_clip8_impl<false>(params,position);
#endif
}
template<bool ExactProjection = false>
inline bool project_geometry_host_inputs(const spu_context& c, const GeometryJobEntry& e,
    const GeometryHostInputRecipe& input, const GeometryHostInputData& data,
    GeometryHostClipData& clips, const char** reason = nullptr,
    const GeometryHostClipParameters* prepared = nullptr, bool bounded_projection = false)
{
    clips.ready = false;
    auto reject = [&](const char* why) { if (reason) *reason = why; return false; };
    if (!data.ready || input.vertices != e.vertices || !e.vertices || e.vertices > 4096 || (e.config[0] & 16))
        return reject("host clip input not ready/family");
    unsigned position = 16;
    for (unsigned field = 0; field < input.fields; ++field) if (input.field[field].semantic == 1) position = field;
    if (position == 16) return reject("host clip position semantic");
    if(data.pending_fields&(1u<<position))return reject("host clip position not captured");
    // Only the current immutable job preparation may supply admitted data.
    // Standalone callers retain their full viewport/codec validation here.
    GeometryHostClipParameters storage;
    if (!prepared && !read_geometry_host_clip_parameters(c, e, storage, reason)) return false;
    // Own the coefficients across output stores; a caller's prepared storage
    // need not be proven disjoint from every member of the output workspace.
    const GeometryHostClipParameters params = prepared ? *prepared : storage;
    if(bounded_projection && !prepared)return reject("host clip missing arithmetic admission");
    for (unsigned first = 0; first < input.vertices; first += 8) {
        const auto& position_data = data.at(input, position, first / 8);
        auto value = [&] {
            // A strict published-output audit cannot use an approximate
            // reciprocal: a one-bit coordinate change can alter culling.
            if constexpr (ExactProjection)
                return bounded_projection ? geometry_host_clip8_impl<false,false,true>(params,position_data) :
                    geometry_host_clip8_impl<false,false>(params,position_data);
            else return bounded_projection ? geometry_host_clip8_impl<false,
#if defined(BUZZ_GEOMETRY_NATIVE_RECIPROCAL) && BUZZ_GEOMETRY_NATIVE_RECIPROCAL
            true,
#else
            false,
#endif
                true>(params,position_data) : geometry_host_clip8(params,position_data);
        }();
        const v8 mask = _mm256_cmpgt_epi32(v8_set(input.vertices - first), _mm256_setr_epi32(0,1,2,3,4,5,6,7));
        for (unsigned component = 0; component < 4; ++component)
            _mm256_store_si256(reinterpret_cast<v8*>(clips.component[component] + first), _mm256_and_si256(value.component[component], mask));
    }
    clips.vertices = input.vertices; clips.ready = true; if (reason) *reason = nullptr; return true;
}
} // namespace buzz
