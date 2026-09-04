# Built automatically when the engine is configured on its own, so the example
# cannot drift from the API. A consuming project never sees this target.
add_executable(engine_example main.cpp)
target_link_libraries(engine_example PRIVATE GraphicsEngine::graphics)
