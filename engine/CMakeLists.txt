add_library(woodyn_engine STATIC
    src/core/Log.cpp
    src/core/FixedTimestep.cpp
    src/geometry/Polyhedron.cpp
    src/geometry/RoundedMesh.cpp
    src/blocks/BlockCatalog.cpp
    src/materials/WoodSpecies.cpp
    src/materials/Stain.cpp
    src/audio/ImpactSynth.cpp
    src/audio/ModalModels.cpp
    src/physics/PhysicsWorld.cpp
    src/physics/ShapeFactory.cpp
    src/simulation/Simulation.cpp
    src/simulation/Scenes.cpp
    src/simulation/ImpactSound.cpp
)
add_library(woodyn::engine ALIAS woodyn_engine)

target_include_directories(woodyn_engine
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# GLM is part of the public API (positions, rotations). Jolt and spdlog are
# implementation details and stay private (D-03 module boundary).
target_link_libraries(woodyn_engine
    PUBLIC  glm::glm
    PRIVATE Jolt spdlog::spdlog)

target_compile_definitions(woodyn_engine PUBLIC
    GLM_FORCE_DEPTH_ZERO_TO_ONE   # Vulkan clip space
    GLM_FORCE_CTOR_INIT           # vec = 0, quat = identity on default construction
    GLM_ENABLE_EXPERIMENTAL)

woodyn_set_warnings(woodyn_engine)
