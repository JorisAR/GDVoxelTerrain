// 3D simplex noise with its gradient, for many points at once. Included inside
// a namespace after batch.h, once per build (see batch.h), so it stays free of
// anything else: code built for AVX2 must not leak into the rest of the library.

constexpr float SimplexScale = 76.0f; // brings the noise to about [-1, 1]

// clang-format off
alignas(32) const float GradientsX[16] = {1, -1, 1, -1, 1, -1, 1, -1, 0, 0, 0, 0, 1, -1, 0, 0};
alignas(32) const float GradientsY[16] = {1, 1, -1, -1, 0, 0, 0, 0, 1, -1, 1, -1, 1, 1, -1, -1};
alignas(32) const float GradientsZ[16] = {0, 0, 0, 0, 1, 1, -1, -1, 1, 1, -1, -1, 0, 0, 1, -1};
// clang-format on

inline Ints hash3(Ints x, Ints y, Ints z, Ints seed)
{
    Ints h = seed ^ (x * Ints(int32_t(0x8da6b343U))) ^ (y * Ints(int32_t(0xd8163841U))) ^ (z * Ints(int32_t(0xcb1ab31fU)));
    h = h ^ shift_right<16>(h);
    h = h * Ints(int32_t(0x7feb352dU));
    h = h ^ shift_right<15>(h);
    h = h * Ints(int32_t(0x846ca68bU));
    return h ^ shift_right<16>(h);
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
