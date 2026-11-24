"""Bind studio quality controls to the matching lifted command dispatcher."""
import hashlib
import re
import sys

# EU 1.00 and EU 1.02 produce the same control flow after linking addresses
# and alignment NOPs are removed. Hash every remaining instruction, register,
# jump-table edge and callee relationship before editing the dispatcher.
DISPATCHER = "90b82662ea67d7a01583a546bc389580831d2a719a0ed7acbd46e4b19ddd3cef"
ANCHOR = 'ctx->gpr[0] = vm_read32((ctx->gpr[9] + ctx->gpr[30]));'
SWITCH = '        switch ((uint32_t)ctx->ctr)'
TABLE_WORD = r'/\* TODO: \.word 0x([0-9A-F]{8}) \*/;'


def studio_targets(body, *, all_commands=False):
    loop = re.search(r'loc_([0-9A-F]{8}):\n        ctx->gpr\[0\] = ppc_rlw64\(ctx->gpr\[8\], 4, 0, 27, 0, 0\);', body)
    words = [int(word, 16) for word in re.findall(TABLE_WORD, body)]
    cases = {int(pc, 16) for pc in re.findall(r'case 0x([0-9A-F]{8})u: goto loc_', body)}
    if not loop or len(words) != 25 or not cases:
        return None
    # The lift emits the PPC jump table as relative words after its switch.
    table = min(cases) - min(words)
    targets = [table + word for word in words]
    if set(targets) != cases:
        return None
    labels = {int(pc, 16): str(n) for n, pc in enumerate(re.findall(r'^loc_([0-9A-F]{8}):', body, re.M))}
    if not cases.issubset(labels):
        return None
    callees = {}
    body = re.sub(r'^        /\* nop \*/;\n', '', body, flags=re.M)
    body = re.sub(TABLE_WORD, lambda m: 'table ' + labels[table + int(m[1], 16)], body)
    body = re.sub(r'(loc_|case 0x)([0-9A-F]{8})', lambda m: m[1] + labels.get(int(m[2], 16), 'unknown'), body)
    body = re.sub(r'ctx->lr = 0x[0-9A-F]{8}', 'ctx->lr = return_site', body)
    body = re.sub(r'func_[0-9A-F]{8}', lambda m: 'callee_' + str(callees.setdefault(m[0], len(callees))), body)
    body = re.sub(r'(vm_read32\(ctx->gpr\[2\] \+ )-?0x[0-9A-F]+', r'\1toc', body)
    if hashlib.sha256(body.encode('utf-8', errors='surrogateescape')).hexdigest() == DISPATCHER:
        roles = (24, 11, 21, 22, 15) if all_commands else (24, 11)
        return (int(loop[1], 16), *(targets[i] for i in roles))
    return None


def generate_studio(source_dir, out_dir, completion=None):
    matches = []
    for original in sorted(source_dir.glob('ppu_recomp_*.cpp')):
        source = completion if completion and original.name == completion.name else original
        # The lifter uses the locale encoding. Preserve legacy Windows bytes
        # while matching and editing the generated ASCII code.
        text = source.read_text(encoding='utf-8', errors='surrogateescape')
        for occurrence in re.finditer(re.escape(ANCHOR), text):
            start = text.rfind('\nvoid func_', 0, occurrence.start()) + 1
            if not start:
                continue
            end = text.index('\n}', occurrence.end()) + 2
            body = text[start:end]
            layout = studio_targets(body)
            if layout:
                matches.append((original.name, text, start + body.index(SWITCH), layout))
    if not matches:
        print("No reviewed studio dispatcher matched. Keeping the original studio commands.", file=sys.stderr)
        return None
    if len(matches) != 1:
        raise ValueError(f'Expected one reviewed studio command dispatcher, found {len(matches)}')
    name, text, pos, (loop, effect, audience) = matches[0]
    hook = f'''        if (ctx->ctr == 0x{effect:08X}u && ctx->gpr[9] == 24 * 4 &&
            buzz::skip_studio_effect(ctx->gpr[28], ctx->gpr[29], ctx->gpr[8])) {{
            ctx->gpr[8] += vm_read16(ctx->gpr[3] + 4);
            vm_write32(ctx->gpr[1] + 0x94, ctx->gpr[8]);
            goto loc_{loop:08X};
        }}
        if (ctx->ctr == 0x{audience:08X}u && ctx->gpr[9] == 11 * 4 &&
            buzz::skip_audience_light(ctx->gpr[28], ctx->gpr[29], ctx->gpr[8])) {{
            ++ctx->gpr[8];
            vm_write32(ctx->gpr[1] + 0x94, ctx->gpr[8]);
            goto loc_{loop:08X};
        }}
'''
    text = (text[:pos] + hook + text[pos:]).replace('#include "ppu_recomp.h"',
            '#include "ppu_recomp.h"\n#include "studio_effects.h"', 1)
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / name
    out.write_text(text, encoding='utf-8', errors='surrogateescape')
    return out
