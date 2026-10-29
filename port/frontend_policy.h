#pragma once
#include <cstdint>
struct ppu_context;
void buzz_register_frontend_policy();
bool buzz_frontend_needs_include();
bool buzz_frontend_target_include(uint32_t lua_state);
void buzz_frontend_after_include(ppu_context*,uint32_t lua_state);
