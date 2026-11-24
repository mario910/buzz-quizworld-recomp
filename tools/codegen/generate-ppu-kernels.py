"""Insert native kernels into private EU 1.00 translations with audited function bodies."""
import argparse
from hashlib import sha256
from pathlib import Path
import re
from tempfile import NamedTemporaryFile


SEARCH = "002B6EF0"
SLOTS = "002B6FC0"
MATRIX = "002CBB58"
OBJECT = "00283028"
UNPACK = "0028FCA0"
WEIGHTED = "002FBBD0"
PLANES = "002C6444"
PACK = "00287AD0"
COLLECT = "00288118"
STREAM = "0028A610"
FILTER = "002C0244"
TRACKS = "002DD350"
SCENE_JOB = "0028876C"
POSE = "002836F0"
COMMAND = "00293000"
DMA_RANGES = "002B6E20"
ANIM_SLOT = "002DDAA0"
BASIS_SETUP = "0028DB60"
BASIS_TAIL = "0028DEAC"
ROTATION = "002BCC18"
COLUMNS = (BASIS_SETUP, BASIS_TAIL, UNPACK, ROTATION)
COLUMN_GROUPS = {"none": (), "matrices": (BASIS_SETUP, BASIS_TAIL, UNPACK),
                 "rotations": (ROTATION,), "all": COLUMNS}
SCENE = (SCENE_JOB, COMMAND, DMA_RANGES, ANIM_SLOT)
JOBS = (PACK, COLLECT, STREAM, FILTER, TRACKS)
PERMUTE_COUNTS = {"002719D0": 4, "0028F930": 3, "0028D870": 3, "002CACB8": 25, "002CF700": 4}
DIGESTS = {
    "002719D0": "b53d2a999097434b84b2285955226572eeb37d4f69779f06251241aca3c0866e",
    "0028F930": "d4b63851603af394d92d0725f83b97cf916682299670b63754fa85ef47cb78fd",
    "0028D870": "6a02c28397426ecf57ea82e3b611b7684a0a5bea3b11f434f7d6fcf25d4f94a4",
    "002CACB8": "ac5ee33d80ec9e8815c394d6fc16f99324064618a5246debcce3993745c6b29b",
    "002CF700": "0d692315c7377106d78ec3f84c417f302d0c7b5a1539eba5e7cdeedaa9ebaa76",
    BASIS_SETUP: "17f692ee42f3e2c00ca310bb88eb6d05a00880a49ecbaf2da54676fe1fdd9f63",
    BASIS_TAIL: "5a5b2cd7f955936e53209d4c9767943f54c02eae3da5e0c48d8a5617c52d8d25",
    ROTATION: "765ee3e1e46f46d957ca45f52aa1c155adedcfc181e033d312c01f941cafed3f",
    SCENE_JOB: "d0c7c909419dd6d0f759deb8b2231e6dc9939534d09e631c71d95ca7ccd3497d",
    POSE: "58bab97f69478fc7a34213b814271b922ac251943d23534719ab32486b335bd2",
    COMMAND: "ddfcb87d2c3643cf1f9c492d3d3eccc56d45c92ecaef350f9f5bb386deda5ae9",
    DMA_RANGES: "727b6ed529fc04c102047f8639f2dcbfe8f760a4719c28b30b9bfa282d3f5290",
    ANIM_SLOT: "f9231ff327931a8c980ac327f7c9628d8f3d3bf372c21a56e15ee850a98de019",
    PACK: "f9864f411bf7627cc01bdebde2c8dd8e6aff63211ad2ac88d8dd2fdb33dc3ddb",
    COLLECT: "79a8d74bbdd5400fc39d8620b447820b0ec27107224f6219d6326fe0560252b2",
    STREAM: "8280a8abaf576b4b7ccc5acc1ea9525f55c3bbfa069218b258d7f875715265a4",
    FILTER: "320fabe372d1863af16959cb96735b432f6894c3451b0aa76da986eb68bf525d",
    TRACKS: "f78e37fa4c02998dc4c45a8cd9187d4dde7c6faf2830813035b3416a66d07b12",
    PLANES: "5793c3ccb6b0de78dcdcca49bc2d0221e0dfb81011180959870b382cb822786a",
    WEIGHTED: "a93dd254a290d5177a6873934fb848d3159c498e1411d5cf39d4e09c61a51d4e",
    UNPACK: "b8358a59746b78963ae1bc50218f232b039b9d54b20c94a2bbbbb6ee2c121c91",
    OBJECT: "2f071e466b0f4464e2334f7a8c5263411c46c3a44ffcc66d2c6a45a7ee736eae",
    SEARCH: "067ea1a54ee3ab80cb818b8b211642b9578d54ee5e11b4f243a29b0689da4644",
    SLOTS: "831d32584933cc371ba2aa5e531c818bb80c60e4543236d82a44fb05d6d3c2ff",
    MATRIX: "11e53a9c9838022a1a2c13c88b22a17b797a511b602a6991832f658dedd8a695",
}
FIRST_LOAD = "        { uint64_t ea = (ctx->gpr[9]) & ~0xFULL; vm_raw_ld16"
FIRST_STORE = "        { uint64_t ea = ((ctx->gpr[10] + ctx->gpr[0])) & ~0xFULL; vm_raw_st16"
SLOT_BRANCH = "        if ((!((ctx->cr >> 2)"
SLOT_CONTINUATION = "loc_002B7054:"
MATRIX_ISA = "defined(__FMA__) && defined(__SSSE3__) && defined(__SSE4_1__)"


def function(source, address):
    return re.search(r"^void func_" + address + r"\(ppu_context\* ctx\) \{\n.*?^\}",
                     source, re.MULTILINE | re.DOTALL)


def transform_span(original, address, bind=str):
    if address == POSE:
        start = original.index("        vmx_vmaddfp(&ctx->vr[5], &ctx->vr[27], &ctx->vr[11], &ctx->vr[5]")
        end = original.index("        ctx->gpr[9] = vm_read32(ctx->gpr[31] + 0x0);", start)
        return start, end, "ppu_finish_pose_rows"
    if address == PLANES:
        start = original.index("        vmx_vnmsubfp(&ctx->vr[10], &ctx->vr[10], &ctx->vr[7], &ctx->vr[8]")
        end = original.index("        (void)vmx_vcmpeqfp(&ctx->vr[3], &ctx->vr[10], &ctx->vr[10])", start)
        return start, end, "ppu_refine_plane_lengths"
    if address == OBJECT:
        start = original.index("\n", original.index(bind("loc_00283128:"))) + 1
        end = original.index("        { uint64_t ea = ((ctx->gpr[9] + ctx->gpr[10])) & ~0xFULL; vm_raw_st16", start)
        return start, end, "ppu_object_transform"
    if address == UNPACK:
        start = original.index("        { uint8_t* a = (uint8_t*)&ctx->vr[13]; uint8_t* b = (uint8_t*)&ctx->vr[0];")
        end = original.index("        { uint32_t* d = (uint32_t*)&ctx->vr[11];", start)
        return start, end, "ppu_unpack_transform_rows"
    start = original.index("\n", original.index("(ctx->gpr[7]) & ~0xFULL; vm_raw_st16")) + 1
    end = original.index("        { uint32_t tmp = vm_read32(ctx->gpr[1] + 0x88);", start)
    return start, end, "ppu_accumulate_weighted_rows"


def column_spans(body, address):
    if address in (BASIS_SETUP, BASIS_TAIL):
        sections = [("vmx_vmaddfp(&ctx->vr[16], &ctx->vr[12], &ctx->vr[10], &ctx->vr[16]",
                     "{ uint64_t ea = ((ctx->gpr[7] + ctx->gpr[0])) & ~0xFULL; vm_raw_st16",
                     "ppu_finish_basis_columns")]
    elif address == UNPACK:
        sections = [("vmx_vmaddfp(&ctx->vr[10], &ctx->vr[17], &ctx->vr[8], &ctx->vr[10]",
                     "{ uint64_t ea = ((ctx->gpr[1] + ctx->gpr[14])) & ~0xFULL; vm_raw_st16",
                     "ppu_transpose_pose_columns")]
    else:
        sections = [("{ uint8_t* a = (uint8_t*)&ctx->vr[11]; uint8_t* b = (uint8_t*)&ctx->vr[7];",
                     "{ uint64_t ea = ((ctx->gpr[6] + ctx->gpr[0])) & ~0xFULL; vm_raw_ld16",
                     "ppu_shuffle_rotation_candidates"),
                    ("{ uint64_t* d=(uint64_t*)&ctx->vr[13]; uint64_t* a=(uint64_t*)&ctx->vr[13];",
                     "{ uint64_t ea = ((ctx->gpr[3] + ctx->gpr[0])) & ~0xFULL; vm_raw_st16",
                     "ppu_finish_rotation_candidates")]
    for first, last, kernel in sections:
        start = body.index("        " + first)
        yield start, body.index("        " + last, start), kernel


def guarded_simd_block(reference, kernel):
    return (f"#if {MATRIX_ISA}\n        if ((_mm_getcsr() & 0x1f80) == 0x1f80) {{\n"
            f"            buzz::{kernel}(*ctx);\n        }} else\n#endif\n        {{\n"
            + reference + "        }\n")


def job_span(original, address, bind=str):
    regions = {
        SCENE_JOB: ("loc_00288A48:", "        ctx->lr = 0x00288B30;", "buzz::ppu_finish_scene_job(*ctx);"),
        COMMAND: ("loc_00293078:", "        switch ((uint32_t)ctx->ctr)", "buzz::ppu_scene_command(*ctx);"),
        DMA_RANGES: ("loc_002B6E48:", "        ctx->gpr[28] = _cs_28;", "buzz::ppu_object_dma_ranges(*ctx);"),
        ANIM_SLOT: ("loc_002DDAA0:", "        ctx->gpr[4] = ppc_rldicl(ctx->gpr[10], 0, 32);",
                    "if (!buzz::ppu_find_animation_slot(*ctx)) goto loc_002DDBB8;"),
        PACK: ("        ctx->gpr[29] = vm_read32(ctx->gpr[5] + 0x0);",
               "        ctx->gpr[3] = vm_read32(ctx->gpr[3] + 0x0);", "buzz::ppu_pack_job_slots(*ctx);"),
        STREAM: ("        ctx->gpr[5] = ppc_rlw64(ctx->gpr[27], 4, 0, 27, 0, 0);",
                 "        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xD0);", "buzz::ppu_pack_stream_job(*ctx);"),
        COLLECT: ("loc_00288160:", "        { int64_t a = (int32_t)ctx->gpr[29];",
                  "buzz::ppu_collect_job_objects(*ctx);"),
        FILTER: ("loc_002C02F0:", "        ctx->gpr[0] = vm_read8(ctx->gpr[6] + 0x114);",
                 """switch (buzz::ppu_filter_jobs(*ctx)) {
            case buzz::PpuJobFilter::special: goto loc_002C0404;
            case buzz::PpuJobFilter::finished: goto loc_002C0384;
            case buzz::PpuJobFilter::accepted: break;
            }"""),
        TRACKS: ("loc_002DD3B0:", "        ctx->gpr[0] = vm_read32(ctx->gpr[28] + 0x174);",
                 "if (buzz::ppu_advance_tracks(*ctx)) goto loc_002DD7E4;"),
    }
    first, last, call = map(bind, regions[address])
    start = original.index(first)
    if first.startswith("loc_"):
        start += len(first) + 1
    return start, original.index(last, start), call


def adapt_job(original, address, condition=None, bind=str):
    start, end, call = job_span(original, address, bind)
    if address == COLLECT:
        condition = f"({condition}) && !g_trampoline_fn" if condition else "!g_trampoline_fn"
    head = f"if ({condition}) " if condition else ""
    join = "native_done_" + address
    # Other lifted edges can enter the middle of a region (e.g. after an expiry callback).
    return (original[:start] + f"        {head}{{\n            {call}\n            goto {join};\n        }}\n"
            + original[start:end] + join + ":\n" + original[end:])


def adapt_existing(original, address, bind=str):
    if address in JOBS or address in SCENE:
        return adapt_job(original, address, bind=bind)
    if address in (OBJECT, UNPACK, WEIGHTED, PLANES, POSE):
        start, end, kernel = transform_span(original, address, bind)
        if address == UNPACK:
            block = f"#if {MATRIX_ISA}\n        buzz::{kernel}(*ctx);\n#else\n{original[start:end]}#endif\n"
        else:
            block = guarded_simd_block(original[start:end], kernel)
        return original[:start] + block.replace(f"buzz::{kernel}(*ctx);", bind(f"buzz::{kernel}(*ctx);")) + original[end:]
    if address == SEARCH:
        return original[:original.index("{") + 1] + "\n    buzz::ppu_record_search(*ctx);\n}"
    if address == SLOTS:
        return original.replace(SLOT_BRANCH, bind("""        if (ctx->gpr[5] <= 4) {
            buzz::ppu_prepare_slots(*ctx);
            goto loc_002B7054;
        }
""") + SLOT_BRANCH)
    start = original.index(FIRST_LOAD)
    end = original.index(FIRST_STORE)
    # Keep the lifted prologue, output stores, and continuation for both paths.
    return (original[:start] + f"#if {MATRIX_ISA}\n"
            "        if ((_mm_getcsr() & 0x1f80) == 0x1f80) {\n"
            "            buzz::ppu_compose_matrix(*ctx);\n"
            "        } else\n#endif\n        {\n" + original[start:end] +
            "        }\n" + original[end:])


def permute_regions(original):
    pattern = (r'        \{ uint8_t\* a = \(uint8_t\*\)&ctx->vr\[(\d+)\]; uint8_t\* b = \(uint8_t\*\)&ctx->vr\[(\d+)\]; '
               r'uint8_t\* c = \(uint8_t\*\)&ctx->vr\[(\d+)\]; uint8_t\* d = \(uint8_t\*\)&ctx->vr\[(\d+)\]; '
               r'uint8_t tmp\[16\]; for \(int i = 0; i < 16; i\+\+\) \{ uint8_t sel = c\[i\] & 0x1F; '
               r'tmp\[i\] = \(sel < 16\) \? a\[sel\] : b\[sel - 16\]; \} memcpy\(d, tmp, 16\); \}')
    return list(re.finditer(pattern, original))


def adapt_permutations(original, address):
    regions = permute_regions(original)
    if len(regions) != PERMUTE_COUNTS[address]:
        raise ValueError(f"Unexpected VPERM schedule at {address}")
    for site in reversed(regions):
        a, b, c, dest = site.groups()
        same = "true" if a == b else "false"
        call = f"        buzz::ppu_permute_bytes<{same}>(ctx->vr[{dest}], ctx->vr[{a}], ctx->vr[{b}], ctx->vr[{c}]);"
        original = original[:site.start()] + f"#if {MATRIX_ISA}\n{call}\n#else\n{site[0]}\n#endif" + original[site.end():]
    return original


def adapt(original, address, columns=COLUMNS):
    if address in PERMUTE_COUNTS:
        return adapt_permutations(original, address)
    body = original
    if address not in (BASIS_SETUP, BASIS_TAIL, ROTATION):
        body = adapt_existing(body, address)
    if address in columns:
        for start, end, kernel in reversed(list(column_spans(body, address))):
            body = body[:start] + guarded_simd_block(body[start:end], kernel) + body[end:]
    return body


def generate(source, addresses, header, columns):
    applied = []
    for address in addresses:
        if address in (BASIS_SETUP, BASIS_TAIL, ROTATION) and address not in columns:
            continue
        match = function(source, address)
        if not match or sha256(match[0].encode()).hexdigest() != DIGESTS[address]:
            print(f"Keeping reference translation for 0x{address} (unrecognized body)")
            continue
        source = source[:match.start()] + adapt(match[0], address, columns) + source[match.end():]
        applied.append(address)
    if applied:
        anchor = '#include "ppu_vmx.h"'
        include = f'#include "{header}"'
        if header != "native_ppu_records.h":
            include = f"#if {MATRIX_ISA}\n{include}\n#endif"
        if any(address in PERMUTE_COUNTS for address in applied):
            include += f'\n#if {MATRIX_ISA}\n#include "native_ppu_permute.h"\n#endif'
        if any(address in columns for address in applied):
            include += f'\n#if {MATRIX_ISA}\n#include "native_ppu_columns.h"\n#endif'
        if any(address in JOBS for address in applied):
            include += '\n#include "native_ppu_jobs.h"'
        if any(address in SCENE for address in applied):
            include += '\n#include "native_ppu_scene.h"'
        if source.count(anchor) != 1:
            raise ValueError("Cannot locate the lifted VMX helpers")
        source = source.replace(anchor, anchor + "\n" + include)
    return source


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("translations", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ppu-columns", choices=COLUMN_GROUPS, default="none")
    args = parser.parse_args()
    args.output.mkdir(exist_ok=True, parents=True)
    records = (args.translations / "ppu_recomp_015.cpp").read_text()
    search = function(records, SEARCH)
    collect = (COLLECT,) if search and sha256(search[0].encode()).hexdigest() == DIGESTS[SEARCH] else ()
    for chunk, addresses, header in [
        ("014", (OBJECT, UNPACK, PACK, STREAM, SCENE_JOB, POSE, BASIS_SETUP, BASIS_TAIL, "002719D0", "0028F930", "0028D870") + collect, "native_ppu_transform.h"),
        ("017", (WEIGHTED,), "native_ppu_transform.h"),
        ("015", (SEARCH, SLOTS, DMA_RANGES, COMMAND, ROTATION), "native_ppu_records.h"),
        ("016", (MATRIX, PLANES, FILTER, TRACKS, ANIM_SLOT, "002CACB8", "002CF700"), "native_ppu_transform.h"),
    ]:
        name = f"ppu_recomp_{chunk}.cpp"
        output = args.output / name
        generated = generate((args.translations / name).read_text(), addresses, header, COLUMN_GROUPS[args.ppu_columns])
        if output.exists() and output.read_text() == generated:
            continue
        with NamedTemporaryFile(mode="w", dir=args.output, delete=False) as temp:
            temp.write(generated)
        Path(temp.name).replace(output)


if __name__ == "__main__":
    main()
