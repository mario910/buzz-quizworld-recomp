#pragma once
namespace buzz_boot {
inline bool enabled(const char* value) { return value && value[0] && value[0]!='0'; }
inline bool needs_probe(unsigned index,bool trace,bool no_fmv) {
    // Omit opening/playing unsupported movies too: reporting an error alone
    // still started the decoder and could block later in its conversion queue.
    // The first menu ends bootstrap suppression; later question media must
    // use the real decoder even when startup films were skipped.
    return trace || (no_fmv && (index==5 || index==6 || index==7 || index==10 || index==17));
}
}
