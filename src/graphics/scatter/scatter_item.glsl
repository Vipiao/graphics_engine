// scatter_item.glsl
// Which item a scatter draw instance is, and where a point of its mesh lands.
// gl_BaseInstance is the start of the mesh's part of the visible list.

#include "../shared_shaders/mesh_transform.glsl"
#include "scatter_data.glsl"

layout(std430, binding = SCATTER_ITEM_BINDING) readonly buffer ScatterItemBuffer {
    ScatterItem items[];
};
layout(std430, binding = SCATTER_INSTANCE_BINDING) readonly buffer ScatterInstanceBuffer {
    ScatterInstanceData instances[];
};
layout(std430, binding = SCATTER_PATCH_BINDING) readonly buffer ScatterPatchBuffer {
    ScatterPatchData patches[];
};
layout(std430, binding = SCATTER_VISIBLE_BINDING) readonly buffer ScatterVisibleBuffer {
    uvec2 visible[];
};
layout(std430, binding = SCATTER_KIND_BINDING) readonly buffer ScatterKindBuffer {
    ScatterKindData kinds[];
};
layout(std430, binding = SCATTER_LOD_DISTANCE_BINDING) readonly buffer ScatterLodDistanceBuffer {
    vec2 lodDistances[];
};

struct ScatterDrawnItem {
    ScatterItem item;
    ScatterPatchData patchData;
    ScatterInstanceData instance;
    // The item's rotation, mesh -> body
    mat3 rotation;
};

ScatterDrawnItem scatterDrawnItem() {
    const uvec2 entry = visible[gl_BaseInstance + gl_InstanceID];
    ScatterDrawnItem drawn;
    drawn.item = items[entry.x];
    drawn.patchData = patches[entry.y];
    drawn.instance = instances[drawn.patchData.instance];
    drawn.rotation = fromQuaternion(drawn.item.orientation);
    return drawn;
}

// A mesh point from the camera, in the body's frame and units. Every term is
// small near the camera.
vec3 scatterBodyOffset(ScatterDrawnItem drawn, vec3 meshPoint) {
    return drawn.patchData.cameraOffset + drawn.item.positionScale.xyz
         + drawn.rotation * (meshPoint * drawn.item.positionScale.w);
}

// The same in the world, relative to the camera.
vec3 scatterCameraRelative(ScatterDrawnItem drawn, vec3 meshPoint) {
    return drawn.instance.bodyRotation
         * (scatterBodyOffset(drawn, meshPoint) * drawn.instance.bodyScale.xyz);
}

float scatterDistance(ScatterDrawnItem drawn) {
    return scatterItemDistance(drawn.patchData.cameraOffset, drawn.item.positionScale.xyz,
                               drawn.instance.bodyScale.xyz);
}

// The item's shift of the dither noise, so neighbours do not thin in step
float scatterDitherShift(ScatterDrawnItem drawn) {
    return fract(float(drawn.item.candidateId) * 0.618034);
}

// For dither.glsl: the noise range a mesh keeps as it dithers out (x up) and the
// previous LOD dithers out of it (y down), shifted per item. lod is the kind's
// lodCount for its impostor.
vec3 scatterCrossfade(ScatterDrawnItem drawn, uint lod) {
    const ScatterKindData kind = kinds[drawn.item.kind];
    const float distance = scatterDistance(drawn);
    const uint mesh = lod < kind.lodCount ? kind.firstMesh + lod : kind.impostorMesh;
    const float fadeIn =
        lod > 0u ? scatterHandover(lodDistances[kind.firstMesh + lod - 1u], distance) : 1.0;
    return vec3(scatterHandover(lodDistances[mesh], distance), fadeIn,
                scatterDitherShift(drawn));
}

// The same for a mesh with nothing dithering into it, only out over its band
vec3 scatterFadeOut(ScatterDrawnItem drawn, uint mesh) {
    return vec3(scatterHandover(lodDistances[mesh], scatterDistance(drawn)), 1.0,
                scatterDitherShift(drawn));
}
