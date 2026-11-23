#pragma once
#include <cstdlib>
#include <iostream>

// assert() is compiled out in Release (NDEBUG). CHECK always runs.
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << "  " #cond  \
                      << std::endl;                                              \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(((a) - (b)) < (eps) && ((b) - (a)) < (eps))
