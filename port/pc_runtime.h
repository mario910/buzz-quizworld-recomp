#pragma once
#include "pc_settings.h"
#include "buzzer_input.h"

// Called before guest/worker initialization. Runtime settings are read once;
// normal play never polls a settings file. UI calls the API directly later.
bool buzz_pc_runtime_init();
buzz_pc::Preferences buzz_pc_requested_preferences();
buzz_pc::Change buzz_pc_apply_preferences(const buzz_pc::Preferences&,bool persist);
bool buzz_pc_save_input_preferences(const buzz_input::Config&,std::string& error);
