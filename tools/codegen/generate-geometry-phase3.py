"""Build the pinned whole-driver audit and native outer-driver adapter."""
import hashlib
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / "analysis/native-geometry/phase3"
SOURCE = ROOT / "port/spu/buzz_job_6eba/spu_recomp.c"
PIN = "5368d1c2e3cdea0e6e3eb594e69cc7404b46980977aadbcbc3b5b9db6edff812"
PARENT_PIN = "0cb78a3d3170bf81921021199fcd2144272858635224f3135208f76c76113d99"


def main():
    if hashlib.sha256(SOURCE.read_bytes()).hexdigest() != PIN:
        raise SystemExit("Unreviewed image-41 lift")
    parent_generator = ROOT / "tools/codegen/generate-geometry-phase2.py"
    if hashlib.sha256(parent_generator.read_bytes()).hexdigest() != PARENT_PIN:
        raise SystemExit("Unreviewed phase-2 adapter; audit before updating the parent pin")
    subprocess.run([sys.executable, str(parent_generator)], check=True)
    parent = ROOT / "analysis/native-geometry/phase2"
    source = (parent / "spu_recomp.c").read_text()
    if "#define BUZZ_GEOMETRY_IMPLEMENTED_MASK 6015" not in source:
        raise SystemExit("Expected all eleven validated phase-2 groups")
    counts = {}
    for old, new in (("spu_wrch", "buzz_geometry_job_wrch"),
                     ("spu_rdch", "buzz_geometry_job_rdch"),
                     ("spu_rchcnt", "buzz_geometry_job_rchcnt")):
        source, counts[old] = re.subn(rf"\b{old}\(", new + "(", source)
        if not counts[old]:
            raise SystemExit(f"Missing channel sites: {old}")
    source = re.sub(r"\bSPU_IB_DISPATCH\b", "buzz_geometry_job_indirect", source)
    source = source.replace("g_spu_trampoline_fn = spu_indirect_branch;",
                            "g_spu_trampoline_fn = buzz_geometry_job_indirect_target(ctx);")
    for command_pc, hook in ((0x32c8, "prepare_commands"), (0x27a0, "publish_commands")):
        command_name = f"buzz_job_6eba_spu_func_{command_pc:08X}"
        command_pattern = rf"(?ms)^void {command_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
        if len(list(re.finditer(command_pattern, source))) != 1:
            raise SystemExit("Expected one pinned command entry")
        source = re.sub(command_pattern, lambda m:
            f"static void {command_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
            f"void {command_name}(spu_context* ctx) {{ buzz_geometry_job_{hook}(ctx, {command_name}_reference); }}",
            source, count=1)
    for command_pc, reset in ((0x20, 1), (0x100, 0)):
        command_name = f"buzz_job_6eba_spu_func_{command_pc:08X}"
        pattern = rf"(?ms)^void {command_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
        if len(list(re.finditer(pattern, source))) != 1:
            raise SystemExit("Expected one pinned binding/reset entry")
        source = re.sub(pattern, lambda m:
            f"static void {command_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
            f"void {command_name}(spu_context* ctx) {{ buzz_geometry_job_command_bindings(ctx, {command_name}_reference, {reset}); }}",
            source, count=1)
    for command_pc, hook in ((0x2e8, "emit_position"), (0x12f0, "emit_full")):
        command_name = f"buzz_job_6eba_spu_func_{command_pc:08X}"
        pattern = rf"(?ms)^void {command_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
        if len(list(re.finditer(pattern, source))) != 1:
            raise SystemExit("Expected one pinned command emitter entry")
        command_library = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in (0x32c8, 0x100, 0x20, 0x27a0))
        source = re.sub(pattern, lambda m:
            f"static const buzz_geometry_command_functions {command_name}_library = {{{command_library}}};\n"
            f"static void {command_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
            f"void {command_name}(spu_context* ctx) {{ buzz_geometry_job_{hook}(ctx, {command_name}_reference, &{command_name}_library); }}",
            source, count=1)
    stream_name = "buzz_job_6eba_spu_func_000093F0"
    stream_pattern = rf"(?ms)^void {stream_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(stream_pattern, source))) != 1:
        raise SystemExit("Expected one pinned attribute stream entry")
    source = re.sub(stream_pattern, lambda m:
        f"static void {stream_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {stream_name}(spu_context* ctx) {{ buzz_geometry_job_decode_stream(ctx, {stream_name}_reference); }}",
        source, count=1)
    attribute_name = "buzz_job_6eba_spu_func_0000AC68"
    attribute_pattern = rf"(?ms)^void {attribute_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(attribute_pattern, source))) != 1:
        raise SystemExit("Expected one pinned attribute root")
    source = re.sub(attribute_pattern, lambda m:
        f"static const buzz_geometry_attribute_functions {attribute_name}_library = {{{stream_name}, buzz_job_6eba_spu_func_000040C8}};\n"
        f"static void {attribute_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {attribute_name}(spu_context* ctx) {{ buzz_geometry_job_decode_attributes(ctx, {attribute_name}_reference, &{attribute_name}_library); }}",
        source, count=1)
    color_name = "buzz_job_6eba_spu_func_000089B0"
    color_pattern = rf"(?ms)^void {color_name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    if len(list(re.finditer(color_pattern, source))) != 1:
        raise SystemExit("Expected one pinned U8 color codec entry")
    source = re.sub(color_pattern, lambda m:
        f"static void {color_name}_reference(spu_context* ctx) {{{m[1]}}}\n"
        f"void {color_name}(spu_context* ctx) {{ buzz_geometry_job_decode_color(ctx, {color_name}_reference, buzz_job_6eba_spu_func_000083C8); }}",
        source, count=1)
    name = "buzz_job_6eba_spu_func_00001C18"
    # Retain the public library wrappers and their per-kernel switches. No
    # original main-driver block is a continuation of the native path.
    entries = (0xac68, 0xbf8, 0x9f8, 0xaea0, 0x6608, 0x2b80, 0x37c0,
               0x2dc0, 0x54e8, 0x35f0, 0x12f0, 0x2e8, 0x32c8, 0x27a0)
    library = ", ".join(f"buzz_job_6eba_spu_func_{pc:08X}" for pc in entries)
    pattern = rf"(?ms)^void {name}\(spu_context\* ctx\) \{{(.*?)^\}}"
    matches = list(re.finditer(pattern, source))
    if len(matches) != 1:
        raise SystemExit("Expected one pinned whole-driver root")
    source = re.sub(pattern, lambda m:
                    f"static const buzz_geometry_job_functions {name}_library = {{{library}}};\n"
                    f"static void {name}_native(spu_context* ctx) {{ "
                    f"buzz_geometry_job_native_start(ctx, &{name}_library); }}\n"
                    f"static void {name}_reference(spu_context* ctx) {{{m[1]}}}\n"
                    f"void {name}(spu_context* ctx) {{ "
                    f"buzz_geometry_job_run(ctx, {name}_reference, {name}_native, "
                    f"buzz_geometry_job_native_eligible); }}", source, count=1)
    support = f'#include "{(ROOT / "port/native_geometry_job.h").as_posix()}"\n'
    support += "#define BUZZ_GEOMETRY_JOB_AUDIT_AVAILABLE 1\n"
    support += "#define BUZZ_GEOMETRY_JOB_NATIVE_SETUP_AVAILABLE 1\n"
    support += "#define BUZZ_GEOMETRY_JOB_DRIVER_ABI 8\n"
    source = source.replace('#include "spu_helpers.h"', '#include "spu_helpers.h"\n' + support, 1)
    DEST.mkdir(parents=True, exist_ok=True)
    (DEST / "spu_recomp.c").write_text(source)
    (DEST / "spu_recomp.h").write_bytes((parent / "spu_recomp.h").read_bytes())
    print(f"Phase-3 reference/shadow adapter -> {DEST}; wrapped sites {counts}")


if __name__ == "__main__":
    main()
