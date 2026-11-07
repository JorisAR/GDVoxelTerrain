#ifndef JAR_BATCH_H
#define JAR_BATCH_H

// The few vector operations the noise kernel needs, so it is written once and
// reads like plain math. Include it inside a namespace, per translation unit:
// the two builds below are different types.
//
//   JAR_SIMD_AVX2 defined: 8 lanes of AVX2 (simplex_avx2.cpp only, built with
//                          AVX2 enabled and chosen at runtime).
//   otherwise:             1 lane, plain C++, for every platform.
//
// No fused multiply-add on purpose: both builds must give the same bits, or a
// feature's place would depend on the CPU.

#ifdef JAR_SIMD_AVX2

constexpr int Lanes = 8;

struct Floats
{
    __m256 v;
    Floats() = default;
    Floats(__m256 value) : v(value) {}
    Floats(float value) : v(_mm256_set1_ps(value)) {}
    static Floats load(const float *p) { return _mm256_load_ps(p); }
    void store(float *p) const { _mm256_store_ps(p, v); }
};

struct Ints
{
    __m256i v;
    Ints() = default;
    Ints(__m256i value) : v(value) {}
    Ints(int32_t value) : v(_mm256_set1_epi32(value)) {}
    static Ints load(const int32_t *p) { return _mm256_load_si256(reinterpret_cast<const __m256i *>(p)); }
};

inline Floats operator+(Floats a, Floats b) { return _mm256_add_ps(a.v, b.v); }
inline Floats operator-(Floats a, Floats b) { return _mm256_sub_ps(a.v, b.v); }
inline Floats operator*(Floats a, Floats b) { return _mm256_mul_ps(a.v, b.v); }
inline Ints operator+(Ints a, Ints b) { return _mm256_add_epi32(a.v, b.v); }
inline Ints operator*(Ints a, Ints b) { return _mm256_mullo_epi32(a.v, b.v); }
inline Ints operator^(Ints a, Ints b) { return _mm256_xor_si256(a.v, b.v); }
inline Ints operator&(Ints a, Ints b) { return _mm256_and_si256(a.v, b.v); }
inline Ints operator|(Ints a, Ints b) { return _mm256_or_si256(a.v, b.v); }
inline Ints operator~(Ints a) { return _mm256_xor_si256(a.v, _mm256_set1_epi32(-1)); }
template <int Bits> inline Ints shift_right(Ints a) { return _mm256_srli_epi32(a.v, Bits); }

// All bits set where it holds.
inline Ints at_least(Floats a, Floats b) { return _mm256_castps_si256(_mm256_cmp_ps(a.v, b.v, _CMP_GE_OQ)); }
inline Ints above(Floats a, Floats b) { return _mm256_castps_si256(_mm256_cmp_ps(a.v, b.v, _CMP_GT_OQ)); }
// `a` where `mask` is set, else 0.
inline Floats only(Ints mask, Floats a) { return _mm256_and_ps(_mm256_castsi256_ps(mask.v), a.v); }
inline Floats to_floats(Ints a) { return _mm256_cvtepi32_ps(a.v); }
inline Floats lookup(const float *table, Ints index) { return _mm256_i32gather_ps(table, index.v, 4); }

#else

constexpr int Lanes = 1;

struct Floats
{
    float v;
    Floats() = default;
    Floats(float value) : v(value) {}
    static Floats load(const float *p) { return *p; }
    void store(float *p) const { *p = v; }
};

struct Ints
{
    int32_t v;
    Ints() = default;
    Ints(int32_t value) : v(value) {}
    static Ints load(const int32_t *p) { return *p; }
};

inline Floats operator+(Floats a, Floats b) { return a.v + b.v; }
inline Floats operator-(Floats a, Floats b) { return a.v - b.v; }
inline Floats operator*(Floats a, Floats b) { return a.v * b.v; }
inline Ints operator+(Ints a, Ints b) { return int32_t(uint32_t(a.v) + uint32_t(b.v)); }
inline Ints operator*(Ints a, Ints b) { return int32_t(uint32_t(a.v) * uint32_t(b.v)); }
inline Ints operator^(Ints a, Ints b) { return a.v ^ b.v; }
inline Ints operator&(Ints a, Ints b) { return a.v & b.v; }
inline Ints operator|(Ints a, Ints b) { return a.v | b.v; }
inline Ints operator~(Ints a) { return ~a.v; }
template <int Bits> inline Ints shift_right(Ints a) { return int32_t(uint32_t(a.v) >> Bits); }

inline Ints at_least(Floats a, Floats b) { return a.v >= b.v ? -1 : 0; }
inline Ints above(Floats a, Floats b) { return a.v > b.v ? -1 : 0; }
inline Floats only(Ints mask, Floats a) { return mask.v != 0 ? a.v : 0.0f; }
inline Floats to_floats(Ints a) { return float(a.v); }
inline Floats lookup(const float *table, Ints index) { return table[index.v]; }

#endif

#endif // JAR_BATCH_H
