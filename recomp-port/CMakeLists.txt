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

set(RECOMPILED_RSP_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../recomp/generated/rsp/aspMain.cpp"
)
if(NOT EXISTS "${RECOMPILED_RSP_SOURCE}")
    message(FATAL_ERROR
        "Generated audio microcode was not found at ${RECOMPILED_RSP_SOURCE}. "
        "Run 'make prepare ROM=/path/to/40-winks.z64' first."
    )
endif()

if(WIN32)
    set(FORTY_WINKS_SDL2_DIR
        "${RT64_DIR}/src/contrib/mupen64plus-win32-deps/SDL2-2.26.3"
    )
    foreach(SDL2_FILE
        "${FORTY_WINKS_SDL2_DIR}/include/SDL.h"
        "${FORTY_WINKS_SDL2_DIR}/lib/x64/SDL2.dll"
        "${FORTY_WINKS_SDL2_DIR}/lib/x64/SDL2.lib"
        "${FORTY_WINKS_SDL2_DIR}/lib/x64/SDL2main.lib"
    )
        if(NOT EXISTS "${SDL2_FILE}")
            message(FATAL_ERROR "RT64's bundled Windows SDL2 file is missing: ${SDL2_FILE}")
        endif()
    endforeach()

    add_library(SDL2::SDL2 SHARED IMPORTED GLOBAL)
    set_target_properties(SDL2::SDL2 PROPERTIES
        IMPORTED_LOCATION "${FORTY_WINKS_SDL2_DIR}/lib/x64/SDL2.dll"
        IMPORTED_IMPLIB "${FORTY_WINKS_SDL2_DIR}/lib/x64/SDL2.lib"
        INTERFACE_INCLUDE_DIRECTORIES "${FORTY_WINKS_SDL2_DIR}/include"
    )
    add_library(SDL2::SDL2main STATIC IMPORTED GLOBAL)
    set_target_properties(SDL2::SDL2main PROPERTIES
        IMPORTED_LOCATION "${FORTY_WINKS_SDL2_DIR}/lib/x64/SDL2main.lib"
    )
else()
    find_package(SDL2 REQUIRED)
endif()
find_package(Threads REQUIRED)

set(RT64_STATIC ON CACHE BOOL "" FORCE)
if(WIN32)
    set(RT64_SDL_WINDOW_VULKAN OFF CACHE BOOL "" FORCE)
    set(NFD_PORTAL OFF CACHE BOOL "" FORCE)
else()
    set(RT64_SDL_WINDOW_VULKAN ON CACHE BOOL "" FORCE)
    set(NFD_PORTAL ON CACHE BOOL "" FORCE)
endif()
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

if(CMAKE_C_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(forty_winks_recompiled PRIVATE
        /clang:-fno-strict-aliasing
    )
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(forty_winks_recompiled PRIVATE -fno-strict-aliasing)
endif()

add_executable(forty-winks-recomp
    "${RECOMPILED_RSP_SOURCE}"
    src/debug_menu.cpp
    src/debug_support.cpp
    src/input_routing.cpp
    src/main.cpp
    src/overlays.cpp
    src/platform.cpp
    src/recomp_support.cpp
    src/rt64_renderer.cpp
    src/split_screen_patch.cpp
    src/visual_patches.cpp
)

target_include_directories(forty-winks-recomp PRIVATE
    src
    "${RECOMPILED_FUNCS_DIR}"
    "${RT64_DIR}/src"
    "${RT64_DIR}/src/contrib"
    "${RT64_DIR}/src/contrib/dxc/inc"
    "${RT64_DIR}/src/contrib/hlslpp/include"
    "${RT64_DIR}/src/contrib/imgui"
    "${RT64_DIR}/src/contrib/plume"
)

target_compile_definitions(forty-winks-recomp PRIVATE
    HLSL_CPU
)

if(WIN32)
    target_compile_definitions(forty-winks-recomp PRIVATE
        NOMINMAX
        _DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR
    )
else()
    target_compile_definitions(forty-winks-recomp PRIVATE
        PLUME_SDL_VULKAN_ENABLED
        RT64_SDL_WINDOW_VULKAN
    )
endif()

target_link_libraries(forty-winks-recomp PRIVATE
    forty_winks_recompiled
    librecomp
    ultramodern
    rt64
    SDL2::SDL2
    Threads::Threads
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND
        CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
            CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        target_compile_options(forty-winks-recomp PRIVATE /clang:-msse4.1)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(forty-winks-recomp PRIVATE -msse4.1)
    endif()
endif()

if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    target_compile_options(forty-winks-recomp PRIVATE /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(forty-winks-recomp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    set(FORTY_WINKS_RUNTIME_DLLS
        "${FORTY_WINKS_SDL2_DIR}/lib/x64/SDL2.dll"
        "${RT64_DIR}/src/contrib/dxc/bin/x64/dxcompiler.dll"
        "${RT64_DIR}/src/contrib/dxc/bin/x64/dxil.dll"
    )
    add_custom_command(TARGET forty-winks-recomp POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            ${FORTY_WINKS_RUNTIME_DLLS}
            "$<TARGET_FILE_DIR:forty-winks-recomp>"
        VERBATIM
    )
    if(MINGW)
        target_link_options(forty-winks-recomp PRIVATE -mwindows)
    else()
        set_target_properties(forty-winks-recomp PROPERTIES
            LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
            LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup"
        )
    endif()
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

    add_executable(visual-patches-test EXCLUDE_FROM_ALL
        tests/visual_patches_test.cpp
        src/visual_patches.cpp
    )
    target_include_directories(visual-patches-test PRIVATE
        src
        "${N64_MODERN_RUNTIME_DIR}/N64Recomp/include"
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(visual-patches-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME visual-patches COMMAND visual-patches-test)
endif()

install(TARGETS forty-winks-recomp RUNTIME DESTINATION bin)
if(WIN32)
    install(FILES ${FORTY_WINKS_RUNTIME_DLLS} DESTINATION bin)
endif()
