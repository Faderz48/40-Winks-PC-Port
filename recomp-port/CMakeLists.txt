cmake_minimum_required(VERSION 3.20)

project(forty_winks_recomp LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
# ModernRuntime's bundled miniz uses POSIX stdio declarations exposed by GNU C mode.
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(
    N64_MODERN_RUNTIME_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../work/external/N64ModernRuntime"
    CACHE PATH
    "Path to N64ModernRuntime"
)
set(
    RT64_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../work/external/rt64"
    CACHE PATH
    "Path to RT64"
)
set(
    RECOMPILED_FUNCS_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/../recomp/generated/RecompiledFuncs"
    CACHE PATH
    "Path to N64Recomp generated sources"
)

if(NOT EXISTS "${N64_MODERN_RUNTIME_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "N64ModernRuntime was not found at ${N64_MODERN_RUNTIME_DIR}")
endif()

if(NOT EXISTS "${RT64_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "RT64 was not found at ${RT64_DIR}")
endif()

if(NOT EXISTS "${RECOMPILED_FUNCS_DIR}/funcs.h")
    message(FATAL_ERROR
        "Generated functions were not found at ${RECOMPILED_FUNCS_DIR}. "
        "Run 'make prepare ROM=/path/to/40-winks.z64' first."
    )
endif()

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

set(RT64_STATIC ON CACHE BOOL "" FORCE)
set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "" FORCE)
set(NFD_PORTAL ON CACHE BOOL "" FORCE)
add_subdirectory(
    "${RT64_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/rt64"
)

add_subdirectory(
    "${N64_MODERN_RUNTIME_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/N64ModernRuntime"
)

file(GLOB RECOMPILED_C_SOURCES CONFIGURE_DEPENDS "${RECOMPILED_FUNCS_DIR}/funcs_*.c")

add_library(forty_winks_recompiled STATIC
    ${RECOMPILED_C_SOURCES}
    "${RECOMPILED_FUNCS_DIR}/lookup.cpp"
)

target_include_directories(forty_winks_recompiled PUBLIC
    "${RECOMPILED_FUNCS_DIR}"
    "${N64_MODERN_RUNTIME_DIR}/N64Recomp/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(forty_winks_recompiled PRIVATE -fno-strict-aliasing)
endif()

add_executable(forty-winks-recomp
    src/debug_menu.cpp
    src/debug_support.cpp
    src/input_routing.cpp
    src/main.cpp
    src/overlays.cpp
    src/platform.cpp
    src/recomp_support.cpp
    src/rt64_renderer.cpp
    src/split_screen_patch.cpp
)

target_include_directories(forty-winks-recomp PRIVATE
    src
    "${RECOMPILED_FUNCS_DIR}"
    "${RT64_DIR}/src"
    "${RT64_DIR}/src/contrib"
    "${RT64_DIR}/src/contrib/hlslpp/include"
    "${RT64_DIR}/src/contrib/imgui"
    "${RT64_DIR}/src/contrib/plume"
)

target_compile_definitions(forty-winks-recomp PRIVATE
    HLSL_CPU
    PLUME_SDL_VULKAN_ENABLED
    RT64_SDL_WINDOW_VULKAN
)

target_link_libraries(forty-winks-recomp PRIVATE
    forty_winks_recompiled
    librecomp
    ultramodern
    rt64
    SDL2::SDL2
    Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(forty-winks-recomp PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(controller-pak-test EXCLUDE_FROM_ALL
        tests/controller_pak_test.cpp
        src/recomp_support.cpp
    )
    target_include_directories(controller-pak-test PRIVATE
        src
        "${RECOMPILED_FUNCS_DIR}"
        "${N64_MODERN_RUNTIME_DIR}/N64Recomp/include"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(controller-pak-test PRIVATE
            -Wall -Wextra -Wpedantic -fno-strict-aliasing
        )
    endif()
    add_test(NAME controller-pak COMMAND controller-pak-test)

    add_executable(debug-level-test EXCLUDE_FROM_ALL
        tests/debug_level_test.cpp
        src/debug_support.cpp
    )
    target_include_directories(debug-level-test PRIVATE
        src
        "${RECOMPILED_FUNCS_DIR}"
        "${N64_MODERN_RUNTIME_DIR}/N64Recomp/include"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(debug-level-test PRIVATE
            -Wall -Wextra -Wpedantic -fno-strict-aliasing
        )
    endif()
    add_test(NAME debug-level COMMAND debug-level-test)

    add_executable(input-routing-test EXCLUDE_FROM_ALL
        tests/input_routing_test.cpp
        src/input_routing.cpp
    )
    target_include_directories(input-routing-test PRIVATE src)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(input-routing-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME input-routing COMMAND input-routing-test)

    add_executable(split-screen-patch-test EXCLUDE_FROM_ALL
        tests/split_screen_patch_test.cpp
        src/split_screen_patch.cpp
    )
    target_include_directories(split-screen-patch-test PRIVATE
        src
        "${N64_MODERN_RUNTIME_DIR}/N64Recomp/include"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(split-screen-patch-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME split-screen-patch COMMAND split-screen-patch-test)
endif()

install(TARGETS forty-winks-recomp RUNTIME DESTINATION bin)
