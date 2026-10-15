/**
 * @file test_framework.h
 * @brief Minimal test registry + CHECK macros shared by the host test files
 */

#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <string>
#include <vector>

// ============================================================
// Tiny test framework
// ============================================================

inline int g_checks = 0;
inline int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failures++;                                                    \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        g_checks++;                                                          \
        const auto _va = (a);                                                \
        const auto _vb = (b);                                                \
        if (!(_va == _vb)) {                                                 \
            g_failures++;                                                    \
            printf("  FAIL %s:%d: %s == %s (%lld vs %lld)\n", __FILE__,      \
                   __LINE__, #a, #b, (long long)_va, (long long)_vb);        \
        }                                                                    \
    } while (0)

struct TestCase {
    const char* name;
    void (*fn)();
};
inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};
#define TEST(name)                                  \
    static void name();                             \
    static Registrar reg_##name(#name, name);       \
    static void name()

namespace fs = std::filesystem;

inline std::string tempDir(const char* name) {
    fs::path p = fs::temp_directory_path() / "esp32ai_host_tests" / name;
    fs::remove_all(p);
    fs::create_directories(p);
    return p.string();
}

inline size_t countLines(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;
    size_t n = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (c == '\n') n++;
    }
    fclose(f);
    return n;
}

