#pragma once
#include <cstdint>
struct ppu_context;
enum class BuzzFrontendInclude { none, store, buzzer_test };
void buzz_register_frontend_policy();
bool buzz_frontend_needs_include();
BuzzFrontendInclude buzz_frontend_target_include(uint32_t lua_state);
void buzz_frontend_after_include(ppu_context*, uint32_t lua_state, BuzzFrontendInclude);
