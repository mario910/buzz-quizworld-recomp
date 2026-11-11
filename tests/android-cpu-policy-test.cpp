#include "../port/android_cpu_policy.h"
#include <cstdio>
#include <cstdlib>
static void check(bool value) { if(!value)std::abort(); }
int main() {
    using buzz_android::choose_cpu_policy;
    {   // AYN Thor / Snapdragon 8 Gen 2 (its real cpu_capacity values):
        // 3 efficiency, 4 performance, 1 prime -> the four performance cores.
        const unsigned capacity[]={280,280,280,855,855,855,855,1024};
        const auto p=choose_cpu_policy(capacity,8);
        check(p.mask==0x78 && p.workers==3);
    }
    {   // Maximum frequencies are not capacities: the Thor's 2.0 GHz efficiency
        // cores are 63% of its prime core's clock but a third of its speed.
        // The caller must pass cpu_capacity; with frequencies nothing is safe
        // to conclude here, and this input (wrongly) keeps every core.
        const unsigned frequency[]={2016000,2016000,2016000,2803200,2803200,2803200,2803200,3187200};
        check(choose_cpu_policy(frequency,8).mask==0);
    }
    {   // Snapdragon 8 Elite: six performance cores and two prime, no slow cores.
        const unsigned capacity[]={800,800,800,800,800,800,1024,1024};
        const auto p=choose_cpu_policy(capacity,8);
        check(p.mask==0 && p.workers==0);
    }
    {   // Homogeneous.
        const unsigned capacity[]={1024,1024,1024,1024};
        const auto p=choose_cpu_policy(capacity,4);
        check(p.mask==0 && p.workers==0);
    }
    {   // 4 + 3 + 1: the prime core would leave three; all four are kept.
        const unsigned capacity[]={300,300,300,300,760,760,760,1024};
        const auto p=choose_cpu_policy(capacity,8);
        check(p.mask==0xF0 && p.workers==3);
    }
    {   // 2 + 6: two fast cores are not enough to hold the game.
        const unsigned capacity[]={400,400,400,400,400,400,1024,1024};
        const auto p=choose_cpu_policy(capacity,8);
        check(p.mask==0 && p.workers==0);
    }
    {   // Five performance cores beside the prime: three workers.
        const unsigned capacity[]={300,300,900,900,900,900,900,1024};
        const auto p=choose_cpu_policy(capacity,8);
        check(p.mask==0x7C && p.workers==3);
    }
    {   // Two equal top cores are not a lone prime; six fast cores, four workers.
        const unsigned capacity[]={300,300,900,900,900,900,1024,1024};
        const auto p=choose_cpu_policy(capacity,8);
        check(p.mask==0xFC && p.workers==4);
    }
    {   // Unknown capacities are never treated as fast; all unknown = no policy.
        const unsigned capacity[]={0,0,0,870,870,870,870,1024};
        check(choose_cpu_policy(capacity,8).mask==0x78);
        const unsigned none[]={0,0,0,0};
        check(choose_cpu_policy(none,4).mask==0);
        check(choose_cpu_policy(nullptr,8).mask==0);
        check(choose_cpu_policy(capacity,1).mask==0);
    }
    std::puts("PASS Android CPU policy: performance-core mask and worker count");
}
