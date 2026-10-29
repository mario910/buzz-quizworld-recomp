"""Keep the exact reference lift; substitute reviewed completion/selector bodies only."""
from pathlib import Path
import re
import hashlib
import json

ROOT = Path(__file__).resolve().parent
PROFILES = (
    ("102", "015", "0026A8A0", "172c4c7ef2a5068e7a05c692d593b065c13f800e7f4bee3811582fc4a57e32b4",
     "0026BEEC", "98e80993997fb4424bb2755b3c8dd3cc4e7d39e4b9cf92dba4b0f57e34d474c1", "0026BF68", "0026BFB8"),
    ("100", "013", "00268108", "3ab70346a51189e6cb35b9959235a26175e33406c0013748859f693e5544862b",
     "002699F4", "c2109892a32d81f5d568829644ea4b6e23425b31e03fcca25938e3f1b9f7a032", "00269A74", "00269AC4"),
)


def generate(root=ROOT):
    candidates = []
    for profile in PROFILES:
        version, chunk, entry, pin, selector, selector_pin, wait, ready = profile
        path = root / f"port/recompiled/ppu_recomp_{chunk}.cpp"
        if not path.exists():
            continue
        original = path.read_text()
        match = re.search(rf"(?ms)^void func_{entry}\(ppu_context\* ctx\) \{{.*?^\}}", original)
        if match:
            if hashlib.sha256(match.group().encode()).hexdigest() != pin:
                raise ValueError(f"Completion body differs from reviewed {version} profile")
            candidates.append((profile, original, match, path.name))
    if len(candidates) != 1:
        raise ValueError("Expected exactly one reviewed completion profile")
    (version, chunk, entry, pin, selector, selector_pin, wait, ready), original, match, filename = candidates[0]
    reference = match.group().replace(f"func_{entry}(", "buzz_completion_reference(", 1)
    preamble = "#define BUZZ_COMPLETION_DISC100 1\n" if version == "100" else ""
    replacement = preamble + reference + '''
#include "../../port/native_completion.h"
#include "../../port/native_slot_queue.h"
extern "C" int ps3_spurs_try_busy_native(uint32_t);
void BUZZ_COMPLETION_FN(ppu_context* ctx) {
    static const int mode = [] { const char* e = getenv("BUZZ_NATIVE_COMPLETION");
        int value = e ? atoi(e) : 0;
        fprintf(stderr, "[native-completion] mode=%d\\n", value); return value; }();
    if (mode == 2 && buzz_native_completion_busy(ctx, ps3_spurs_try_busy_native)) return;
    if (mode) buzz_native_completion(ctx); else buzz_completion_reference(ctx);
}
'''
    output = original[:match.start()] + replacement + original[match.end():]
    selector_match = re.search(rf"(?ms)^void func_{selector}\(ppu_context\* ctx\) \{{.*?^\}}", output)
    if not selector_match or hashlib.sha256(selector_match.group().encode()).hexdigest() != selector_pin:
        raise ValueError(f"Slot selector differs from reviewed {version} profile")
    anchor = f"loc_{wait}:\n"
    if selector_match.group().count(anchor) != 1:
        raise ValueError("Ambiguous completion queue insertion")
    queued = selector_match.group().replace(anchor, anchor + f"        if (buzz_wait_completed_slot(ctx)) goto loc_{ready};\n")
    output = output[:selector_match.start()] + queued + output[selector_match.end():]
    dest = root / "analysis/native-completion"
    dest.mkdir(parents=True, exist_ok=True)
    (dest / filename).write_text(output)
    (dest / "reference.inc").write_text(reference)
    (dest / "profile.json").write_text(json.dumps(dict(version=version, source=filename)))
    print(f"Generated checked {version} completion/queue adapter: {dest / filename}")


if __name__ == "__main__":
    generate()
