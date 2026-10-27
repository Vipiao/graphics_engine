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
    // Colour unit, normal unit, views per side, pixels per view
    ivec4 atlas;
    // x: border pixels
    ivec4 border;
    // x: band start metres. y: band width metres.
    vec4 fadeBand;
    // xyz: offset at the band's end, mesh frame
    vec4 fadeOffset;
};

// GL's DrawElementsIndirectCommand.
struct ScatterDrawCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    int baseVertex;
    uint baseInstance;
};
