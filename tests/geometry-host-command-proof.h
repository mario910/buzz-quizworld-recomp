#pragma once
#define SPU_LS_DEBUG_HOOKS 0
#include <cassert>
#include "../port/native_geometry_full_commands.h"
namespace buzz { namespace host_command_test {
inline bool read_with_forecast_check(const spu_context& c,GeometryCommandPublication& p,
    const char** reason,const GeometryCommandPublicationForecast* forecast)
{
    // Native preparation proves this synchronous child's publication extent.
    // The original preparation callback legitimately has no forwarded proof.
    if(geometry_command_admission && geometry_command_admission->prepared)assert(forecast);
    return read_geometry_command_publication(c,p,reason,forecast);
}
}}
#define read_geometry_command_publication host_command_test::read_with_forecast_check
#include "../port/native_geometry_host_commands.h"
#undef read_geometry_command_publication
