
#include "poisson_disc_sampler.h"
#include <cmath>
#include <queue>
#include <algorithm>

std::vector<PoissonDiscPoint> PoissonDiscSampler::generate(float width, float height,
                                                           float radius,
                                                           int k,
                                                           unsigned int seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    float cellSize = radius / std::sqrt(2.0f);
    int gridWidth  = static_cast<int>(std::ceil(width / cellSize));
    int gridHeight = static_cast<int>(std::ceil(height / cellSize));

    std::vector<int> grid(gridWidth * gridHeight, -1);
    std::vector<PoissonDiscPoint> points;
    std::vector<int> active;

    auto gridIndex = [&](float x, float y) {
        return std::pair<int,int>(
            static_cast<int>(x / cellSize),
            static_cast<int>(y / cellSize)
        );
    };

    // First point
    PoissonDiscPoint first{dist01(rng) * width, dist01(rng) * height};
    points.push_back(first);
    auto [gx, gy] = gridIndex(first.x, first.y);
    grid[gy * gridWidth + gx] = 0;
    active.push_back(0);

    auto generateCandidate = [&](const PoissonDiscPoint& p) {
        float angle = 2.0f * float(3.1415f) * dist01(rng);
        float r = radius * (1.0f + dist01(rng));
        return PoissonDiscPoint{p.x + r * std::cos(angle), p.y + r * std::sin(angle)};
    };

    while (!active.empty()) {
        std::uniform_int_distribution<int> distActive(0, (int)active.size() - 1);
        int idx = distActive(rng);
        PoissonDiscPoint base = points[active[idx]];
        bool found = false;

        for (int i = 0; i < k; ++i) {
            PoissonDiscPoint cand = generateCandidate(base);
            if (cand.x < 0 || cand.x >= width || cand.y < 0 || cand.y >= height)
                continue;

            auto [cgx, cgy] = gridIndex(cand.x, cand.y);
            bool ok = true;

            for (int yy = std::max(0, cgy - 2); yy <= std::min(gridHeight - 1, cgy + 2); ++yy) {
                for (int xx = std::max(0, cgx - 2); xx <= std::min(gridWidth - 1, cgx + 2); ++xx) {
                    int gi = grid[yy * gridWidth + xx];
                    if (gi != -1) {
                        float dx = points[gi].x - cand.x;
                        float dy = points[gi].y - cand.y;
                        if (dx*dx + dy*dy < radius*radius) {
                            ok = false;
                            break;
                        }
                    }
                }
                if (!ok) break;
            }

            if (ok) {
                points.push_back(cand);
                grid[cgy * gridWidth + cgx] = (int)points.size() - 1;
                active.push_back((int)points.size() - 1);
                found = true;
                break;
            }
        }

        if (!found) {
            active[idx] = active.back();
            active.pop_back();
        }
    }

    return points;
}
