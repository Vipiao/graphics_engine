#pragma once

#include <array>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "../physics/RigidBody.h"

class GraphicsEngine;
class Geometry;
class Instance;

class Drone
{
public:
    // GraphicsEngine is held as a weak observer — the drone does not own it.
    Drone(std::weak_ptr<RigidBody>   rigidBody,
          int                         ssboIndex,
          std::weak_ptr<Instance>     instance,
          std::weak_ptr<Geometry>     geometry,
          std::weak_ptr<GraphicsEngine> graphicsEngine,
          double                      simRate);

    ~Drone();

    // Called once per physics tick: run PID, set motor thrusts, apply forces.
    void control();

    // Called once per tick after physics: push state to GPU SSBO + instance buffer.
    void syncGraphics(uint64_t tick);

    [[nodiscard]] glm::dvec3 getPosition() const
    {
        auto rb = m_rigidBody.lock();
        if (!rb) return glm::dvec3(0.0);
        return rb->m_position;
    }
    void setPosition(const glm::dvec3& position)
    {
        auto rb = m_rigidBody.lock();
        if (!rb) return;
        rb->m_position = position;
    }

    // Non-owning access to the underlying rigid body for external initialisation
    // (e.g. setting initial velocity before the first tick).
    [[nodiscard]] std::weak_ptr<RigidBody> getRigidBody() const { return m_rigidBody; }

    // ── Motor layout (body coordinates) ──────────────────────────────────────
    // Positions are relative to the body origin; directions are thrust axes.
    std::array<glm::dvec3, 4> motorPositions {{
        { 0.1,  0.1, 0.0},
        {-0.1,  0.1, 0.0},
        {-0.1, -0.1, 0.0},
        { 0.1, -0.1, 0.0}
    }};
    std::array<glm::dvec3, 4> motorDirections {{
        {0.0, 0.0, 1.0},
        {0.0, 0.0, 1.0},
        {0.0, 0.0, 1.0},
        {0.0, 0.0, 1.0}
    }};
    // Current thrust per motor (set by control() each tick, in tick-force units).
    std::array<double, 4> motorThrusts {{ 0.0, 0.0, 0.0, 0.0 }};
    // Yaw reaction torque per unit thrust (positive → +Z body torque).
    std::array<double, 4> rotorTorqueCoeffs {{ -0.001, 0.001, -0.001, 0.001 }};

    // ── Aerodynamic / damping coefficients ───────────────────────────────────
    // Stored as SI values (N/(m/s) and N·m/(rad/s)); the simRate cancels out
    // when velocities are already in tick-units, so no runtime conversion needed.
    double linearDragCoef    { 0.1  };
    double angularDampingCoef{ 0.01 };

    // ── PID configuration ─────────────────────────────────────────────────────
    // Gains are SI-tuned (original dt = 1/60 s); they are rescaled inside
    // control() each tick so you can keep editing them in familiar SI terms.
    double m_targetHeight   {   5.0 };
    double m_pidKp          {   6.0 };
    double m_pidKi          {   1.0 };
    double m_pidKd          {   2.0 };
    double m_maxTotalThrust { 100.0 };  // Newtons (real); converted inside control()

    // ── Render state ──────────────────────────────────────────────────────────
    int        m_tex1{ -1 }, m_tex2{ -1 }, m_tex3{ -1 };
    glm::dvec3 m_scale           { 1.0, 1.0, 1.0 };
    glm::dvec3 m_centerOfRotation{ 0.0, 0.0, 0.0 };
    double     m_emissive        { 0.0 };

private:
    // Separated so control() stays readable: PID → thrusts, then this → forces.
    void applyForcesAndTorques();

    std::weak_ptr<RigidBody>   m_rigidBody;
    std::weak_ptr<Instance>    m_instance;
    std::weak_ptr<Geometry>    m_geometry;
    std::weak_ptr<GraphicsEngine> m_graphicsEngine;

    int    m_ssboIndex;
    double m_simRate;

    // ── PID state ─────────────────────────────────────────────────────────────
    double m_pidIntegrator{ 0.0 };
    double m_pidPrevError { 0.0 };
};