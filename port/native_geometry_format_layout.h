/* Host description of Edge output metadata. No SPU register bank, call frame
 * or payload conversion is needed to describe its source/destination walks. */
#pragma once
#include "native_geometry_context.h"
#include "spu_helpers.h"

namespace buzz {
struct GeometryFormatArguments { uint32_t stack, manager, descriptor; };

struct GeometryFormatMetadata {
    const spu_context& context;
    uint32_t cursor_address;
    u128 cursor;

    static unsigned byte(const u128& quad, unsigned index) {
        index &= 15;
        return (quad._u32[index / 4] >> (24 - (index % 4) * 8)) & 255;
    }
    u128 quad(uint32_t address) const {
        if ((address & 0x3fff0) == cursor_address) return cursor;
        return spu_ls_read128(&context, address);
    }
    uint32_t word(uint32_t address) const {
        const auto value = quad(address);
        uint32_t result = 0;
        for (unsigned i = 0; i < 4; ++i) result = (result << 8) | byte(value, address + i);
        return result;
    }
};

struct GeometryFormatField {
    unsigned storage = 0, width = 0, offset = 0, semantic = 0;
    uint32_t source = 0, target = 0;
    bool converted() const { return storage >= 1 && storage <= 7; }
};
struct GeometryFormatColumn { u128 cursor, limit, route; bool cross; };

class GeometryFormatLayout {
    GeometryFormatArguments arguments;
    GeometryFormatMetadata metadata;
    u128 source_lanes, destination_lanes, semantic_ids;
public:
    unsigned fields, stride, columns, source_step, destination_step;
    uint32_t vertex_bytes, destination, dummy;

    // Caller has validated metadata spans and exclusions. The sole predicted
    // metadata write publishes the cursor word, including unaligned SP rules.
    GeometryFormatLayout(const spu_context& c, GeometryFormatArguments args, const u128* pending_cursor)
        : arguments(args), metadata{c, (args.manager & ~15u) + 0x50,
              pending_cursor ? *pending_cursor : spu_ls_read128(&c, args.manager + 0x50)}
    {
        const unsigned descriptor = args.descriptor & ~15u;
        fields = c.ls[descriptor]; stride = c.ls[descriptor + 1];
        unsigned shift = 0;
        while (shift < 4 && !(stride & (1u << shift))) ++shift;
        columns = 16 >> shift; source_step = 1024 >> shift;
        destination_step = stride << (6 - shift);
        const unsigned count = geometry_job_be16(c.ls + (args.manager & ~15u) + 8);
        vertex_bytes = ((count + 3) & ~3u) * 16;
        destination = metadata.cursor._u32[2];
        dummy = args.stack - 96 - destination;
        metadata.cursor._u32[((args.stack + 4) & 12) / 4] = destination;
        source_lanes = metadata.quad(0xcf50); destination_lanes = metadata.quad(0xcf60);
        semantic_ids = metadata.quad(args.manager + 0x160);
        for (unsigned lane = 0; lane < 4; ++lane) {
            source_lanes._u32[lane] = (source_lanes._u32[lane] << (4 - shift)) + 1;
            destination_lanes._u32[lane] <<= 4 - shift;
        }
    }
    GeometryFormatField field(unsigned index) const {
        const uint32_t address = arguments.descriptor + 8 + index * 8;
        const auto bytes = metadata.quad(address);
        GeometryFormatField f;
        f.storage = GeometryFormatMetadata::byte(bytes, address + 1);
        if (!f.converted()) return f; // No dead target/pointer look-ahead.
        f.offset = GeometryFormatMetadata::byte(bytes, address);
        f.semantic = GeometryFormatMetadata::byte(bytes, address + 3);
        f.width = GeometryFormatMetadata::byte(bytes, address + 4);
        unsigned slot = 0;
        while (slot < 16 && GeometryFormatMetadata::byte(semantic_ids, slot) != f.semantic) ++slot;
        f.source = metadata.word(arguments.manager + 16 + slot * 4);
        f.target = metadata.word(0x445c + (f.storage - 1) * 4) + 0x445c;
        return f;
    }
    GeometryFormatColumn column(const GeometryFormatField& f, unsigned index) const {
        GeometryFormatColumn result;
        const unsigned advance = f.storage == 2 ? 0 : source_step;
        for (unsigned lane = 0; lane < 4; ++lane) {
            result.cursor._u32[lane] = f.source + source_lanes._u32[lane] + index * 16 + advance;
            result.limit._u32[lane] = f.source + vertex_bytes + advance;
            const unsigned low = (destination_lanes._u32[lane] + index) & 65535;
            const int32_t signed_index = low < 32768 ? int32_t(low) : int32_t(low) - 65536;
            result.route._u32[lane] = uint32_t(signed_index * int32_t(stride)) + f.offset;
        }
        result.cross = (result.route._u32[0] & 15) + f.width > 16;
        return result;
    }
};
} // namespace buzz
