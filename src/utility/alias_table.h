#ifndef ALIAS_TABLE_H
#define ALIAS_TABLE_H

#include <vector>
#include <random>
#include <cstddef>

class AliasTable {
public:
    AliasTable() = default;
    explicit AliasTable(const std::vector<double>& weights);

    void build(const std::vector<double>& weights);

    // Sample using two uniform random numbers in [0,1)
    std::size_t sample(double u, double v) const;

    // Convenience: sample using an RNG
    template <typename URNG>
    std::size_t sample(URNG& rng) const {
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        return sample(dist(rng), dist(rng));
    }

    std::size_t size() const { return _prob.size(); }

private:
    std::vector<double> _prob;   // scaled probabilities
    std::vector<std::size_t> _alias; // alias indices
};

#endif // ALIAS_TABLE_H
