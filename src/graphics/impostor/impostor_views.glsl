// impostor_views.glsl
//
// Which direction each atlas cell was baked from, shared by the bake and the
// draw. The half-sphere above the mesh (+Z up) maps onto [-1, 1]^2 as a
// hemi-octahedron: centre straight above, edge on the horizon. Cell (i, j) of n
// per side is baked from the direction at its centre.

// Towards the viewer, from the mesh.
vec3 impostorViewDirection(vec2 square) {
    vec2 diamond = 0.5 * vec2(square.x + square.y, square.x - square.y);
    float up = 1.0 - abs(diamond.x) - abs(diamond.y);
    return normalize(vec3(diamond, up));
}

// The inverse, for direction.z >= 0.
vec2 impostorSquare(vec3 direction) {
    vec3 onDiamond = direction / (abs(direction.x) + abs(direction.y) + direction.z);
    return vec2(onDiamond.x + onDiamond.y, onDiamond.x - onDiamond.y);
}

vec3 impostorCellDirection(ivec2 cell, int viewsPerSide) {
    vec2 square = (vec2(cell) + 0.5) / float(viewsPerSide) * 2.0 - 1.0;
    return impostorViewDirection(square);
}

// The picture's axes, the mesh's +Z leaning up. Undefined straight above.
void impostorViewAxes(vec3 direction, out vec3 right, out vec3 up) {
    right = normalize(cross(vec3(0.0, 0.0, 1.0), direction));
    up = cross(direction, right);
}
