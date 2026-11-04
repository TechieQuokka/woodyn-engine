# GPU renderer (Phase 4): SDL3 GPU API on Vulkan (D-37), GLSL -> SPIR-V via glslc.

find_program(GLSLC glslc
    HINTS $ENV{VULKAN_SDK}/bin /opt/vulkan-sdk/1.4.363.1/x86_64/bin
    REQUIRED)

set(WOODYN_SHADER_SOURCES
    wood.vert wood.frag
    room.vert room.frag
    shadow.vert shadow.frag
    prepass.frag
    ghost.frag
    fullscreen.vert
    ssao.frag ssao_blur.frag
    dof.frag
    bloom_down.frag bloom_up.frag
    tonemap.frag
    grain_probe.comp
)
set(WOODYN_SHADER_OUT ${CMAKE_BINARY_DIR}/shaders)
file(GLOB WOODYN_SHADER_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common/*.glsl)

set(spirv_outputs)
foreach(src ${WOODYN_SHADER_SOURCES})
    set(in ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${src})
    set(out ${WOODYN_SHADER_OUT}/${src}.spv)
    add_custom_command(
        OUTPUT ${out}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${WOODYN_SHADER_OUT}
        COMMAND ${GLSLC} --target-env=vulkan1.2 -O -I ${CMAKE_CURRENT_SOURCE_DIR}/shaders/common
                -o ${out} ${in}
        DEPENDS ${in} ${WOODYN_SHADER_INCLUDES}
        COMMENT "glslc ${src}"
        VERBATIM)
    list(APPEND spirv_outputs ${out})
endforeach()
add_custom_target(woodyn_shaders DEPENDS ${spirv_outputs})

# stb_image implementation, compiled without WOODYN's warning flags.
add_library(woodyn_stb_image STATIC src/StbImage.cpp)
target_link_libraries(woodyn_stb_image PUBLIC stb_headers)

add_library(woodyn_renderer STATIC
    src/GpuUtil.cpp
    src/Environment.cpp
    src/GpuRenderer.cpp
    src/GrainProbe.cpp
    src/ImageLoader.cpp
)
add_library(woodyn::renderer ALIAS woodyn_renderer)
add_dependencies(woodyn_renderer woodyn_shaders)

target_include_directories(woodyn_renderer
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(woodyn_renderer PUBLIC woodyn::engine SDL3::SDL3 PRIVATE woodyn_stb_image)
target_compile_definitions(woodyn_renderer PUBLIC
    WOODYN_SHADER_DIR="${WOODYN_SHADER_OUT}"
    WOODYN_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets")
woodyn_set_warnings(woodyn_renderer)
