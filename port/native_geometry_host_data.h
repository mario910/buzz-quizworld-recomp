/* Eight vertices in host little-endian structure-of-arrays form. The job
 * owns storage, admission and publication; these values have no SPU ABI. */
#pragma once
#include "spu_helpers.h"
#include "native_stagef_avx2.h"

namespace buzz {
struct GeometryHostSoA8 {
    v8 component[4]; // Raw host-order float bits; lane i is vertex i.
};
} // namespace buzz
