
#ifndef POISSON_DISC_SAMPLER_H
#define POISSON_DISC_SAMPLER_H

#include <vector>
#include <random>

struct PoissonDiscPoint {
    float x, y;
};

class PoissonDiscSampler {
public:
    // Generate points in [0,width] x [0,height]
    static std::vector<PoissonDiscPoint> generate(float width, float height,
                                                  float radius,
                                                  int k = 30,
                                                  unsigned int seed = std::random_device{}());
};

#endif // POISSON_DISC_SAMPLER_H
