#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* names[] = {"SPURS_POOL_SPIN_US", "SPURS_JTS_SPIN_US"};
static char c_values[2][32], cpp_values[2][32];
static void capture(char values[2][32]) {
    for (unsigned i = 0; i < 2; ++i) {
        const char* value = std::getenv(names[i]);
        std::snprintf(values[i], 32, "%s", value ? value : "<unset>");
    }
}
__attribute__((constructor)) static void c_constructor() { capture(c_values); }
struct Consumer { Consumer() { capture(cpp_values); } } consumer;
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    for (unsigned i = 0; i < 2; ++i) {
        const char* value = std::getenv(names[i]);
        if (std::strcmp(argv[i+1], c_values[i]) ||
            std::strcmp(argv[i+1], cpp_values[i]) ||
            std::strcmp(argv[i+1], value ? value : "<unset>")) {
            std::fprintf(stderr, "%s: expected=%s C=%s C++=%s main=%s\n",
                names[i], argv[i+1], c_values[i], cpp_values[i], value ? value : "<unset>");
            return 1;
        }
    }
    std::puts("PASS SPURS wait defaults before C/C++ constructors and independent explicit overrides");
}
