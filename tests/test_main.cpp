#include <cstring>

#include "bstest.h"

// Usage: boringshot_tests [substring-filter]
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0;
    for (const auto& c : bstest::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = bstest::failures();
        c.fn();
        std::printf("%s %s\n", bstest::failures() == before ? "[ ok ]" : "[FAIL]", c.name);
        ++ran;
    }
    std::printf("\n%d tests, %d failed checks\n", ran, bstest::failures());
    return (ran == 0 || bstest::failures() != 0) ? 1 : 0;
}
