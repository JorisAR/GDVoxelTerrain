### Meshing Algorithms Overview
I have created two versions of surface nets, that aim to bridge the gaps between LOD:
- Adaptive Surface Nets (removed; kept below for the design history)
- Stitched Surface nets (the one in use)


This readme aims to briefly explain some of my thought processes when creating these algorithms.

### Adaptive Surface Nets
Adaptive Surface Nets is the original algorithm. It collects all nodes in an 18x18x18 sized cube around the chunk, and writes them to a look up table, enabling constant time neighbour finding. When it detects that it borders a chunk of a higher LOD, we increase the resolution of this look up table to  support those nodes.

To ensure better compatibility, it looks for neighbours in outwards going directions, meaning it never tries to look for nodes that are of higher LOD from a particular node. This makes the algorithm slightly more robust, and is computed as the sign of the difference between camera position and chunk center, referred to in code as Octant.

However, it struggles to compute the normals at LOD intersections, and it's a little messy due to the octant part over complicating some parts.


### Stitched Surface Nets
Stitched Surface Nets comes from the idea of stitching chunks together. If this is done after chunks have been generated independently, it adds a nasty dependence on neighbouring chunk meshes to the system. I dislike this, as this may result in higher latency, or having to recompute things like colliders. 

Therefore, this implementation aims to keep the chunk independence. The base case remains simple, we can mesh in 18x18x18 node grids, assuming each chunk is 16x16x16. We need two more, as the dual approach generates vertices in a 17x17x17 grid, which is exactly what we need to tile 16x16x16. (in 1D, you need n + 1 vertices if you want n edges; simplest case: 1 edge needs 2 vertices).

When we mesh on LOD boundaries, it becomes a little more complicated. When going from high to low LOD (smaller to larger voxels), I decide to strip away a layer of vertices. I then find all faces that facilitate such a LOD border, and generate vertices according to the lower LOD there. This means we now have 2 sets of vertices in a border chunk: inner vertices from the high LOD nodes, and ring vertices from the lod LOD chunk, generated on the border of the chunk, only if the LOD changes on that border.

We then connect them. We can do this reasonably fast using a hashmap, and by converting between the coordinate systems. We explicitly want the ring nodes to have coordinates in the range [0, 9], as opposed to [0,18) like we would in the inner nodes. This enables us to generate triangles wherever we need, by mapping certain inner nodes's neighbouring ringnodes to the same node. Intuitively we need an alternating pattern of 1 quad, 1 triangle to connect the chunks here.