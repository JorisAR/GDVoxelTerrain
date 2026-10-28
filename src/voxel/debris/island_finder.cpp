#include "island_finder.h"
#include <deque>

namespace
{
constexpr int Anchored = -1;
constexpr int TooLarge = -2;
const glm::ivec3 Neighbours[6] = {glm::ivec3(1, 0, 0),  glm::ivec3(-1, 0, 0), glm::ivec3(0, 1, 0),
                                  glm::ivec3(0, -1, 0), glm::ivec3(0, 0, 1),  glm::ivec3(0, 0, -1)};

bool on_border(const VoxelBlock &b, const glm::ivec3 &c)
{
    return c.x == 0 || c.y == 0 || c.z == 0 || c.x == b.size.x - 1 || c.y == b.size.y - 1 || c.z == b.size.z - 1;
}

// Labels every solid cell connected to `seeds` with `label`; returns them.
std::vector<int64_t> fill(const VoxelBlock &b, std::vector<int> &labels, std::deque<int64_t> &queue, int label)
{
    std::vector<int64_t> cells;
    while (!queue.empty())
    {
        const int64_t i = queue.front();
        queue.pop_front();
        cells.push_back(i);
        const glm::ivec3 c = b.cell(i);
        for (const glm::ivec3 &o : Neighbours)
        {
            const glm::ivec3 n = c + o;
            if (!b.contains(n))
                continue;
            const int64_t j = b.index(n);
            if (labels[j] == 0 && b.values[j] < 0.0f)
            {
                labels[j] = label;
                queue.push_back(j);
            }
        }
    }
    return cells;
}
} // namespace

std::vector<IslandFinder::Island> IslandFinder::find(const VoxelBlock &region, int maxCells)
{
    std::vector<Island> islands;
    const int64_t count = region.cell_count();
    if (count == 0)
        return islands;
    std::vector<int> labels(count, 0);
    std::deque<int64_t> queue;

    // Everything solid that reaches the border holds on to the rest of the world.
    for (int64_t i = 0; i < count; ++i)
        if (region.values[i] < 0.0f && on_border(region, region.cell(i)))
        {
            labels[i] = Anchored;
            queue.push_back(i);
        }
    fill(region, labels, queue, Anchored);

    // What is left floats: one piece per connected part.
    int next = 1;
    for (int64_t i = 0; i < count; ++i)
    {
        if (labels[i] != 0 || region.values[i] >= 0.0f)
            continue;
        const int label = next++;
        labels[i] = label;
        queue.push_back(i);
        const std::vector<int64_t> cells = fill(region, labels, queue, label);

        glm::ivec3 lo(std::numeric_limits<int>::max()), hi(std::numeric_limits<int>::min());
        glm::vec3 sum(0.0f);
        for (int64_t j : cells)
        {
            const glm::ivec3 c = region.cell(j);
            lo = glm::min(lo, c);
            hi = glm::max(hi, c);
            sum += region.position(c);
        }
        if (glm::any(glm::greaterThan(hi - lo + 1, glm::ivec3(maxCells))))
        {
            for (int64_t j : cells)
                labels[j] = TooLarge;
            continue;
        }

        // The piece alone: its cells, the empty space around it, and any other
        // solid in its box turned into empty space.
        constexpr int Margin = 2;
        auto block = std::make_shared<VoxelBlock>();
        block->spacing = region.spacing;
        block->origin = region.position(lo - Margin);
        block->resize(hi - lo + 1 + 2 * Margin);
        for (int64_t k = 0; k < block->cell_count(); ++k)
        {
            const glm::ivec3 c = block->cell(k);
            const glm::ivec3 rc = lo - Margin + c;
            if (!region.contains(rc))
                continue; // stays empty
            const int64_t j = region.index(rc);
            const bool mine = labels[j] == label;
            if (mine || region.values[j] >= 0.0f)
            {
                block->values[k] = region.values[j];
                block->materials[k] = region.materials[j];
                block->normals[k] = region.normals[j];
            }
            else
                block->values[k] = region.spacing; // another solid: not part of this piece
        }
        islands.push_back({block, sum / float(cells.size()), int(cells.size())});
    }
    return islands;
}
