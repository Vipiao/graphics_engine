#pragma once

#include <memory>
#include <vector>
#include "../utils/TimeHandler.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class PhysicsEngine;
class GraphicsEngine;
class Drone;
class Geometry;

class GameBase
{
public:
    GameBase();
    ~GameBase();

    // Spawn a drone at the given world position/orientation.
    // Lock the returned weak_ptr before run() to configure initial velocity
    // or PID targets via drone->getRigidBody().
    [[nodiscard]] std::weak_ptr<Drone> createDrone(
        glm::dvec3 initialPosition    = glm::dvec3(0.0),
        glm::dquat initialOrientation = glm::dquat(1.0, 0.0, 0.0, 0.0)
    );

    // Blocking main loop: control → physics → syncGraphics → render, per tick.
    void run();

    // Direct camera access so main (or derived classes) can override defaults.
    [[nodiscard]] glm::dvec3& getCamPos();
    [[nodiscard]] glm::dquat& getCamOri();

private:
    std::shared_ptr<PhysicsEngine>  m_physicsEngine;
    std::shared_ptr<TimeHandler> m_timeHandler;
    std::shared_ptr<GraphicsEngine> m_graphicsEngine;

    std::vector<std::shared_ptr<Drone>> m_drones;

    // The drone geometry is shared across all drone instances.
    // GameBase does not own it (InstanceHandler does) — held as a weak_ptr.
    std::weak_ptr<Geometry> m_droneGeometry;
};