// DebugRenderer.h
#pragma once

#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Pure interface for debug visualization rendering
class DebugRenderer {
public:
    virtual ~DebugRenderer() = default;
    
    // Mesh creation/removal
    virtual int createSphere(const std::string& name, const glm::dvec3& position, double radius = 1.0) = 0;
    virtual int createSphere(const glm::dvec3& position, double radius = 1.0) = 0;  // Added int version
    virtual void removeMesh(const std::string& name) = 0;
    virtual void removeMesh(int id) = 0;
    
    // Property setters by name
    virtual void setPosition(const std::string& name, const glm::dvec3& position) = 0;
    virtual void setOrientation(const std::string& name, const glm::dquat& orientation) = 0;
    virtual void setScale(const std::string& name, const glm::dvec3& scale) = 0;
    
    // Property setters by ID
    virtual void setPosition(int id, const glm::dvec3& position) = 0;
    virtual void setOrientation(int id, const glm::dquat& orientation) = 0;
    virtual void setScale(int id, const glm::dvec3& scale) = 0;
    
    // Utility functions
    virtual int getIdFromName(const std::string& name) const = 0;
    virtual std::string getNameFromId(int id) const = 0;

    // Prefix-based operations
    virtual std::vector<int> getIdsByPrefix(const std::string& prefix) const = 0;
    virtual void removeMeshesByPrefix(const std::string& prefix) = 0;

    // Generate GeoGebra commands for visualization
    virtual std::string generateGeogebraCommands(const std::vector<glm::dvec2>& points, int precision = 4) const = 0;
    virtual std::string generateGeogebraCommands(const std::vector<glm::dvec3>& points, int precision = 4) const = 0;

};