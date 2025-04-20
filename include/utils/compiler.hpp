#pragma once

#include <cstdlib>

#include "logger.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define OCTET_BUILTIN_UNREACHABLE() __builtin_unreachable()
#elif defined(_MSC_VER)
#define OCTET_BUILTIN_UNREACHABLE() __assume(false)
#else
#define OCTET_BUILTIN_UNREACHABLE() ((void)0)
#endif

// Макрос для недостижимых веток кода
#define UNREACHABLE(reason)                                                                        \
    do {                                                                                           \
        LOG_CRITICAL << "UNREACHABLE code reached: " << reason;                                    \
        std::abort();                                                                              \
        OCTET_BUILTIN_UNREACHABLE();                                                               \
    } while (0)
