#pragma once

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Keep a disconnected/occluded game eligible for HighQoS without changing
// process priority, affinity, timer policy or the system power plan. Opt-in:
// battery/thermal behavior and other Windows versions still need qualification.
inline void buzz_configure_windows_qos()
{
    const char* option = std::getenv("BUZZ_WINDOWS_HIGH_QOS");
    if (!option || std::strcmp(option, "1") != 0) {
        std::fprintf(stderr, "[boot] Windows HighQoS=system (BUZZ_WINDOWS_HIGH_QOS=1 to opt in)\n");
        return;
    }
    // Resolve at runtime so unsupported Windows versions retain OS policy.
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto set_info = kernel ? reinterpret_cast<decltype(&SetProcessInformation)>(
        GetProcAddress(kernel, "SetProcessInformation")) : nullptr;
    if (!set_info) {
        std::fprintf(stderr, "[boot] Windows HighQoS unavailable; using OS policy\n");
        return;
    }
    PROCESS_POWER_THROTTLING_STATE policy{};
    policy.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    policy.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    // Control the execution-speed mechanism and explicitly disable throttling.
    // https://learn.microsoft.com/windows/win32/api/processthreadsapi/nf-processthreadsapi-setprocessinformation
    if (!set_info(GetCurrentProcess(), ProcessPowerThrottling, &policy, sizeof(policy))) {
        std::fprintf(stderr, "[boot] Windows HighQoS failed (error %lu); using OS policy\n",
                     static_cast<unsigned long>(GetLastError()));
        return;
    }
    std::fprintf(stderr, "[boot] Windows HighQoS=requested (execution throttling disabled)\n");
}
#endif
