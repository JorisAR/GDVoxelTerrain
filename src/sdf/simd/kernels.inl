// The terrain's hot loops, for many items at once. Included inside a namespace
// after batch.h, once per build (see batch.h), so it stays free of
// anything else: code built for AVX2 must not leak into the rest of the library.

constexpr float SimplexScale = 76.0f; // brings the noise to about [-1, 1]

// clang-format off
alignas(32) const float GradientsX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, -1, 0, 0};
alignas(32) const float GradientsY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, 1, -1, -1};
alignas(32) const float GradientsZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 0, 1, -1};
// clang-format on

inline Ints hash_u32(Ints x)
{
    x = x ^ shift_right<16>(x);
    x = x * Ints(int32_t(0x7feb352dU));
    x = x ^ shift_right<15>(x);
    x = x * Ints(int32_t(0x846ca68bU));
    return x ^ shift_right<16>(x);
}

inline Ints hash_cell(Ints x, Ints y, Ints z)
{
    return (x * Ints(int32_t(0x8da6b343U))) ^ (y * Ints(int32_t(0xd8163841U))) ^ (z * Ints(int32_t(0xcb1ab31fU)));
}

inline Ints hash3(Ints x, Ints y, Ints z, Ints seed)
{
    return hash_u32(seed ^ hash_cell(x, y, z));
}

// Fills value[i] and gradient (gx, gy, gz)[i] for the `count` points (px, py,
// pz)[i], each noise with its own seed.
void simplex_lanes(const double *px, const double *py, const double *pz, const uint32_t *seeds, int count, float *value,
                   float *gx, float *gy, float *gz)
{
    constexpr float G3 = 1.0f / 6.0f;
    for (int start = 0; start < count; start += Lanes)
    {
        const int lanes = count - start < Lanes ? count - start : Lanes;
        // The cell in doubles (positions are huge), the offset into it (small)
        // in floats: from here on everything fits in lanes of floats.
        alignas(32) float x0[Lanes] = {}, y0[Lanes] = {}, z0[Lanes] = {};
        alignas(32) int32_t cellX[Lanes] = {}, cellY[Lanes] = {}, cellZ[Lanes] = {}, seed[Lanes] = {};
        for (int l = 0; l < lanes; ++l)
        {
            const double x = px[start + l], y = py[start + l], z = pz[start + l];
            const double s = (x + y + z) * (1.0 / 3.0);
            const double fi = std::floor(x + s), fj = std::floor(y + s), fk = std::floor(z + s);
            const double t = (fi + fj + fk) * (1.0 / 6.0);
            cellX[l] = int32_t(fi);
            cellY[l] = int32_t(fj);
            cellZ[l] = int32_t(fk);
            x0[l] = float(x - (fi - t));
            y0[l] = float(y - (fj - t));
            z0[l] = float(z - (fk - t));
            seed[l] = int32_t(seeds[start + l]);
        }
        const Floats dx0 = Floats::load(x0), dy0 = Floats::load(y0), dz0 = Floats::load(z0);
        const Ints i = Ints::load(cellX), j = Ints::load(cellY), k = Ints::load(cellZ), noiseSeed = Ints::load(seed);

        // Which simplex of the skewed cube: the order of the coordinates. The
        // four corners are 0, a step along the largest axis, a further one
        // along the next, and (1, 1, 1).
        const Ints xy = at_least(dx0, dy0), yz = at_least(dy0, dz0), xz = at_least(dx0, dz0);
        const Ints one(1);
        const Ints firstX = (xy & xz) & one, firstY = (~xy & yz) & one, firstZ = (~xz & ~yz) & one;
        const Ints secondX = (xy | xz) & one, secondY = (~xy | yz) & one, secondZ = (~xz | ~yz) & one;
        const Ints cornerX[4] = {Ints(0), firstX, secondX, one};
        const Ints cornerY[4] = {Ints(0), firstY, secondY, one};
        const Ints cornerZ[4] = {Ints(0), firstZ, secondZ, one};

        Floats n(0.0f), sumX(0.0f), sumY(0.0f), sumZ(0.0f);
        for (int c = 0; c < 4; ++c)
        {
            const Floats shift(float(c) * G3);
            const Floats dx = dx0 - to_floats(cornerX[c]) + shift;
            const Floats dy = dy0 - to_floats(cornerY[c]) + shift;
            const Floats dz = dz0 - to_floats(cornerZ[c]) + shift;
            // Kernel radius^2 0.5: zero before the next simplex starts (continuous).
            const Floats k2 = Floats(0.5f) - (dx * dx + dy * dy + dz * dz);
            const Ints inside = above(k2, Floats(0.0f));
            const Ints h = hash3(i + cornerX[c], j + cornerY[c], k + cornerZ[c], noiseSeed) & Ints(15);
            const Floats ax = lookup(GradientsX, h), ay = lookup(GradientsY, h), az = lookup(GradientsZ, h);
            const Floats dot = ax * dx + ay * dy + az * dz;
            const Floats k3 = k2 * k2 * k2;
            const Floats k4 = k3 * k2;
            n = n + only(inside, k4 * dot);
            // d/dp (k2^4 * dot) = k2^4 * grad - 8 k2^3 * dot * d
            const Floats slope = Floats(8.0f) * k3 * dot;
            sumX = sumX + only(inside, k4 * ax - slope * dx);
            sumY = sumY + only(inside, k4 * ay - slope * dy);
            sumZ = sumZ + only(inside, k4 * az - slope * dz);
        }
        alignas(32) float outN[Lanes], outX[Lanes], outY[Lanes], outZ[Lanes];
        (n * Floats(SimplexScale)).store(outN);
        (sumX * Floats(SimplexScale)).store(outX);
        (sumY * Floats(SimplexScale)).store(outY);
        (sumZ * Floats(SimplexScale)).store(outZ);
        for (int l = 0; l < lanes; ++l)
        {
            value[start + l] = outN[l];
            gx[start + l] = outX[l];
            gy[start + l] = outY[l];
            gz[start + l] = outZ[l];
        }
    }
}

constexpr float Tau = 6.28318530718f;

// sin(x) on [-pi/2, pi/2] (Taylor to x^9: error below 4e-6).
inline Floats sin_half(Floats x)
{
    const Floats x2 = x * x;
    return x * (Floats(1.0f) + x2 * (Floats(-1.0f / 6.0f) + x2 * (Floats(1.0f / 120.0f) +
                                                                 x2 * (Floats(-1.0f / 5040.0f) + x2 * Floats(1.0f / 362880.0f)))));
}

// Into [-pi/2, pi/2], where the series is accurate.
inline Floats fold(Floats x)
{
    constexpr float Pi = 3.14159265359f, HalfPi = 1.57079632679f;
    return choose(above(x, Floats(HalfPi)), Floats(Pi) - x, choose(above(Floats(-HalfPi), x), Floats(-Pi) - x, x));
}

// Erosion: for the stripe pivots of neighboring cells (cell[i], `step[i]` cells
// from the sample at `f` in its own cell), where each pivot is seen from the
// sample (d), how strongly (falloff), and sin and cos of the stripes through it
// (running across `across`, in cells). `flat`: pivots follow the sample's height.
void pivot_lanes(const PivotInput &input, int count, float *falloff, float *sine, float *cosine, float *dx, float *dy,
                 float *dz)
{
    constexpr float HalfPi = 1.57079632679f, Scale = 1.0f / 1024.0f;
    for (int start = 0; start < count; start += Lanes)
    {
        const int lanes = count - start < Lanes ? count - start : Lanes;
        int32_t cellX[Lanes] = {}, cellY[Lanes] = {}, cellZ[Lanes] = {};
        float stepX[Lanes] = {}, stepY[Lanes] = {}, stepZ[Lanes] = {};
        for (int l = 0; l < lanes; ++l)
        {
            cellX[l] = input.cellX[start + l];
            cellY[l] = input.cellY[start + l];
            cellZ[l] = input.cellZ[start + l];
            stepX[l] = input.stepX[start + l];
            stepY[l] = input.stepY[start + l];
            stepZ[l] = input.stepZ[start + l];
        }
        // A random point in each cell, in [0, 1)^3 (10 bits of one hash each).
        const Ints h = hash_u32(hash_cell(Ints::load(cellX), Ints::load(cellY), Ints::load(cellZ)));
        const Ints mask(0x3FF);
        const Floats offsetX = to_floats(h & mask) * Floats(Scale);
        const Floats offsetY = to_floats(shift_right<10>(h) & mask) * Floats(Scale);
        const Floats offsetZ = to_floats(shift_right<20>(h) & mask) * Floats(Scale);
        const Floats x = Floats(input.fx) - (Floats::load(stepX) + offsetX);
        const Floats y = input.flat ? Floats(0.0f) : Floats(input.fy) - (Floats::load(stepY) + offsetY);
        const Floats z = Floats(input.fz) - (Floats::load(stepZ) + offsetZ);
        const Floats strength = maximum(Floats(0.0f), Floats(1.0f) - (x * x + y * y + z * z));
        const Floats angle = (x * Floats(input.acrossX) + y * Floats(input.acrossY) + z * Floats(input.acrossZ)) * Floats(Tau);
        // Wrapped to [-pi, pi]; cos(x) = sin(x + pi/2).
        const Floats wrapped = angle - Floats(Tau) * floor(angle * Floats(1.0f / Tau) + Floats(0.5f));
        const Floats shifted = wrapped + Floats(HalfPi);
        const Floats forCos = choose(above(shifted, Floats(3.14159265359f)), shifted - Floats(Tau), shifted);
        float outF[Lanes], outS[Lanes], outC[Lanes], outX[Lanes], outY[Lanes], outZ[Lanes];
        strength.store(outF);
        sin_half(fold(wrapped)).store(outS);
        sin_half(fold(forCos)).store(outC);
        x.store(outX);
        y.store(outY);
        z.store(outZ);
        for (int l = 0; l < lanes; ++l)
        {
            falloff[start + l] = outF[l];
            sine[start + l] = outS[l];
            cosine[start + l] = outC[l];
            dx[start + l] = outX[l];
            dy[start + l] = outY[l];
            dz[start + l] = outZ[l];
        }
    }
}
