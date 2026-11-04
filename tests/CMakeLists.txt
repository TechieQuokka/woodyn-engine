include(GoogleTest)

add_executable(woodyn_tests
    core/FixedTimestepTests.cpp
    geometry/BlockCatalogTests.cpp
    physics/BlockMassTests.cpp
    physics/FreeFallTests.cpp
    physics/FrictionTests.cpp
    physics/RestitutionTests.cpp
    physics/StackingTests.cpp
    physics/DeterminismTests.cpp
    physics/InteractionTests.cpp
    physics/RollingResistanceTests.cpp
    physics/PlayAreaTests.cpp
    physics/SoftLandingTests.cpp
    physics/ImpactEventTests.cpp
    audio/ImpactSynthTests.cpp
    simulation/SnapshotTests.cpp
    wood/GrainContinuityTests.cpp
)

target_include_directories(woodyn_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(woodyn_tests PRIVATE woodyn::engine GTest::gtest_main)
woodyn_set_warnings(woodyn_tests)

gtest_discover_tests(woodyn_tests DISCOVERY_TIMEOUT 60)

# GPU tests (skipped automatically when no Vulkan device is present).
if(TARGET woodyn_renderer)
    add_executable(woodyn_gpu_tests render/GpuGrainParityTests.cpp render/GpuRenderSmokeTests.cpp)
    target_link_libraries(woodyn_gpu_tests PRIVATE woodyn::renderer GTest::gtest_main)
    woodyn_set_warnings(woodyn_gpu_tests)
    gtest_discover_tests(woodyn_gpu_tests DISCOVERY_TIMEOUT 60 PROPERTIES LABELS gpu)
endif()
