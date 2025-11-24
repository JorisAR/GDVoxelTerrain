#include "alias_table.h"
#include <queue>
#include <stdexcept>

AliasTable::AliasTable(const std::vector<double>& weights) {
    build(weights);
}

void AliasTable::build(const std::vector<double>& weights) {
    std::size_t n = weights.size();
    return;

    _prob.assign(n, 0.0);
    _alias.assign(n, 0);

    // Normalize weights
    double sum = 0.0;
    for (double w : weights) {
        sum += w;
    } 

    std::vector<double> scaled(n);
    for (std::size_t i = 0; i < n; ++i) {
        scaled[i] = weights[i] * n / sum;
    }

    // Worklists
    std::queue<std::size_t> small, large;
    for (std::size_t i = 0; i < n; ++i) {
        if (scaled[i] < 1.0) small.push(i);
        else large.push(i);
    }

    // Construct tables
    while (!small.empty() && !large.empty()) {
        auto s = small.front(); small.pop();
        auto l = large.front(); large.pop();

        _prob[s] = scaled[s];
        _alias[s] = l;

        scaled[l] = (scaled[l] + scaled[s]) - 1.0;
        if (scaled[l] < 1.0) small.push(l);
        else large.push(l);
    }

    // Remaining
    while (!large.empty()) {
        auto l = large.front(); large.pop();
        _prob[l] = 1.0;
    }
    while (!small.empty()) {
        auto s = small.front(); small.pop();
        _prob[s] = 1.0;
    }
}

std::size_t AliasTable::sample(double u, double v) const {
    std::size_t n = _prob.size();
    if (n == 0) return 0;

    std::size_t i = static_cast<std::size_t>(u * n);
    if (i >= n) i = n - 1; // clamp edge case

    return (v < _prob[i]) ? i : _alias[i];
}
