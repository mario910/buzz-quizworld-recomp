#pragma once
// Where the game's own threads run on a big.LITTLE Android device, and how
// many SPURS list workers they are. Pure: the caller reads the capacities and
// applies the result, so this is testable off the device.
//
// Measured on an AYN Thor (Snapdragon 8 Gen 2: three 2.0 GHz efficiency
// cores, four 2.8 GHz performance cores, one prime core), main menu, same
// build: heavy threads allowed on CPUs 0-4 with four list workers 6.5 FPS;
// performance cores only with three workers 9.6 FPS, at the same temperature.
// A job that lands on an efficiency core takes about three times as long, and
// the frame waits for the slowest lane. Leaving those cores to Android also
// keeps the system (and adb) responsive while the game saturates the rest.
#include <cstddef>
#include <cstdint>

namespace buzz_android {
struct CpuPolicy {
    uint64_t mask = 0;        // CPUs for the game's native threads; 0 = leave as is
    unsigned workers = 0;     // SPURS list workers; 0 = leave the default
};

// `capacity[i]` is CPU i's cpu_capacity from sysfs (0 = unknown/offline); only
// ratios matter. Not its maximum frequency: an efficiency core's clock is
// much closer to a performance core's than its speed is. At most 64 CPUs.
inline CpuPolicy choose_cpu_policy(const unsigned* capacity, size_t count)
{
    CpuPolicy policy;
    if (!capacity || count < 2 || count > 64) return policy;
    unsigned best = 0;
    for (size_t i = 0; i < count; ++i) if (capacity[i] > best) best = capacity[i];
    if (!best) return policy;
    // A performance core has at least 60% of the fastest core's capacity: the
    // Thor's are 4 x ~0.85 and 1.0, its efficiency cores ~0.35. A missing value
    // counts as an efficiency core rather than being guessed fast.
    unsigned fast = 0;
    uint64_t mask = 0;
    for (size_t i = 0; i < count; ++i)
        if (uint64_t(capacity[i]) * 10 >= uint64_t(best) * 6) { mask |= uint64_t(1) << i; ++fast; }
    // Homogeneous CPUs: nothing to choose. Fewer than three performance
    // cores: the game cannot live on them alone, so keep every core.
    if (fast == count || fast < 3) return policy;
    policy.mask = mask;
    // Lanes share those cores with the audio SPU, the RSX worker, the PPU
    // threads and the object chain: one lane per core up to five cores (the
    // measured Thor configuration is three), the default four beyond.
    policy.workers = fast <= 5 ? 3 : 4;
    return policy;
}
}
