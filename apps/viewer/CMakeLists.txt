# Debug viewer for the physics prototype (Phase 2). It draws flat-shaded block
# meshes with SDL3's 2D renderer; the real Vulkan renderer comes in Phase 4.
add_executable(woodyn_viewer
    main.cpp
    OrbitCamera.cpp
    DebugRenderer.cpp
    Playroom.cpp
)
target_link_libraries(woodyn_viewer PRIVATE woodyn::engine SDL3::SDL3)
woodyn_set_warnings(woodyn_viewer)
