#ifndef JAR_KERNELS_H
#define JAR_KERNELS_H

#include <cstdint>

// The hot loops under all terrain, for many items at once. kernels.inl is the
// code; it is built once per instruction set, and the best one this CPU runs is
// chosen at startup. Every build gives the same bits.
namespace jar_simd
{
// Up to 27: a 3x3x3 block of cells around a sample.
struct PivotInput
{
    int32_t cellX[27], cellY[27], cellZ[27];
    float stepX[27], stepY[27], stepZ[27];
    float fx, fy, fz;
    float acrossX, acrossY, acrossZ;
    bool flat;
};

struct Kernels
{
    void (*simplex)(const double *px, const double *py, const double *pz, const uint32_t *seeds, int count, float *value,
                    float *gx, float *gy, float *gz);
    void (*pivots)(const PivotInput &input, int count, float *falloff, float *sine, float *cosine, float *dx, float *dy,
                   float *dz);
};

extern const Kernels kernels_generic;
#ifdef JAR_HAS_AVX2
extern const Kernels kernels_avx2;
#endif

// The kernels in use.
const Kernels &kernels();
// Off: the plain kernels, whatever the CPU has (tests compare the two).
void use_simd(bool enabled);
// Whether faster kernels are in use.
bool simd_active();
} // namespace jar_simd

#endif // JAR_KERNELS_H
