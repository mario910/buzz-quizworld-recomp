/* Pure MFC argument writes. Commands, waits and notifications use the SDK. */
#pragma once
#include "spu_context.h"

namespace buzz {
inline bool set_geometry_mfc_argument(spu_context& context, uint32_t channel, uint32_t value)
{
    switch (channel) {
    case MFC_LSA: context.mfc_lsa = value; return true;
    case MFC_EAH: context.mfc_eah = value; return true;
    case MFC_EAL: context.mfc_eal = value; return true;
    case MFC_Size: context.mfc_size = value; return true;
    case MFC_TagID: context.mfc_tag = value & 31u; return true;
    default: return false;
    }
}
} // namespace buzz
