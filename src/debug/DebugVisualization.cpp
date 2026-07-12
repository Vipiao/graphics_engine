// DebugVisualization.cpp
#include "DebugVisualization.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

// Static member initialization
int DebugVisualization::s_nextDebugId = 1;

DebugVisualization::DebugVisualization(InstanceHandler* instanceHandler, SSBOManager* ssboManager) 
    : m_instanceHandler(instanceHandler)
    , m_ssboManager(ssboManager)
    , m_textureIndex(-1)
    , m_resourcesLoaded(false)
{
    if (!m_instanceHandler) {
        throw std::runtime_error("InstanceHandler pointer cannot be null");
    }

    if (!m_ssboManager) {
        throw std::runtime_error("SSBOManager cannot be null");
    }
    
    loadSharedResources();
}

DebugVisualization::~DebugVisualization() {
    // Clean up all active debug spheres
    for (const auto& pair : m_idToMeshIndex) {
        int debugId = pair.first;
        int meshIndex = pair.second;
        
        // Remove instance
        auto instanceIt = m_idToInstance.find(debugId);
        if (instanceIt != m_idToInstance.end()) {
            if (auto geometry = m_sphereGeometry.lock()) {
                geometry->removeInstance(instanceIt->second);
            }
        }
        
        // Deallocate mesh index
        m_ssboManager->deallocateIndex(meshIndex);
    }

    // Textures are automatically cleaned up by TextureManagerBase destructor
    
    if (auto geometry = m_sphereGeometry.lock()) {
        m_instanceHandler->releaseGeometry(geometry);
    }

    m_nameToId.clear();
    m_idToName.clear();
    m_meshProperties.clear();
    m_idToInstance.clear();
    m_idToMeshIndex.clear();
}

void DebugVisualization::loadSharedResources() {
    if (m_resourcesLoaded) return;
    
    try {
        // Load shared sphere geometry; overlay so debug markers draw on top of
        // the scene
        m_sphereGeometry = m_instanceHandler->createGeometry(
            "../media/blender/02_sphere.obj", RenderLayer::Overlay);

        // Load shared texture
        m_textureIndex = m_instanceHandler->createTexture("../media/debug_red_transparent.png");

        m_resourcesLoaded = true;
        std::cout << "Debug visualization shared resources loaded successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to load debug visualization resources: " << e.what() << std::endl;
        m_resourcesLoaded = false;
    }
}

int DebugVisualization::getNextDebugId() {
    return s_nextDebugId++;
}

int DebugVisualization::createSphere(const glm::dvec3& position, double radius) {
    if (!m_resourcesLoaded) {
        std::cerr << "Cannot create debug sphere: shared resources not loaded" << std::endl;
        return -1;
    }
    
    auto geometry = m_sphereGeometry.lock();
    if (!geometry) {
        std::cerr << "Cannot create debug sphere: shared geometry not available" << std::endl;
        return -1;
    }
    
    // Allocate mesh index for SSBO
    int meshIndex;
    try {
        meshIndex = m_ssboManager->allocateIndex();
    } catch (const std::exception& e) {
        std::cerr << "Failed to allocate mesh index for debug sphere: " << e.what() << std::endl;
        return -1;
    }
    
    // Create instance with identity local transforms
    const glm::dvec4 redColor(1.0, 0.0, 0.0, 1.0);  // Red color
    auto instanceWeak = geometry->addInstance(meshIndex, m_textureIndex, -1, -1, redColor);
    auto instance = instanceWeak.lock();
    if (!instance) {
        m_ssboManager->deallocateIndex(meshIndex);
        std::cerr << "Failed to create instance for debug sphere" << std::endl;
        return -1;
    }

    // Set SSBO transform (world position and scale)
    m_ssboManager->updateMeshTransform(
        meshIndex,
        position,
        glm::dvec3(0.0),                      // velocity
        glm::dquat(1.0, 0.0, 0.0, 0.0),      // orientation
        glm::dvec3(0.0, 1.0, 0.0),           // angVelAxis
        0.0,                                  // angVel
        glm::dvec3(0.0),                     // centerOfRotation
        glm::dvec3(radius),                  // scale
        -1,                                   // colorTextureUnit (handled by instance)
        -1,                                   // normalTextureUnit
        -1,                                   // materialTextureUnit
        0,                                    // time
        1.0                                   // emissiveScalar
    );
    
    // Get unique debug ID and store mappings
    int debugId = getNextDebugId();
    m_idToInstance[debugId] = instanceWeak;
    m_idToMeshIndex[debugId] = meshIndex;
    m_meshProperties[debugId] = {position, glm::dquat(1.0, 0.0, 0.0, 0.0), glm::dvec3(radius)};
    
    return debugId;
}

int DebugVisualization::createSphere(const std::string& name, const glm::dvec3& position, double radius) {
    // Remove existing mesh with this name if it exists
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end()) {
        removeMesh(it->second);
    }
    
    // Create the sphere
    int debugId = createSphere(position, radius);
    
    if (debugId >= 0) {
        // Store name mapping
        m_nameToId[name] = debugId;
        m_idToName[debugId] = name;
    }
    
    return debugId;
}

void DebugVisualization::removeMesh(const std::string& name) {
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end()) {
        removeMesh(it->second);
    }
}

void DebugVisualization::removeMesh(int id) {
    // Remove instance
    auto instanceIt = m_idToInstance.find(id);
    if (instanceIt != m_idToInstance.end()) {
        if (auto geometry = m_sphereGeometry.lock()) {
            geometry->removeInstance(instanceIt->second);
        }
        m_idToInstance.erase(instanceIt);
    }
    
    // Deallocate mesh index
    auto meshIndexIt = m_idToMeshIndex.find(id);
    if (meshIndexIt != m_idToMeshIndex.end()) {
        m_ssboManager->deallocateIndex(meshIndexIt->second);
        m_idToMeshIndex.erase(meshIndexIt);
    }
    
    // Remove from name mappings
    auto idToNameIt = m_idToName.find(id);
    if (idToNameIt != m_idToName.end()) {
        std::string name = idToNameIt->second;
        m_nameToId.erase(name);
        m_idToName.erase(id);
    }
    
    // Remove properties
    m_meshProperties.erase(id);
}

void DebugVisualization::setPosition(const std::string& name, const glm::dvec3& position) {
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end()) {
        setPosition(it->second, position);
    }
}

void DebugVisualization::setOrientation(const std::string& name, const glm::dquat& orientation) {
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end()) {
        setOrientation(it->second, orientation);
    }
}

void DebugVisualization::setScale(const std::string& name, const glm::dvec3& scale) {
    auto it = m_nameToId.find(name);
    if (it != m_nameToId.end()) {
        setScale(it->second, scale);
    }
}

void DebugVisualization::setPosition(int id, const glm::dvec3& position) {
    auto it = m_meshProperties.find(id);
    if (it != m_meshProperties.end()) {
        it->second.position = position;
        updateMeshTransform(id);
    }
}

void DebugVisualization::setOrientation(int id, const glm::dquat& orientation) {
    auto it = m_meshProperties.find(id);
    if (it != m_meshProperties.end()) {
        it->second.orientation = orientation;
        updateMeshTransform(id);
    }
}

void DebugVisualization::setScale(int id, const glm::dvec3& scale) {
    auto it = m_meshProperties.find(id);
    if (it != m_meshProperties.end()) {
        it->second.scale = scale;
        updateMeshTransform(id);
    }
}

int DebugVisualization::getIdFromName(const std::string& name) const {
    auto it = m_nameToId.find(name);
    return (it != m_nameToId.end()) ? it->second : -1;
}

std::string DebugVisualization::getNameFromId(int id) const {
    auto it = m_idToName.find(id);
    return (it != m_idToName.end()) ? it->second : "";
}

std::vector<int> DebugVisualization::getIdsByPrefix(const std::string& prefix) const {
    std::vector<int> matchingIds;
    
    for (const auto& pair : m_nameToId) {
        const std::string& name = pair.first;
        int id = pair.second;
        
        if (name.substr(0, prefix.length()) == prefix) {
            matchingIds.push_back(id);
        }
    }
    
    return matchingIds;
}

void DebugVisualization::removeMeshesByPrefix(const std::string& prefix) {
    std::vector<int> idsToRemove = getIdsByPrefix(prefix);
    
    for (int id : idsToRemove) {
        removeMesh(id);
    }
}

void DebugVisualization::updateMeshTransform(int id) {
    auto propsIt = m_meshProperties.find(id);
    auto meshIndexIt = m_idToMeshIndex.find(id);
    
    if (propsIt != m_meshProperties.end() && meshIndexIt != m_idToMeshIndex.end()) {
        const auto& props = propsIt->second;
        int meshIndex = meshIndexIt->second;
        
        m_ssboManager->updateMeshTransform(
            meshIndex,
            props.position,
            glm::dvec3(0.0),                      // velocity
            props.orientation,
            glm::dvec3(0.0, 1.0, 0.0),           // angVelAxis
            0.0,                                  // angVel
            glm::dvec3(0.0),                     // centerOfRotation
            props.scale,
            -1,                                   // colorTextureUnit (handled by instance)
            -1,                                   // normalTextureUnit
            -1,                                   // materialTextureUnit
            0,                                    // time
            1.0                                   // emissiveScalar
        );
    }
}

void DebugVisualization::update() {
    // Currently no per-frame updates needed for debug shapes
    // This function is here for future extensions
}

std::string DebugVisualization::generateGeogebraCommands(const std::vector<glm::dvec2>& points, int precision) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision);
    
    for (size_t i = 0; i < points.size(); ++i) {
        // Generate point name (A, B, C, ..., Z, A1, B1, C1, ...)
        std::string pointName;
        if (i < 26) {
            pointName = static_cast<char>('A' + i);
        } else {
            size_t seriesIndex = (i - 26) / 26 + 1;
            size_t letterIndex = (i - 26) % 26;
            pointName = static_cast<char>('A' + letterIndex) + std::to_string(seriesIndex);
        }
        
        oss << pointName << "=(" << points[i].x << "," << points[i].y << ")";
        if (i < points.size() - 1) {
            oss << "\n";
        }
    }
    return oss.str();
}

std::string DebugVisualization::generateGeogebraCommands(const std::vector<glm::dvec3>& points, int precision) const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision);
    
    for (size_t i = 0; i < points.size(); ++i) {
        // Generate point name (A, B, C, ..., Z, A1, B1, C1, ...)
        std::string pointName;
        if (i < 26) {
            pointName = static_cast<char>('A' + i);
        } else {
            size_t seriesIndex = (i - 26) / 26 + 1;
            size_t letterIndex = (i - 26) % 26;
            pointName = static_cast<char>('A' + letterIndex) + std::to_string(seriesIndex);
        }
        
        oss << pointName << "=(" << points[i].x << "," << points[i].y << "," << points[i].z << ")";
        if (i < points.size() - 1) {
            oss << "\n";
        }
    }
    return oss.str();
}