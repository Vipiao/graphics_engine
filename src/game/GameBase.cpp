#include "GameBase.h"
#include "../physics/PhysicsEngine.h"
#include "../physics/RigidBody.h"
#include "Drone.h"
#include "../graphics/GraphicsEngine.h"
#include "../graphics/SSBOManager.h"
#include "../graphics/instanceHandler/InstanceHandler.h"

#include <glm/gtc/quaternion.hpp>
#include <iostream>

static constexpr int    k_windowW      = 1280;
static constexpr int    k_windowH      = 720;
static constexpr int    k_maxTriangles = 10000;
static constexpr int    k_maxMeshes    = 100;

static const char* k_droneModelPath = "../media/models/01_drone/drone.obj";

// ── Construction / destruction ────────────────────────────────────────────────

GameBase::GameBase()
    : m_physicsEngine {std::make_shared<PhysicsEngine>()}
    , m_timeHandler   {std::make_shared<TimeHandler>(TimeHandler::Mode::NONE)}
    , m_graphicsEngine{std::make_shared<GraphicsEngine>(
          k_windowW, k_windowH,
          "Drone Demo",
          k_maxTriangles, k_maxMeshes,
          GraphicsEngineBase::Mode::NONE
      )}
{
    // Default camera: 10 m back (-Y), 5 m up, pitched down 30°.
    m_graphicsEngine->getCamPos() = glm::dvec3(0.0, -10.0, 5.0);
    m_graphicsEngine->getCamOri() =
        glm::angleAxis(glm::radians(-30.0), glm::dvec3(1.0, 0.0, 0.0));
}

GameBase::~GameBase()
{
    // Drones must be destroyed before the graphics engine so their destructors
    // can remove GPU instances and return SSBO slots while those systems still exist.
    m_drones.clear();
    m_droneGeometry.reset();
    // m_graphicsEngine and m_physicsEngine release last via shared_ptr.
}

// ── Drone creation ────────────────────────────────────────────────────────────

std::weak_ptr<Drone> GameBase::createDrone(
    glm::dvec3 initialPosition,
    glm::dquat initialOrientation)
{
    // 1. SSBO slot for the drone's world transform on the GPU.
    const int ssboIndex = m_graphicsEngine->m_ssboManager->allocateIndex();

    // 2. Rigid body — set initial state immediately.
    std::weak_ptr<RigidBody> rbWeak = m_physicsEngine->createRigidBody();
    if (auto rb = rbWeak.lock())
    {
        rb->m_position    = initialPosition;
        rb->m_orientation = initialOrientation;
    }

    // 3. Geometry: load once, reuse across all drone instances.
    if (m_droneGeometry.expired())
    {
        m_droneGeometry =
            m_graphicsEngine->getInstanceHandler()->createGeometry(k_droneModelPath);
    }

    auto geom = m_droneGeometry.lock();
    if (!geom)
    {
        std::cerr << "[GameBase] Failed to load drone geometry: "
                  << k_droneModelPath << '\n';
        m_physicsEngine->removeRigidBody(rbWeak);
        m_graphicsEngine->m_ssboManager->deallocateIndex(ssboIndex);
        return {};
    }

    // 4. GPU instance for this drone (solid green-ish tint).
    std::weak_ptr<Instance> instWeak = geom->addInstance(
        ssboIndex,
        -1, -1, -1,
        glm::dvec4(0.1, 1.0, 0.1, 1.0)
    );

    // 5. Construct the drone — GraphicsEngine passed as weak observer.
    auto drone = std::make_shared<Drone>(
        rbWeak,
        ssboIndex,
        instWeak,
        m_droneGeometry,
        m_graphicsEngine,
        m_physicsEngine->getSimRate()
    );

    // Push initial state to the GPU before the first tick.
    drone->syncGraphics(0);

    m_drones.push_back(drone);
    return drone;
}

// ── Main loop ─────────────────────────────────────────────────────────────────

void GameBase::run()
{
    const double k_fixedDt = 1.0 / m_physicsEngine->getSimRate();
    auto   lastTime    = m_timeHandler->now();
    double accumulator = 0.0;

    while (!m_graphicsEngine->getGraphicsEngineBase()->shouldClose())
    {
        // ── Frame timing ──────────────────────────────────────────────────
        const auto now       = m_timeHandler->now();
        double     frameTime = m_timeHandler->elapsed(lastTime).count();
        lastTime             = now;
        if (frameTime > 0.25) frameTime = 0.25; // clamp runaway steps after hitches

        // ── Fixed-step simulation ─────────────────────────────────────────
        // Order per tick:
        //   1. control()      – PID → motor thrusts → forces on rigid bodies
        //   2. physics run()  – integrate accelerations then positions
        //   3. syncGraphics() – push authoritative state to GPU
        accumulator += frameTime;
        while (accumulator >= k_fixedDt)
        {
            for (auto& drone : m_drones)
                drone->control();
            //m_drones[0]->getRigidBody().lock()->m_translationalVelocity = {0,0,-10.0};

            m_physicsEngine->run();

            const uint64_t tick = m_physicsEngine->getTickCount();
            for (auto& drone : m_drones)
                drone->syncGraphics(tick);

            accumulator -= k_fixedDt;
        }

        // ── Render ────────────────────────────────────────────────────────
        m_graphicsEngine->beginFrame();
        const uint64_t tick = m_physicsEngine->getTickCount();
        m_graphicsEngine->setRenderParameters(tick, accumulator / k_fixedDt);
        m_graphicsEngine->render();
        m_graphicsEngine->endFrame();
    }
}

// ── Camera passthrough ────────────────────────────────────────────────────────

glm::dvec3& GameBase::getCamPos()
{
    return m_graphicsEngine->getCamPos();
}

glm::dquat& GameBase::getCamOri()
{
    return m_graphicsEngine->getCamOri();
}