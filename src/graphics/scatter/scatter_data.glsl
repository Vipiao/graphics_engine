// scatter_data.glsl
// Mirrors the C++ structs of the same names (ScatterConfig.h, ScatterHandler.h).
// The SCATTER_*_BINDING numbers come from ScatterHandler's addScatterDefines.

struct ScatterItem {
    vec4 positionScale;
    vec4 orientation;
    uint kind;
    uint candidateId;
    uint padding0;
    uint padding1;
};

struct ScatterInstanceData {
    mat3 bodyRotation;
    vec4 bodyScale;
};

struct ScatterPatchData {
    vec3 cameraOffset;
    uint slot;
    uint instance;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct ScatterKindData {
    vec4 color;
    // Colour, normal, material, mask; -1 for none.
    ivec4 textureUnits;
    uint firstMesh;
    uint lodCount;
    float boundingRadius;
    // The impostor quad's mesh, or SCATTER_NO_MESH.
    uint impostorMesh;
};

struct ScatterImpostorData {
    vec4 centreRadius;
    // Colour, normal and depth units; w unused
    ivec4 units;
    // Views per side, pixels per view, border pixels; w unused
    ivec4 atlas;
    // x: band start metres. y: band width metres.
    vec4 fadeBand;
    // xyz: offset at the band's end, mesh frame
    vec4 fadeOffset;
};

// An item's origin from the camera in real metres, which the LOD distances measure.
float scatterItemDistance(vec3 cameraOffset, vec3 position, vec3 bodyScale) {
    return length((cameraOffset + position) * bodyScale);
}

// How far an item at the distance is through the band where a mesh dithers into
// the next: 0 before it, 1 at its end. lodDistance: max distance, crossfade metres.
float scatterHandover(vec2 lodDistance, float distance) {
    return lodDistance.y > 0.0
        ? clamp((distance - lodDistance.x) / lodDistance.y + 1.0, 0.0, 1.0)
        : 0.0;
}

// GL's DrawElementsIndirectCommand.
struct ScatterDrawCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint baseInstance;
};
