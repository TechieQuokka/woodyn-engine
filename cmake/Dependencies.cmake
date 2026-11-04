# Third-party dependencies, fetched at configure time (D-02: CMake FetchContent).
# Every dependency is pinned to an exact release and verified by SHA-256.
include(FetchContent)

set(FETCHCONTENT_QUIET ON)

# --- GLM (math for public API and rendering) -------------------------------
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glm
    URL      https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz
    URL_HASH SHA256=6775e47231a446fd086d660ecc18bcd076531cfedd912fbd66e576b118607001
    SYSTEM)

# --- spdlog (logging) --------------------------------------------------------
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    URL      https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
    SYSTEM)

# --- Jolt Physics --------------------------------------------------------------
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)        # no -Werror on third-party code
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)         # keep our CMAKE_CXX_FLAGS_<CONFIG>
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)               # GPU compute back-ends are not used
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    URL          https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.6.0.tar.gz
    URL_HASH     SHA256=6e069ee0172478cc78182047aac87e5310ba14a67a53348ae14cc37801fd3f8e
    SOURCE_SUBDIR Build
    SYSTEM)

FetchContent_MakeAvailable(glm spdlog JoltPhysics)

# --- SDL3 (window, input) ------------------------------------------------------
if(WOODYN_BUILD_VIEWER)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.20/SDL3-3.4.20.tar.gz
        URL_HASH SHA256=f3bb06f55bed9557227a700af2ea62e49497ebdf58037732a731a5fb3ef8581f
        SYSTEM)
    FetchContent_MakeAvailable(SDL3)
endif()

# --- GoogleTest -----------------------------------------------------------------
if(WOODYN_BUILD_TESTS)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(googletest
        URL      https://github.com/google/googletest/archive/refs/tags/v1.18.0.tar.gz
        URL_HASH SHA256=6e3191c1455468b3fc35a417fb565c1c5071aee1b7e7f85e30cf48a98d37d8b5
        SYSTEM)
    FetchContent_MakeAvailable(googletest)
endif()
