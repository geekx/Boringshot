#pragma once
// Minimal zero-dependency test harness: TEST(name) { CHECK(...); }

#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace bstest {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

inline void fail(const char* file, int line, const char* expr) {
    std::printf("  FAIL %s:%d: %s\n", file, line, expr);
    ++failures();
}

}  // namespace bstest

#define BS_CAT2(a, b) a##b
#define BS_CAT(a, b) BS_CAT2(a, b)
#define TEST(name)                                                              \
    static void BS_CAT(test_, name)();                                          \
    static const bstest::Registrar BS_CAT(reg_, name){#name, &BS_CAT(test_, name)}; \
    static void BS_CAT(test_, name)()

#define CHECK(expr) \
    ((expr) ? (void)0 : bstest::fail(__FILE__, __LINE__, #expr))
#define CHECK_NEAR(a, b, eps) \
    CHECK(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= (eps))
