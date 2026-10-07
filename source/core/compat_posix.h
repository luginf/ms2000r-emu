// Force-included on non-MSVC builds (see CMakeLists.txt): the MSVC-isms the sources rely on.
#pragma once
#ifndef _MSC_VER
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <climits>
#include <cmath>
#include <chrono>
#include <string>
#include <algorithm>
#include <memory>
#include <unistd.h>

#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif

#include <thread>
#include <ctime>
#define sprintf_s(buf, sz, ...) std::snprintf((buf), (sz), __VA_ARGS__)
inline int strncpy_s(char* d, size_t dsz, const char* s, size_t n) {
    if (!d || dsz == 0) return EINVAL;
    size_t i = 0;
    for (; i < n && i + 1 < dsz && s[i]; ++i) d[i] = s[i];
    d[i] = 0;
    return 0;
}
inline int localtime_s(std::tm* out, const std::time_t* t) { return localtime_r(t, out) ? 0 : errno; }
inline void Sleep(unsigned ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

#ifndef MS2K_HAVE_FOPEN_S
#define MS2K_HAVE_FOPEN_S
inline int fopen_s(FILE** f, const char* name, const char* mode) {
    *f = std::fopen(name, mode);
    return *f ? 0 : errno;
}
#endif
#endif
