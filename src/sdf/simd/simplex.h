#ifndef JAR_SIMPLEX_H
#define JAR_SIMPLEX_H

#include <cstdint>

// Simplex noise for many points at once: the one hot loop under all terrain.
// simplex_kernel.inl is the code; it is built once per instruction set, and the
// best one this CPU runs is chosen at startup.
namespace jar_simd
{
using SimplexLanes = void (*)(const double *px, const double *py, const double *pz, const uint32_t *seeds, int count,
                              float *value, float *gx, float *gy, float *gz);

extern const SimplexLanes simplex_generic;
#ifdef JAR_HAS_AVX2
extern const SimplexLanes simplex_avx2;
#endif

// The kernel in use.
SimplexLanes simplex();
// Off: the plain kernel, whatever the CPU has (tests compare the two).
void use_simd(bool enabled);
// Whether a faster kernel is in use.
bool simd_active();
} // namespace jar_simd

#endif // JAR_SIMPLEX_H
