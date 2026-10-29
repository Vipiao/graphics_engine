// scatter_impostor.vert
#version 460 core

// One corner of an item's impostor: a quad facing the camera, upright on the
// screen, blending the four atlas views nearest the camera's direction. Each
// view is sampled by projecting the corner onto the axes it was baked with.

#include "scatter_item.glsl"
#include "../impostor/impostor_views.glsl"

layout(std430, binding = SCATTER_IMPOSTOR_BINDING) readonly buffer ScatterImpostorBuffer {
    ScatterImpostorData impostors[];
};

// In [-1, 1]^2
layout (location = 0) in vec3 position;

uniform mat4 view;
uniform mat4 projection;

// Per view: atlas uv, cell rectangle (min uv, max uv), blend weight
out vec2 vert_atlasUV[4];
flat out vec4 vert_cellRect[4];
flat out vec4 vert_weights;
flat out mat3 vert_meshToView;
// Colour, normal and depth atlas units
flat out ivec3 vert_atlasUnits;
// View z on the plane through the sphere's centre, and how far the depth atlas
// moves it at the sphere's front
out float vert_viewDepth;
flat out float vert_depthReach;
flat out float vert_atlasSide;
flat out float vert_maxLod;
flat out vec3 vert_crossfade;

void main() {
    const ScatterDrawnItem drawn = scatterDrawnItem();
    const ScatterImpostorData impostor = impostors[drawn.item.kind];
    const vec3 centre = impostor.centreRadius.xyz;

    const float radius = impostor.centreRadius.w;
    const mat3 meshFromWorld = transpose(drawn.instance.bodyRotation * drawn.rotation);

    // In the mesh's frame
    const vec3 fromCamera = scatterBodyOffset(drawn, centre);
    const vec3 toCamera = dot(fromCamera, fromCamera) > 0.0
        ? normalize(transpose(drawn.rotation) * -fromCamera)
        : vec3(1.0, 0.0, 0.0);

    // From below the horizon, the horizon's views are the nearest
    vec3 viewed = vec3(toCamera.xy, max(toCamera.z, 0.0));
    viewed = dot(viewed, viewed) > 0.0 ? normalize(viewed) : vec3(1.0, 0.0, 0.0);
    // Position among the cell centres: the four cells around it and the weights
    const int viewsPerSide = impostor.atlas.x;
    const vec2 square = impostorSquare(viewed);
    const vec2 inCentres = (square * 0.5 + 0.5) * float(viewsPerSide) - 0.5;
    const ivec2 firstCell = ivec2(floor(inCentres));
    const vec2 toNext = inCentres - vec2(firstCell);

    // The screen's up is never along the line to the camera
    const vec3 screenUp = meshFromWorld * vec3(view[0][1], view[1][1], view[2][1]);
    const vec3 quadRight = normalize(cross(screenUp, toCamera));
    const vec3 quadUp = cross(toCamera, quadRight);

    const vec2 corner = position.xy;
    const vec3 fromCentre = (corner.x * quadRight + corner.y * quadUp) * radius;
    const vec3 quadPoint = centre + fromCentre;

    // In the far band: shrink about the origin and move by the offset
    const float distance = scatterDistance(drawn);
    const float intoBand = impostor.fadeBand.y > 0.0
        ? clamp((distance - impostor.fadeBand.x) / impostor.fadeBand.y, 0.0, 1.0)
        : 0.0;
    const vec3 meshPoint =
        quadPoint * (1.0 - intoBand) + impostor.fadeOffset.xyz * intoBand;
    const vec3 cameraRelative = scatterCameraRelative(drawn, meshPoint);
    const vec4 viewPosition = view * vec4(cameraRelative, 1.0);
    const vec3 reach = scatterCameraRelative(
        drawn, meshPoint + toCamera * radius * (1.0 - intoBand)) - cameraRelative;
    vert_viewDepth = viewPosition.z;
    vert_depthReach = (mat3(view) * reach).z;
    // Moved along the camera's rays to the sphere's front: the same on screen,
    // and in front of the surface, which the fragments only push back
    const vec3 meshCentre = centre * (1.0 - intoBand) + impostor.fadeOffset.xyz * intoBand;
    const float toFront =
        max(1.0 - length(reach) / length(scatterCameraRelative(drawn, meshCentre)), 0.0);
    gl_Position = projection * vec4(viewPosition.xyz * toFront, 1.0);

    const float viewPixels = float(impostor.atlas.y);
    const float border = float(impostor.atlas.z);
    const float side = float(viewsPerSide) * viewPixels;
    for (int nearest = 0; nearest < 4; ++nearest) {
        const ivec2 offset = ivec2(nearest & 1, nearest >> 1);
        // Past the square's edge, the edge view counts twice
        const ivec2 cell = clamp(firstCell + offset, 0, viewsPerSide - 1);
        const vec2 share = mix(1.0 - toNext, toNext, vec2(offset));
        vert_weights[nearest] = share.x * share.y;

        vec3 pictureRight;
        vec3 pictureUp;
        impostorViewAxes(impostorCellDirection(cell, viewsPerSide), pictureRight, pictureUp);
        // In [-1, 1]^2 across the picture
        const vec2 onPicture =
            vec2(dot(fromCentre, pictureRight), dot(fromCentre, pictureUp)) / radius;

        const vec2 cellMin = vec2(cell) * viewPixels;
        vert_atlasUV[nearest] =
            (cellMin + border + (onPicture * 0.5 + 0.5) * (viewPixels - 2.0 * border)) / side;
        vert_cellRect[nearest] = vec4(cellMin / side, (cellMin + viewPixels) / side);
    }

    vert_meshToView = mat3(view) * drawn.instance.bodyRotation * drawn.rotation;
    vert_atlasUnits = impostor.units.xyz;
    vert_atlasSide = side;
    // The coarsest mip whose cells are whole texels and at least four across, so
    // no texel straddles two views
    vert_maxLod = float(min(findLSB(impostor.atlas.y), findMSB(impostor.atlas.y / 4)));
    vert_crossfade = scatterCrossfade(drawn, kinds[drawn.item.kind].lodCount);
}
