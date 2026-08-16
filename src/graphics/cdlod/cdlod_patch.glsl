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
//
// A vertex leaves here as an offset from the camera, built in the wide float of
// shared_shaders/dekker_arithmetic.glsl. A planet's own frame puts its points
// 6.4e6 metres out, where an ordinary float's spacing is half a metre;
// subtracting the camera first leaves the vertex's own distance, where the same
// relative error is a fraction of a pixel. The body's world position never
// appears -- it is what cancels when the camera is written in the body's frame.

#include "../shared_shaders/dekker_arithmetic.glsl"

// Everything true of a whole body, as CdlodHandler uploads it. Mirrors
// CdlodHandler::CdlodInstanceData, which the static_assert there pins to this
// layout. It is read rather than passed as uniforms so that every instance
// sharing a surface can be drawn in one instanced call.
struct CdlodInstanceData {
   vec4 baseColor;
   // The camera in this body's own frame, Dekker split: body-sized, and the term
   // every vertex is measured against.
   vec4 cameraBodyPositionHigh;
   vec4 cameraBodyPositionLow;
   // Body -> world rotation at this frame's pose, built on the CPU. The camera
   // above was placed by inverting exactly this matrix, so applying it undoes
   // that placement exactly. Rebuilding it here would agree only to a part in ten
   // million, which against the camera's distance from a planet's centre is most
   // of a metre of rigid slide, redrawn every frame the body turns.
   mat3 bodyRotation;
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

// A patch coordinate placed in the node's own frame, as a displacement from the
// node's centre: half of the crude point, and the half that is small. Affine in
// patchUv, which is what lets it be interpolated across a triangle exactly.
//
// Kept apart from the centre rather than added to it, because the two want
// different treatment everywhere they go. The axes are half a patch edge, and
// the tree keeps a patch within twice its own range, so they scale with distance
// and their last bits are a fixed fraction of a pixel. The centre stays
// body-sized however small the patch gets.
vec3 cdlodPatchOffset(vec2 patchUv, vec3 uAxis, vec3 vAxis) {
   return (2.0 * patchUv.x - 1.0) * uAxis
        + (2.0 * patchUv.y - 1.0) * vAxis;
}

// The injected surface body turns a crude point into geometry:
//
//    Df3  cdlodSurfacePoint(Df3 crudePoint);
//    vec3 cdlodSurfaceNormal(Df3 crudePoint, vec3 crudeDerivX, vec3 crudeDerivY);
//
// The normal is given how far the crude point moves to the neighbouring pixels,
// which is what a body sampling a texture needs to choose a filter width by. It
// arrives from the caller rather than being differenced inside the body because
// only one of the two stages has screen derivatives at all, and the body is
// compiled into both.
//
// crudePoint is a point of the solid the caller's root frames came off, in the
// body's own frame and in metres; what that solid is and what surface it maps to
// are the caller's entirely. Two functions rather than one because the vertex
// stage places geometry and the fragment stage shades, and they differ in what
// they return: a position is body-sized and needs the width, a normal is only a
// direction.
//
// Both take the point wide, including the one that returns a direction. A normal
// is tolerant of where it is measured only until the surface is asked about
// detail finer than the error: a body-sized float steps half a metre, and a
// surface reading a map at millimetre texels would be sampling a lattice rather
// than itself.
//
// Df3 and the arithmetic on it are already in scope here, so the body uses them
// without naming a path: it is spliced in below the include above, and a snippet
// lives outside this repository with no relative path that would reach the file.
// It is src/graphics/shared_shaders/dekker_arithmetic.glsl in the graphics
// engine, for a snippet author who wants to read what is available.
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
float cdlodMorphWeight(vec3 restOffset, float patchEdge, int patchLevel,
                       float lodRangeFactor) {
   if (patchLevel == 0) return 0.0;  // a root has no parent to merge into

   float ownRange = lodRangeFactor * patchEdge;
   float morphEnd = 2.0 * ownRange;
   float morphStart = mix(ownRange, morphEnd, cdlodMorphStartFraction(lodRangeFactor));

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

   return clamp((length(restOffset) - morphStart) / (morphEnd - morphStart), 0.0, 1.0);
}

// The vertex both stages draw, as an offset from the camera: its length is how
// far away the vertex is, so the float operations after it cost a fixed fraction
// of what it subtends.
//
// crudeOffset comes back for the shading stage, which pairs it with the node
// centre it was measured from. It leaves as a displacement rather than a point
// because only a displacement survives the trip: a point is body-sized, and the
// interpolator between the stages works in float whatever is handed to it.
//
// Shared so the depth pass cannot place a vertex anywhere the G-buffer pass does
// not. The surface is evaluated twice: unmorphed to find how far this vertex is,
// then at the coordinate that distance chose -- the morph follows from the
// distance, so the distance cannot follow from the morph.
vec3 cdlodBuildVertex(int vertexId, Df3 centre, vec3 uAxis, vec3 vAxis,
                      int patchLevel, CdlodInstanceData instance, out vec3 crudeOffset) {
   // Already a high and a low part in the buffer, so it is the wide float itself
   // rather than something to be assembled into one.
   Df3 cameraBodyPosition = Df3(instance.cameraBodyPositionHigh.xyz,
                                instance.cameraBodyPositionLow.xyz);
   // Grid coordinate of this vertex within the patch, in whole vertices.
   ivec2 grid = ivec2(vertexId % u_patchVertices, vertexId / u_patchVertices);

   vec3 restPatchOffset = cdlodPatchOffset(cdlodPatchGridUv(grid, 0.0), uAxis, vAxis);
   Df3 restCrude = df3AddVec(centre, restPatchOffset);
   vec3 restCameraOffset =
      df3ToVec(df3Sub(cdlodSurfacePoint(restCrude), cameraBodyPosition));

   float patchEdge = 2.0 * length(uAxis);
   float morphK =
      cdlodMorphWeight(restCameraOffset, patchEdge, patchLevel, instance.lodRangeFactor);

   // At zero the morphed coordinate is the rest coordinate down to the bit, so
   // asking the surface again would return what it just returned. The band is a
   // narrow part of the range a leaf lives in, so most patches are below it
   // entirely and take this together rather than vertex by vertex.
   if (morphK == 0.0) {
      crudeOffset = restPatchOffset;
      return restCameraOffset;
   }

   vec3 morphedPatchOffset = cdlodPatchOffset(cdlodPatchGridUv(grid, morphK), uAxis, vAxis);

   crudeOffset = morphedPatchOffset;
   return df3ToVec(df3Sub(cdlodSurfacePoint(df3AddVec(centre, morphedPatchOffset)),
                          cameraBodyPosition));
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
