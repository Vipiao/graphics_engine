// cdlod_patch.glsl
//
// Shared patch construction for the CDLOD stages. Turns a vertex of the abstract
// patch grid, plus the per-instance node record, into a point of the crude solid
// the caller subdivided, and hands that point to the injected surface body. What
// shape comes back is the surface body's business alone.
//
// Patch vertices carry no attributes: the grid coordinate is derived from
// gl_VertexID, which under glDrawElements is the value fetched from the index
// buffer built by CdlodPatchGeometry. The grid's size arrives as a uniform
// rather than a constant here, so CdlodPatchGeometry::k_patchVertices stays the
// single place it is decided -- a copy in this file could drift out of step with
// the index buffer and silently fold the patch onto itself.

// Everything true of a whole body, as CdlodHandler uploads it. Mirrors
// CdlodHandler::CdlodInstanceData, which the static_assert there pins to this
// layout. It is read rather than passed as uniforms so that every instance
// sharing a surface can be drawn in one instanced call.
struct CdlodInstanceData {
   vec4 baseColor;
   vec4 cameraBodyPosition;  // xyz: the camera in this body's own frame
   float lodRangeFactor;
   int ssboIndex;            // the body's world transform, in the shared SSBO
   ivec2 padding;
};

layout(std430, binding = 1) readonly buffer CdlodInstanceBuffer {
   CdlodInstanceData cdlodInstanceBuffer[];
};

// Where in a leaf's distance band the morph begins, as a fraction of the band.
// Derived from the range factor rather than chosen, since a merge is seamless
// only if the morph has finished when it fires: the merge tests the parent's
// nearest point, the morph is per vertex, and the furthest vertex of a square
// patch sits sqrt(2) edge lengths beyond that point. Requiring it to be at
// morph 0 there gives
//
//    f * edge + sqrt(2) * edge <= f * edge * (1 + fraction)
//
// Below sqrt(2) / f the parent renders itself partly morphed the instant it takes
// over from children that handed it an unmorphed mesh, and the surface steps
// open. The band left to morph in is edge * (f - sqrt(2)), which is why
// CdlodConfig floors f at sqrt(2).
float cdlodMorphStartFraction(float lodRangeFactor) {
   return sqrt(2.0) / lodRangeFactor;
}

// The patch grid both vertex stages walk. It lives here rather than in each leaf
// shader because the depth stage has to morph a vertex to the exact same place
// the G-buffer stage does -- were it declared separately, the shadow map could
// disagree with the surface casting it.
uniform int u_patchVertices;

// Grid coordinate of this vertex within the patch, in whole vertices.
ivec2 cdlodPatchVertexGrid(int vertexId) {
   return ivec2(vertexId % u_patchVertices, vertexId / u_patchVertices);
}

// The patch's [0, 1] coordinate, with odd-indexed vertices -- exactly the ones
// the parent patch does not have -- slid onto their even neighbour as morphK
// reaches 1. At 1 the odd vertices coincide with the even ones, the triangles
// between them go degenerate, and what is left is precisely the parent's mesh.
// Parity is tested on the integer index rather than by taking fract() of a
// scaled coordinate, so it stays exact whatever the patch size.
vec2 cdlodPatchGridUv(ivec2 grid, float morphK) {
   vec2 isOdd = vec2(grid & ivec2(1));
   return (vec2(grid) - isOdd * morphK) / float(u_patchVertices - 1);
}

// A patch coordinate placed in the node's own frame: the crude point, in the
// space the quadtree subdivides and before the surface has said anything about
// where it goes. Affine in patchUv, which is what lets it be interpolated across
// a triangle exactly.
vec3 cdlodCrudePoint(vec2 patchUv, vec3 centre, vec3 uAxis, vec3 vAxis) {
   return centre + (2.0 * patchUv.x - 1.0) * uAxis
                 + (2.0 * patchUv.y - 1.0) * vAxis;
}

// The injected surface body turns a crude point into geometry:
//
//    vec3 cdlodSurfacePoint(vec3 crudePoint);
//    vec3 cdlodSurfaceNormal(vec3 crudePoint);
//
// crudePoint is a point of the solid the caller's root frames came off, in the
// body's own frame and in metres; what that solid is and what surface it maps to
// are the caller's entirely. Two functions rather than one because the vertex
// stage places geometry and the fragment stage shades.
//
// Both must be pure functions of crudePoint -- the level is deliberately not
// passed. A patch morphs its shared edge onto a coarser neighbour's vertices, and
// the seam closes only because both evaluate this at the same point.
//
// What they draw must stay inside the ICdlodPatchBounds given to the tree.
__CDLOD_SURFACE_BODY__

// How far this vertex has been pulled toward the parent patch.
//
// A leaf lives in a bounded distance band: it did not split, so it is at least
// its own split range away, and its parent did split, so it is nearer than the
// parent's range -- which is exactly twice its own. This maps the far part of
// that band onto 0..1, so a patch has become geometrically identical to its
// parent by the time the parent merges it, and the merge changes nothing on
// screen. The same reasoning closes the cracks: a point on the edge shared with
// a coarser neighbour belongs to that neighbour too, so it is at least the
// neighbour's split range away, which is where this reaches 1.
//
// Driven by the vertex's own distance rather than the patch's. A patch meeting a
// coarser neighbour on one edge must be fully morphed there while still showing
// detail on the edge facing the camera, and one value per patch would instead
// make same-level neighbours disagree along their shared edge.
float cdlodMorphWeight(vec3 restPosition, float patchEdge, int patchLevel,
                       CdlodInstanceData instance) {
   if (patchLevel == 0) return 0.0;  // a root has no parent to merge into

   float ownRange = instance.lodRangeFactor * patchEdge;
   float morphEnd = 2.0 * ownRange;
   float morphStart = mix(ownRange, morphEnd,
                          cdlodMorphStartFraction(instance.lodRangeFactor));

   // Both ends pulled in by this much of the band. The two above are derived for
   // a patch lying flat in its own frame, and a body's bounds are free to be
   // looser than that -- terrain lifts a patch off the shape they were measured
   // on, so it reaches a little further than the split was decided for. Spent at
   // both ends because either one running short reopens a seam: the coarse side
   // must not have begun morphing where a finer neighbour meets it, and the fine
   // side must have finished.
   const float k_bandMargin = 0.2;
   float bandEnd = morphEnd;
   morphEnd = mix(morphEnd, morphStart, k_bandMargin);
   morphStart = mix(morphStart, bandEnd, k_bandMargin);

   return clamp((distance(restPosition, instance.cameraBodyPosition.xyz) - morphStart) /
                (morphEnd - morphStart), 0.0, 1.0);
}

// The patch coordinate this vertex actually renders at. Both vertex stages go
// through here, so the depth pass cannot morph differently from the G-buffer one.
vec2 cdlodMorphedPatchUv(int vertexId, vec3 centre, vec3 uAxis, vec3 vAxis,
                         int patchLevel, CdlodInstanceData instance) {
   ivec2 grid = cdlodPatchVertexGrid(vertexId);
   // Measured unmorphed: the morph follows from this distance, so the distance
   // cannot be allowed to follow from the morph.
   vec3 restPosition = cdlodSurfacePoint(
      cdlodCrudePoint(cdlodPatchGridUv(grid, 0.0), centre, uAxis, vAxis));
   float patchEdge = 2.0 * length(uAxis);
   return cdlodPatchGridUv(grid,
                           cdlodMorphWeight(restPosition, patchEdge, patchLevel, instance));
}

// Distinct hue per quadtree level, for the debug view. Reading the tree's state
// off the screen is the whole point, so the steps are far apart rather than a ramp.
vec3 cdlodLevelColor(int patchLevel) {
   const vec3 k_levelPalette[6] = vec3[6](
      vec3(0.90, 0.30, 0.25), vec3(0.95, 0.70, 0.20), vec3(0.45, 0.85, 0.35),
      vec3(0.30, 0.75, 0.90), vec3(0.45, 0.45, 0.95), vec3(0.90, 0.45, 0.85)
   );
   return k_levelPalette[patchLevel % 6];
}
