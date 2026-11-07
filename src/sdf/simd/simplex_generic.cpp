// The noise kernel in plain C++, for every platform.
#include "simplex.h"
#include <cmath>

namespace jar_simd
{
namespace generic
{
#include "batch.h"
#include "simplex_kernel.inl"
} // namespace generic

const SimplexLanes simplex_generic = generic::simplex_lanes;
} // namespace jar_simd
