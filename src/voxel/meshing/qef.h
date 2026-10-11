#ifndef JAR_QEF_H
#define JAR_QEF_H

#include <cmath>
#include <algorithm>
#include <glm/glm.hpp>
#include <limits>

// Quadratic error function for dual contouring: accumulates tangent planes
// (point + normal at each surface/edge crossing) and finds the point that is
// closest to all of them. Directions the planes do not constrain (e.g. along a
// flat face or a straight edge) fall back to the mass point, via truncated
// eigen-decomposition, which keeps vertices stable on flat and curved areas
// while still snapping to sharp edges and corners.
class Qef
{
  public:
    void add(const glm::vec3 &point, const glm::vec3 &normal)
    {
        _ata[0][0] += normal.x * normal.x;
        _ata[0][1] += normal.x * normal.y;
        _ata[0][2] += normal.x * normal.z;
        _ata[1][1] += normal.y * normal.y;
        _ata[1][2] += normal.y * normal.z;
        _ata[2][2] += normal.z * normal.z;
        const float d = glm::dot(normal, point);
        _atb += normal * d;
        _massPoint += point;
        _count++;
    }

    int count() const { return _count; }
    glm::vec3 mass_point() const { return _count > 0 ? _massPoint / float(_count) : glm::vec3(0.0f); }

    // Solves using only the `rank` strongest plane directions (1 = the dominant
    // plane, 2 = an edge, 3 = a corner); weaker directions are left free.
    // `threshold` caps the rank further: directions weaker than threshold *
    // strongest are always free. Returns the point closest to the mass point
    // that satisfies the used directions, and the free directions.
    struct Solution
    {
        glm::vec3 point;
        glm::vec3 free[3];
        int freeCount = 0;
    };

    Solution solve_rank(int rank, float threshold = 0.1f) const
    {
        Solution result;
        const glm::vec3 c = mass_point();
        result.point = c;
        glm::mat3 a(0.0f);
        a[0][0] = _ata[0][0];
        a[1][1] = _ata[1][1];
        a[2][2] = _ata[2][2];
        a[0][1] = a[1][0] = _ata[0][1];
        a[0][2] = a[2][0] = _ata[0][2];
        a[1][2] = a[2][1] = _ata[1][2];
        const glm::vec3 rhs = _atb - a * c;
        glm::mat3 v;
        glm::vec3 eig;
        jacobi_eigen(a, v, eig);
        // Strongest directions first.
        int order[3] = {0, 1, 2};
        std::sort(order, order + 3, [&](int x, int y) { return std::abs(eig[x]) > std::abs(eig[y]); });
        const float largest = std::abs(eig[order[0]]);
        for (int k = 0; k < 3; ++k)
        {
            const int i = order[k];
            const glm::vec3 vi = glm::normalize(glm::vec3(v[i]));
            if (k >= rank || largest <= 0.0f || std::abs(eig[i]) < threshold * largest)
            {
                result.free[result.freeCount++] = vi;
                continue;
            }
            result.point += vi * (glm::dot(vi, rhs) / eig[i]);
        }
        return result;
    }

    int rank(float threshold = 0.1f) const
    {
        return 3 - solve_rank(3, threshold).freeCount;
    }

    // Moves a solution along its free directions to the point closest to
    // `target` (inside [lo, hi] if possible). Returns false if no point of the
    // solution's feature (point / line / plane) lies inside the box.
    static bool fit_into(Solution &s, const glm::vec3 &target, const glm::vec3 &lo, const glm::vec3 &hi)
    {
        auto inside = [&](const glm::vec3 &p) {
            return !glm::any(glm::lessThan(p, lo)) && !glm::any(glm::greaterThan(p, hi));
        };
        if (s.freeCount == 0)
            return inside(s.point);
        if (s.freeCount == 1)
        {
            // Feature line p + t d: slab intersection with the box, then the
            // valid t closest to the target.
            const glm::vec3 d = s.free[0];
            float tMin = -std::numeric_limits<float>::max(), tMax = std::numeric_limits<float>::max();
            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(d[i]) < 1e-6f)
                {
                    if (s.point[i] < lo[i] || s.point[i] > hi[i])
                        return false;
                    continue;
                }
                float t0 = (lo[i] - s.point[i]) / d[i], t1 = (hi[i] - s.point[i]) / d[i];
                if (t0 > t1)
                    std::swap(t0, t1);
                tMin = std::max(tMin, t0);
                tMax = std::min(tMax, t1);
            }
            if (tMin > tMax)
                return false;
            s.point += d * std::clamp(glm::dot(target - s.point, d), tMin, tMax);
            return true;
        }
        // Plane (or nothing constrained): project the target onto it.
        if (s.freeCount == 2)
        {
            const glm::vec3 n = glm::normalize(glm::cross(s.free[0], s.free[1]));
            s.point = target - n * glm::dot(target - s.point, n);
        }
        else
            s.point = target;
        return inside(s.point);
    }

    // `threshold`: eigenvalues below threshold * largest are treated as
    // unconstrained. Higher values snap less aggressively to features.
    glm::vec3 solve(float threshold = 0.1f) const
    {
        const glm::vec3 c = mass_point();
        glm::mat3 a(0.0f);
        a[0][0] = _ata[0][0];
        a[1][1] = _ata[1][1];
        a[2][2] = _ata[2][2];
        a[0][1] = a[1][0] = _ata[0][1];
        a[0][2] = a[2][0] = _ata[0][2];
        a[1][2] = a[2][1] = _ata[1][2];

        // Solve A (x - c) = A^T b - A c with the pseudo-inverse of A.
        const glm::vec3 rhs = _atb - a * c;
        glm::mat3 v;
        glm::vec3 eig;
        jacobi_eigen(a, v, eig);
        const float largest = std::max(std::abs(eig.x), std::max(std::abs(eig.y), std::abs(eig.z)));
        if (largest <= 0.0f)
            return c;
        glm::vec3 x(0.0f);
        for (int i = 0; i < 3; ++i)
        {
            if (std::abs(eig[i]) < threshold * largest)
                continue;
            const glm::vec3 vi = v[i]; // column i: eigenvector
            x += vi * (glm::dot(vi, rhs) / eig[i]);
        }
        return c + x;
    }

  private:
    float _ata[3][3] = {};
    glm::vec3 _atb{0.0f};
    glm::vec3 _massPoint{0.0f};
    int _count = 0;

    // Cyclic Jacobi rotations for a symmetric 3x3 matrix. Columns of `vectors`
    // are the eigenvectors.
    static void jacobi_eigen(glm::mat3 a, glm::mat3 &vectors, glm::vec3 &values)
    {
        vectors = glm::mat3(1.0f);
        for (int sweep = 0; sweep < 8; ++sweep)
        {
            const float off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
            if (off < 1e-12f)
                break;
            for (int p = 0; p < 2; ++p)
                for (int q = p + 1; q < 3; ++q)
                {
                    const float apq = a[q][p];
                    if (std::abs(apq) < 1e-12f)
                        continue;
                    const float theta = (a[q][q] - a[p][p]) / (2.0f * apq);
                    const float t = (theta >= 0.0f ? 1.0f : -1.0f) / (std::abs(theta) + std::sqrt(theta * theta + 1.0f));
                    const float cs = 1.0f / std::sqrt(t * t + 1.0f);
                    const float sn = t * cs;
                    // a = J^T a J with J the (p, q) rotation.
                    glm::mat3 j(1.0f);
                    j[p][p] = cs;
                    j[q][q] = cs;
                    j[q][p] = sn;  // row p, column q
                    j[p][q] = -sn; // row q, column p
                    a = glm::transpose(j) * a * j;
                    vectors = vectors * j;
                }
        }
        values = glm::vec3(a[0][0], a[1][1], a[2][2]);
    }
};

#endif // JAR_QEF_H
