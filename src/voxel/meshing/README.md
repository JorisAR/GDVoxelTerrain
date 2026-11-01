### Meshing Algorithms Overview
I have created two versions of surface nets, that aim to bridge the gaps between LOD:
- Adaptive Surface Nets (removed; kept below for the design history)
- Stitched Surface nets (replaced by its mixed-LOD version, see the last section)


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

### Mixed-LOD Dual Surface Nets (current)
The stitched version meshed each chunk on a uniform grid and joined LOD borders with a ring of coarse vertices and a heuristic search for the triangles between them. Where a chunk met a coarser one only diagonally (along an edge or corner), the two sides built different surfaces: cracks along every LOD seam, up to half a meter wide.

The current version (still `StitchedSurfaceNets`, with `StitchedMeshChunk` as its input) generalizes surface nets / dual contouring to leaves of mixed sizes, so there is nothing to stitch:
- Samples are the octree leaves at their region's LOD (the chunk's own and its neighbors' around it), on a lattice of the smallest leaf size.
- A vertex for each point where leaves meet (an octree vertex), from the 8 leaves around it; a larger leaf may fill several of the 8 places. It depends only on those leaves (and is computed relative to their corner, see `from_cell`), so every chunk that needs it computes the same floats.
- A polygon for each face between two leaves whose samples differ in sign: the vertices of the octree vertices around the face, its 4 corners plus any along its sides where smaller leaves meet it (leaving those out made T-junction cracks). 4 makes a quad as before; more are fanned around the face's surface crossing.
- Each face is meshed by exactly one chunk: the one owning its smaller leaf, or for equal leaves the one on the face's negative side.

On a uniform region this is exactly the stitched version's mesh. A chunk is meshed again when the LOD of any of its 26 neighbors changes (`VoxelOctreeNode::compute_boundaries`), as its border vertices depend on their leaves. The topology tests check that the mesh is closed across all LOD seams.
