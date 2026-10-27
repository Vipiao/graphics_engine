// ScatterConfig.h
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "../impostor/ImpostorConfig.h"

// What a caller describes to the scatter renderer. Free of GL types.
//
//    kind       a type of item, e.g. one tree species, with a mesh per LOD
//    patch      a caller-chosen region holding up to a fixed number of items,
//               with an origin in the body's frame and a payload
//    payload    four numbers only the caller's snippet reads
//    candidate  a possible item in a patch; its index is the item's identity
//    item       a kept candidate: position, rotation, scale and kind

struct ScatterLod {
    std::string m_modelPath;
    // Metres; used from the previous LOD's distance up to this
    double m_maxDistance{0.0};
};

// A kind's far stand-in: a baked picture of its LOD 0 (ImpostorBaker).
struct ScatterImpostor {
    // Metres; used from the last LOD's distance up to this
    double m_maxDistance{0.0};
    // Over this many metres before m_maxDistance the impostor shrinks to nothing
    // and moves by m_fadeOffsetMetres, so items leave the far edge gradually.
    // 0 for none.
    double m_fadeBandMetres{0.0};
    // How far it has moved at the end of the band, in the mesh's frame, before
    // the item's scale
    glm::dvec3 m_fadeOffsetMetres{0.0};
    ImpostorBakeSpec m_bake;
};

struct ScatterKind {
    // Nearest first. Not drawn past the last one (or the impostor).
    std::vector<ScatterLod> m_lods;
    // Decoded by material_decode.glsl, as for every other mesh
    glm::dvec4 m_color{1.0, 1.0, 1.0, 1.0};
    // Empty for none. Material: R emissive mask, G roughness, B metallic.
    std::string m_colorTexturePath;
    std::string m_normalTexturePath;
    std::string m_materialTexturePath;
    std::string m_maskTexturePath;
    std::optional<ScatterImpostor> m_impostor;
};

struct ScatterSetConfig {
    // An item names its kind by index here
    std::vector<ScatterKind> m_kinds;
    // The item buffer is m_maxPatches fixed blocks of m_itemsPerPatch, so patches
    // come and go without moving each other's items
    uint32_t m_maxPatches{0};
    uint32_t m_itemsPerPatch{0};

    // The furthest any kind is drawn
    double drawDistance() const {
        double distance{0.0};
        for (const ScatterKind& kind : m_kinds) {
            if (!kind.m_lods.empty() && kind.m_lods.back().m_maxDistance > distance) {
                distance = kind.m_lods.back().m_maxDistance;
            }
            if (kind.m_impostor && kind.m_impostor->m_maxDistance > distance) {
                distance = kind.m_impostor->m_maxDistance;
            }
        }
        return distance;
    }
};

// One item as the GPU stores it, mirroring scatter_data.glsl. Made and read on
// the GPU only. Float, so positions are relative to the patch origin.
struct ScatterItem {
    // xyz: metres from the patch origin, body frame. w: uniform scale.
    glm::vec4 m_positionScale{0.0f, 0.0f, 0.0f, 1.0f};
    // Quaternion (x, y, z, w), mesh -> body, as fromQuaternion reads it
    glm::vec4 m_orientation{0.0f, 0.0f, 0.0f, 1.0f};
    uint32_t m_kind{0};
    // Stable identity; the item's place in the buffer is not
    uint32_t m_candidateId{0};
    uint32_t m_padding[2]{};
};
static_assert(sizeof(ScatterItem) == 48,
              "ScatterItem must match the 48-byte std430 layout the scatter shaders read");
