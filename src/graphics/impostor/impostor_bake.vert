// impostor_bake.vert
#version 460 core

// Orthographic view from the cell's direction, framing the mesh's sphere.
// Reverse-Z: depth runs from 0 at the sphere's back to 1 at its front.

#include "impostor_views.glsl"

layout (location = 0) in vec3 position;
layout (location = 1) in vec3 normal;
layout (location = 2) in vec3 tangent;
layout (location = 3) in vec2 uv;
layout (location = 4) in float layer;

uniform ivec2 u_cell;
uniform int u_viewsPerSide;
uniform vec3 u_centre;
uniform float u_radius;

// In the mesh's frame, as the atlas holds them
out vec3 vert_normal;
out mat3 vert_TBN;
// z: the texture layer
out vec3 vert_uv;

void main() {
    vec3 direction = impostorCellDirection(u_cell, u_viewsPerSide);
    vec3 right;
    vec3 up;
    impostorViewAxes(direction, right, up);

    vec3 fromCentre = position - u_centre;
    gl_Position = vec4(dot(fromCentre, right) / u_radius, dot(fromCentre, up) / u_radius,
                       0.5 + 0.5 * dot(fromCentre, direction) / u_radius, 1.0);

    vert_normal = normal;
    vert_TBN = mat3(tangent, cross(normal, tangent), normal);
    vert_uv = vec3(uv, layer);
}
