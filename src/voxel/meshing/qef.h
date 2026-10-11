#ifndef JAR_QEF_H
#define JAR_QEF_H

#include <cmath>
#include <glm/glm.hpp>

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
