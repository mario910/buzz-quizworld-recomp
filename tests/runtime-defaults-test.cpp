#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

static const char* names[] = {
#define X(name, value) #name,
#include "../port/runtime_defaults.inc"
#undef X
};
static constexpr unsigned count = sizeof(names) / sizeof(names[0]);
static char c_cached[count][128], cpp_cached[count][128];
static void capture(char values[count][128]) {
    for (unsigned i = 0; i < count; ++i) {
        const char* value = std::getenv(names[i]);
        std::snprintf(values[i], 128, "%s", value ? value : "<unset>");
    }
}
// Exercise both constructor mechanisms used by the linked runtime.
__attribute__((constructor)) static void c_constructor() { capture(c_cached); }
struct Consumer { Consumer() { capture(cpp_cached); } } consumer;

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    FILE* expected = std::fopen(argv[1], "r");
    if (!expected) return 2;
    char line[256];
    for (unsigned i = 0; i < count; ++i) {
        if (!std::fgets(line, sizeof(line), expected)) return 2;
        line[std::strcspn(line, "\r\n")] = 0;
        const char* current = std::getenv(names[i]);
        if (std::strcmp(line, c_cached[i]) || std::strcmp(line, cpp_cached[i]) ||
            std::strcmp(line, current ? current : "<unset>")) {
            std::fprintf(stderr, "%s expected '%s' C '%s' C++ '%s' main '%s'\n",
                names[i], line, c_cached[i], cpp_cached[i], current ? current : "<unset>");
            return 1;
        }
    }
    std::fclose(expected);
    for (const char* rejected : {"SPURS_POOL_SPIN_US", "SPURS_JOB_LS_ZERO", "RSX_PACK_AA_COLOR", "BUZZ_GEOMETRY_KERNELS"}) {
        if (std::getenv(rejected)) return 1;
    }
    std::printf("PASS %u defaults visible in C/C++ constructors and main; experiments remain unset\n", count);
}
