#pragma once
#include <cstdint>
// Messages come from the embedding window; shutdown is serialized against
// pending input. These hooks never consume WM_INPUT: DefWindowProc must still
// perform the OS's RawInput cleanup.
void buzz_hid_host_message(void* window,unsigned message,uintptr_t wparam,intptr_t lparam);
void buzz_hid_host_shutdown();
bool buzz_hid_host_registered();
