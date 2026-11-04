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
    physics/ImpactEventTests.cpp
    audio/ImpactSynthTests.cpp
    simulation/SnapshotTests.cpp
)

target_include_directories(woodyn_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(woodyn_tests PRIVATE woodyn::engine GTest::gtest_main)
woodyn_set_warnings(woodyn_tests)

gtest_discover_tests(woodyn_tests DISCOVERY_TIMEOUT 60)
