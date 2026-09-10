// The whole framework. A test is a function that registers itself, and a check
// records a failure and keeps going, so one bad assertion does not hide the
// rest of the file.
#pragma once

#include <cmath>
#include <string>
#include <vector>

struct TestCase {
    const char *name;
    void (*fn)();
};

void registerTest(const char *name, void (*fn)());
void reportFailure(const char *file, int line, const char *expression, const std::string &detail);

#define TEST(name)                                                                                 \
    static void name();                                                                            \
    namespace {                                                                                    \
    struct name##_registrar {                                                                      \
        name##_registrar() {                                                                       \
            registerTest(#name, &name);                                                            \
        }                                                                                          \
    } name##_registrar_instance;                                                                   \
    }                                                                                              \
    static void name()

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            reportFailure(__FILE__, __LINE__, #expression, std::string());                          \
        }                                                                                          \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                 \
    do {                                                                                           \
        auto actualValue = (actual);                                                               \
        auto expectedValue = (expected);                                                           \
        if (!(actualValue == expectedValue)) {                                                     \
            reportFailure(__FILE__, __LINE__, #actual " == " #expected,                             \
                          "got " + std::to_string(actualValue) + ", wanted " +                      \
                              std::to_string(expectedValue));                                       \
        }                                                                                          \
    } while (false)

#define CHECK_NEAR(actual, expected, tolerance)                                                    \
    do {                                                                                           \
        double actualValue = (double)(actual);                                                     \
        double expectedValue = (double)(expected);                                                 \
        if (std::fabs(actualValue - expectedValue) > (double)(tolerance)) {                        \
            reportFailure(__FILE__, __LINE__, #actual " ~= " #expected,                             \
                          "got " + std::to_string(actualValue) + ", wanted " +                      \
                              std::to_string(expectedValue));                                       \
        }                                                                                          \
    } while (false)
