// cdlod_vertex_shader.vert
#version 460 core

#include "../shared_shaders/mesh_transform.glsl"
#include "cdlod_patch.glsl"

layout(std430, binding = 0) buffer MeshDataBuffer {
   MeshData meshDataBuffer[];
};

// No per-vertex attributes: the patch grid comes from gl_VertexID.
// Per-node attributes (from the shared instance VBO). Locations start at 4,
// leaving 0..3 free so the layout stays readable against the instanced-geometry
// shaders. The centre arrives Dekker split, being the one body-sized term a patch
// carries. Location 9 carries the CDLOD instance rather than the SSBO index those
// shaders have there: everything true of the body is reached through it.
layout (location = 4) in vec3 patchCentreHigh;
layout (location = 5) in vec3 patchCentreLow;
layout (location = 6) in vec3 patchUAxis;
layout (location = 7) in vec3 patchVAxis;
layout (location = 8) in int patchLevel;
layout (location = 9) in int instanceIndex;

uniform mat4 view;
uniform mat4 projection;

// u_patchVertices and the instance buffer are declared by cdlod_patch.glsl, which
// every stage consulting the surface shares.
// Tints each quadtree level differently so the debug view shows which level a
// patch was selected at, not just where its edges are.
uniform bool u_colorByLevel;

// The crude point this vertex was built from, split so the fragment stage can
// rebuild it at full width and ask the surface for the normal there, shading per
// pixel rather than per vertex.
//
// Split because the interpolator works in float: it blends the three vertices
// per pixel and rounds the result to a float at that result's own magnitude, so
// a body-sized value arrives quantized to half a metre however exactly it was
// sent. The centre is therefore passed flat -- every vertex of a patch carries
// the same instance attribute, so flat delivers those bits untouched -- and only
// the displacement within the patch is interpolated. That one is bounded by the
// patch's half diagonal, which the tree keeps proportional to camera distance,
// so rounding it costs a fixed fraction of a pixel.
flat out vec3 vert_patchCentreHigh;
flat out vec3 vert_patchCentreLow;
out vec3 vert_crudeOffset;
// Body -> view rotation, for taking that normal into the space the G-buffer
// stores. Constant across a body, so it is passed flat rather than interpolated.
flat out mat3 vert_bodyToView;
out vec4 vert_color;
flat out float vert_emissiveScalar;

void main() {
   CdlodInstanceData instance = cdlodInstanceBuffer[instanceIndex];
   MeshData meshData = meshDataBuffer[instance.ssboIndex];

   // Patch vertex -> crude point -> the surface, already relative to the camera.
   vec3 crudeOffset;
   vec3 cameraOffset = cdlodBuildVertex(
      gl_VertexID, Df3(patchCentreHigh, patchCentreLow),
      patchUAxis, patchVAxis, patchLevel, instance, crudeOffset);

   // The pose acts on that offset rather than on a body-frame position, so its
   // last bits cost a fraction of the vertex's distance instead of half a metre
   // of planet. The world position and centre of rotation cancel: the camera was
   // placed by inverting this very rotation, and putting it back leaves the
   // eye-to-vertex vector the view matrix wants.
   //
   // Interpolated on the CPU, unlike the meshes sharing this SSBO: selection
   // already builds this pose there, and a second answer could disagree.
   vec3 worldOffset = instance.bodyRotation * (cameraOffset * meshData.scale.xyz);

   vert_patchCentreHigh = patchCentreHigh;
   vert_patchCentreLow = patchCentreLow;
   vert_crudeOffset = crudeOffset;
   vert_bodyToView = mat3(view) * instance.bodyRotation;
   vert_color = u_colorByLevel ? vec4(cdlodLevelColor(patchLevel), 1.0) : instance.baseColor;
   vert_emissiveScalar = meshData.emissiveScalar;

   gl_Position = projection * view * vec4(worldOffset, 1.0);
}
