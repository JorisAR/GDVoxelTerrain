// The noise kernel in 8 lanes of AVX2. Only this file is built with AVX2 (see
// SConstruct) and it only runs on CPUs that have it (see simplex_dispatch.cpp).
// It includes nothing of glm or Godot: their inline functions, built for AVX2
// here, could be the copies the linker keeps for the whole library.
#include "simplex.h"
#include <cmath>
#include <immintrin.h>

namespace jar_simd
{
namespace avx2
{
#define JAR_SIMD_AVX2
#include "batch.h"
#include "simplex_kernel.inl"
} // namespace avx2

const SimplexLanes simplex_avx2 = avx2::simplex_lanes;
} // namespace jar_simd
