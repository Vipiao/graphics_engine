// cdlod_patch.glsl
//
// Shared patch construction for the CDLOD vertex stages. Turns a vertex of the
// abstract patch grid, plus the per-instance node record, into a point on the
// body's sphere, and hands that point to the injected surface body.
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
   float halfExtent;
   float lodRangeFactor;
   int ssboIndex;            // the body's world transform, in the shared SSBO
   int padding;
};

layout(std430, binding = 1) readonly buffer CdlodInstanceBuffer {
   CdlodInstanceData cdlodInstanceBuffer[];
};

// Where in a leaf's distance band the morph begins, as a fraction of the band.
// Later keeps full detail for longer but ramps more steeply; earlier is gentler
// but leaves the surface softer than its level nominally promises.
//
// Must not go below about 0.44. A merge is seamless only while the parent is
// still at morph 0 as its children vanish, and the merge fires on the parent's
// nearest point while the morph is per vertex: its furthest vertex sits at
// 1.435x the split range, so a morph starting any earlier than that has already
// begun when the children go, and the surface steps.
const float k_morphStartFraction = 0.5;

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

// The body's base shape: a patch coordinate placed in the node's own frame and
// projected onto the sphere, in the body's own frame and in metres. The frame
// arrives with the node record, so nothing here knows what solid the patch came
// off -- only that the result is projected onto a sphere.
//
// CdlodTree::projectedPoint is the CPU twin of this and has to agree with it,
// or selection measures distances to a surface that is not where it is drawn.
// Everything the tree decides -- splits, merges, morph weights -- is measured
// here, on the smooth sphere, never on the displaced surface. That is what lets
// the surface body displace by any amount without disturbing the distance
// bounds those decisions rest on.
vec3 cdlodPatchPoint(vec2 patchUv, vec3 centre, vec3 uAxis, vec3 vAxis, float halfExtent) {
   vec3 basePoint = centre + (2.0 * patchUv.x - 1.0) * uAxis
                           + (2.0 * patchUv.y - 1.0) * vAxis;
   return normalize(basePoint) * halfExtent;
}

// The injected surface body defines a radial height field over the sphere:
//
//    float cdlodSurfaceElevation(vec3 spherePosition);
//    vec3 cdlodSurfaceGradient(vec3 spherePosition);
//
// spherePosition is a point on the body's sphere in the body's own frame, in
// metres; the body is centred on the origin, so the outward direction there is
// just its own normalized value. The first returns metres of displacement along
// that direction, the second the gradient of that same height field.
//
// A scalar height rather than a displaced position on purpose: it is what makes
// "the normal is the gradient of the surface" a statement that can be true. A
// body free to move a point sideways would have no such relationship, and its
// two functions could disagree with nothing to catch it.
//
// The two are separate because the stages that need them are -- the vertex
// stage places geometry, the fragment stage shades -- and a stage compiles only
// the one it calls. Assembling position and normal from the pair is done below
// rather than here, so a surface writes only the height field and its
// derivative and never repeats the geometry.
//
// Both must be pure functions of spherePosition. The level and size of the node
// are deliberately not passed: a patch meeting a coarser neighbour morphs its
// shared edge onto that neighbour's vertices, and the seam closes only because
// both patches then evaluate this at the same point and get the same answer. A
// body that could see which level it was being drawn at could reopen every seam
// while looking perfectly reasonable.
__CDLOD_SURFACE_BODY__

// The surface point above a point on the sphere: displaced along the outward
// direction by the height field.
vec3 cdlodDisplacedPosition(vec3 spherePosition) {
   vec3 sphereNormal = normalize(spherePosition);
   return spherePosition + sphereNormal * cdlodSurfaceElevation(spherePosition);
}

// The unit normal of that displaced surface.
//
// Only the part of the gradient lying in the sphere's tangent plane tilts the
// normal; the radial part moves the point in or out without turning it. The
// 1 + height/radius term is the stretch of the tangent vectors as the surface
// rises off the sphere, which is what keeps this exact rather than a small
// angle approximation.
vec3 cdlodDisplacedNormal(vec3 spherePosition) {
   vec3 sphereNormal = normalize(spherePosition);
   float height = cdlodSurfaceElevation(spherePosition);
   vec3 gradient = cdlodSurfaceGradient(spherePosition);

   vec3 tangentialGradient = gradient - sphereNormal * dot(sphereNormal, gradient);
   float radius = length(spherePosition);
   return normalize(sphereNormal - tangentialGradient / (1.0 + height / radius));
}

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
   float morphStart = mix(ownRange, morphEnd, k_morphStartFraction);
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
   vec3 restPosition = cdlodPatchPoint(cdlodPatchGridUv(grid, 0.0), centre, uAxis, vAxis,
                                       instance.halfExtent);
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
