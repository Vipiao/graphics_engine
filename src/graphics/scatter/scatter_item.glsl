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
