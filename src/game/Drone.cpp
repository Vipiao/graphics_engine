#include "Drone.h"
#include "../physics/RigidBody.h"
#include "../graphics/GraphicsEngine.h"
#include "../graphics/instanceHandler/InstanceHandler.h"

#include <glm/gtc/quaternion.hpp>

Drone::Drone(std::weak_ptr<RigidBody>   rigidBody,
             int                         ssboIndex,
             std::weak_ptr<Instance>     instance,
             std::weak_ptr<Geometry>     geometry,
             std::weak_ptr<GraphicsEngine> graphicsEngine,
             double                      simRate)
    : m_rigidBody     {std::move(rigidBody)}
    , m_instance      {std::move(instance)}
    , m_geometry      {std::move(geometry)}
    , m_graphicsEngine{std::move(graphicsEngine)}
    , m_ssboIndex     {ssboIndex}
    , m_simRate       {simRate}
{}

Drone::~Drone()
{
    if (auto geom = m_geometry.lock())
        geom->removeInstance(m_instance);

    if (auto gfx = m_graphicsEngine.lock())
        gfx->m_ssboManager->deallocateIndex(m_ssboIndex);
}

// ── control ───────────────────────────────────────────────────────────────────
// Altitude PID → motor thrusts (tick units) → forces applied to rigid body.

void Drone::control()
{
    auto rb = m_rigidBody.lock();
    if (!rb) return;

    // Convert SI magic numbers to tick-unit equivalents once, at entry.
    // This is the only place dt (= 1/simRate) appears — as a unit conversion.
    const double gravPerTick      = 9.81          / m_simRate; // m/s² → m/tick
    const double maxThrustPerTick = m_maxTotalThrust / m_simRate; // N   → tick-force

    // Rescale PID gains from SI-tuned (dt = 1/simRate) to tick-unit integration.
    //   Kp: output scales with dt              → Kp / simRate
    //   Ki: integrator accumulates 1/dt faster → Ki / simRate²
    //   Kd: derivative · dt / dt cancels       → Kd (unchanged)
    const double Kp = m_pidKp / m_simRate;
    const double Ki = m_pidKi / (m_simRate * m_simRate);
    const double Kd = m_pidKd;

    // ── PID (no dt — all quantities in tick units) ─────────────────────────
    const double error      = m_targetHeight - rb->m_position.z;
    m_pidIntegrator        += error;
    const double derivative = error - m_pidPrevError;
    m_pidPrevError          = error;

    const double accCmd = Kp * error + Ki * m_pidIntegrator + Kd * derivative;

    // Total thrust to overcome gravity and achieve the commanded acceleration.
    double desiredTotalThrust = rb->m_mass * (gravPerTick + accCmd);
    if (desiredTotalThrust < 0.0) desiredTotalThrust = 0.0;
    if (desiredTotalThrust > maxThrustPerTick)
    {
        desiredTotalThrust  = maxThrustPerTick;
        m_pidIntegrator    -= error; // anti-windup: undo the last accumulation
    }

    const double perMotor = desiredTotalThrust / 4.0;
    motorThrusts = {{ perMotor, perMotor, perMotor, perMotor }};

    applyForcesAndTorques();
}

// ── applyForcesAndTorques ─────────────────────────────────────────────────────
// Motor model → net force (world) + net torque (body) → rigid body accumulators.

void Drone::applyForcesAndTorques()
{
    auto rb = m_rigidBody.lock();
    if (!rb) return;

    glm::dvec3 totalThrustBody(0.0);
    glm::dvec3 totalTorqueBody(0.0);

    for (size_t i = 0; i < 4; ++i)
    {
        const glm::dvec3 Fi = motorDirections[i] * motorThrusts[i]; // body-frame force
        totalThrustBody += Fi;
        totalTorqueBody += glm::cross(motorPositions[i], Fi);       // moment-arm torque
        totalTorqueBody += glm::dvec3(0.0, 0.0,
            rotorTorqueCoeffs[i] * motorThrusts[i]);                // rotor reaction torque
    }

    // Angular damping in body frame.
    // linearDragCoef / angularDampingCoef are SI (N/(m/s), N·m/(rad/s)).
    // When velocities are in tick-units, the factor of simRate in the velocity
    // and the 1/simRate in the force conversion cancel exactly — so the
    // coefficients are applied as-is without any additional conversion.
    totalTorqueBody += -angularDampingCoef * rb->getAngularVelocityBody();

    // Linear drag in world frame — same unit cancellation applies.
    const glm::dvec3 dragWorld = -linearDragCoef * rb->m_translationalVelocity;

    // Rotate body thrust into world frame and combine with drag.
    const glm::dmat3 rot         = glm::mat3_cast(rb->m_orientation);
    const glm::dvec3 thrustWorld = rot * totalThrustBody;

    rb->addForce(thrustWorld + dragWorld);
    rb->addTorqueBody(totalTorqueBody);
}

// ── syncGraphics ──────────────────────────────────────────────────────────────
// Push current physics state to the GPU SSBO and per-instance CPU/GPU buffer.

void Drone::syncGraphics(uint64_t tick)
{
    auto rb   = m_rigidBody.lock();
    auto gfx  = m_graphicsEngine.lock();
    auto geom = m_geometry.lock();
    auto inst = m_instance.lock();
    if (!rb || !gfx || !geom || !inst) return;

    const glm::dvec3 angVelWorld = rb->getAngularVelocityWorld();
    const double     angSpeed    = glm::length(angVelWorld);
    const glm::dvec3 angAxis     = (angSpeed > 1e-12)
                                 ? (angVelWorld / angSpeed)
                                 : glm::dvec3(0.0, 0.0, 1.0);

    gfx->m_ssboManager->updateMeshTransform(
        m_ssboIndex,
        rb->m_position,
        rb->m_translationalVelocity,
        rb->m_orientation,
        angAxis,
        angSpeed,
        m_centerOfRotation,
        m_scale,
        m_tex1, m_tex2, m_tex3,
        tick,
        m_emissive
    );

    geom->updateInstanceInBuffer(inst.get());
}