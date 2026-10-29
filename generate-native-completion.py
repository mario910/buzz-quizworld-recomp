"""Keep the reference lift and substitute the audited polling function only."""
from pathlib import Path
import re
import hashlib
ROOT = Path(__file__).resolve().parent
original = (ROOT / "port/recompiled/ppu_recomp_015.cpp").read_text()
pattern = r"(?ms)^void func_0026A8A0\(ppu_context\* ctx\) \{.*?^\}"
match = re.search(pattern, original)
if not match:
    raise ValueError("Missing completion function")
if hashlib.sha256(match.group().encode()).hexdigest() != "172c4c7ef2a5068e7a05c692d593b065c13f800e7f4bee3811582fc4a57e32b4":
    raise ValueError("Completion function differs from the audited 1.02 body")
reference = match.group().replace("func_0026A8A0(", "buzz_completion_reference(", 1)
replacement = reference + '''
#include "../../port/native_completion.h"
#include "../../port/native_slot_queue.h"
extern "C" int ps3_spurs_try_busy_native(uint32_t);
void func_0026A8A0(ppu_context* ctx) {
    static const int mode = [] { const char* e = getenv("BUZZ_NATIVE_COMPLETION");
        int value = e ? atoi(e) : 0;
        fprintf(stderr, "[native-completion] mode=%d\\n", value); return value; }();
    // These two audited continuations use only r3 and callee-saved state.
    // A BUSY native return needs no emulated temporary guest stack frame.
    // Ready/unknown/error paths retain the exact reference ABI and consumer.
    if (mode == 2 && buzz_native_completion_busy(ctx, ps3_spurs_try_busy_native)) return;
    if (mode) buzz_native_completion(ctx); else buzz_completion_reference(ctx);
}
'''
dest = ROOT / "analysis/native-completion"
dest.mkdir(exist_ok=True)
output = original[:match.start()] + replacement + original[match.end():]
selector = re.search(r"(?ms)^void func_0026BEEC\(ppu_context\* ctx\) \{.*?^\}", output)
if not selector or hashlib.sha256(selector.group().encode()).hexdigest() != "98e80993997fb4424bb2755b3c8dd3cc4e7d39e4b9cf92dba4b0f57e34d474c1":
    raise ValueError("Slot selector differs from the audited 1.02 body")
queued = selector.group().replace("loc_0026BF68:\n", "loc_0026BF68:\n        if (buzz_wait_completed_slot(ctx)) goto loc_0026BFB8;\n", 1)
output = output[:selector.start()] + queued + output[selector.end():]
(dest / "ppu_recomp_015.cpp").write_text(output)
(dest / "reference.inc").write_text(reference)
print(f"Generated opt-in native completion adapter: {dest}")
