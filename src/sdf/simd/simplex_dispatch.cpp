#include "simplex.h"
#include <atomic>

#ifdef JAR_HAS_AVX2
#if defined(_MSC_VER) && !defined(__clang__)
#include <immintrin.h>
#include <intrin.h>
#endif
#endif

namespace jar_simd
{
namespace
{
#ifdef JAR_HAS_AVX2
bool cpu_has_avx2()
{
#if defined(_MSC_VER) && !defined(__clang__)
    int info[4];
    __cpuid(info, 0);
    if (info[0] < 7)
        return false;
    __cpuid(info, 1);
    constexpr int Xsave = 1 << 27, Avx = 1 << 28;
    if ((info[2] & Xsave) == 0 || (info[2] & Avx) == 0)
        return false;
    if ((_xgetbv(0) & 6) != 6)
        return false; // the system does not save the AVX registers
    __cpuidex(info, 7, 0);
    return (info[1] & (1 << 5)) != 0;
#else
    return __builtin_cpu_supports("avx2");
#endif
}
#endif

SimplexLanes best()
{
#ifdef JAR_HAS_AVX2
    if (cpu_has_avx2())
        return simplex_avx2;
#endif
    return simplex_generic;
}

std::atomic<SimplexLanes> s_current{nullptr};
} // namespace

SimplexLanes simplex()
{
    SimplexLanes kernel = s_current.load(std::memory_order_relaxed);
    if (kernel == nullptr)
    {
        kernel = best();
        s_current.store(kernel, std::memory_order_relaxed);
    }
    return kernel;
}

void use_simd(bool enabled)
{
    s_current.store(enabled ? best() : simplex_generic, std::memory_order_relaxed);
}

bool simd_active()
{
    return simplex() != simplex_generic;
}
} // namespace jar_simd
