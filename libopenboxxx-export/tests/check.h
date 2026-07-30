// SPDX-License-Identifier: GPL-2.0-or-later
// Minimal dependency-free test harness (avoids pulling a framework into the
// Phase-0 scaffold; can be swapped for Catch2/GoogleTest during Mixxx integration).
#ifndef OPENBOXXX_TESTS_CHECK_H
#define OPENBOXXX_TESTS_CHECK_H

#include <cstdio>
#include <string>
#include <vector>

namespace obxtest {

inline int& failures() { static int f = 0; return f; }

inline void report(bool ok, const char* expr, const char* file, int line) {
    if (!ok) {
        std::printf("  FAIL %s:%d  %s\n", file, line, expr);
        ++failures();
    }
}

using Fn = void (*)();
inline std::vector<std::pair<std::string, Fn>>& registry() {
    static std::vector<std::pair<std::string, Fn>> r; return r;
}
struct Reg { Reg(const char* name, Fn f) { registry().push_back({name, f}); } };

}  // namespace obxtest

#define CHECK(expr) ::obxtest::report((expr), #expr, __FILE__, __LINE__)
#define TEST(name)                                                        \
    static void name();                                                   \
    static ::obxtest::Reg reg_##name(#name, name);                        \
    static void name()

#endif  // OPENBOXXX_TESTS_CHECK_H
