// The kernels in plain C++, for every platform.
#include "kernels.h"
#include <cmath>

namespace jar_simd
{
namespace generic
{
#include "batch.h"
#include "kernels.inl"
} // namespace generic

const Kernels kernels_generic = {generic::simplex_lanes, generic::pivot_lanes};
} // namespace jar_simd
