cmake_minimum_required(VERSION 3.21)

option(FAP_BUILD_PLUGIN "Build the F4SE plugin DLL (requires CommonLibF4RD, F4SEMenuFrameworkExample, and vcpkg)" ON)

if (FAP_BUILD_PLUGIN)
    if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        if (DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        elseif (EXISTS "C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "C:/Program Files/Microsoft Visual Studio/18/Community/VC/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
        endif()
    endif()

    set(VCPKG_TARGET_TRIPLET "x64-windows-static-md" CACHE STRING "")
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "")

project(FloatingActionPrompts VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/MP /permissive- /utf-8 /Zc:preprocessor)
endif()

# ---- Dependencies ----
if (FAP_BUILD_PLUGIN)
    if (NOT TARGET CommonLibF4)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/CommonLibF4RD/CommonLibF4" CommonLibF4)
    endif()

    find_package(fmt CONFIG REQUIRED)
    find_package(spdlog CONFIG REQUIRED)

    # ---- Plugin Library ----
    file(GLOB_RECURSE PLUGIN_SOURCES
        "src/*.cpp"
        "src/*.h"
    )

    add_library(${PROJECT_NAME} SHARED
        ${PLUGIN_SOURCES}
    )

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/external/F4SEMenuFrameworkExample/include
    )

    target_precompile_headers(${PROJECT_NAME} PRIVATE src/PCH.h)

    target_link_libraries(${PROJECT_NAME} PRIVATE
        CommonLibF4::CommonLibF4
        fmt::fmt
        spdlog::spdlog
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _UNICODE
        UNICODE
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
    )
endif()

# ---- Tests ----
enable_testing()

add_executable(test_scaffold tests/test_scaffold.cpp)
target_include_directories(test_scaffold PRIVATE src)
add_test(NAME test_scaffold COMMAND test_scaffold)

add_executable(test_config tests/test_config.cpp src/Config.cpp)
target_include_directories(test_config PRIVATE src)
add_test(NAME test_config COMMAND test_config)

add_executable(test_anchor tests/test_anchor.cpp src/Config.cpp src/AnchorManager.cpp)
target_include_directories(test_anchor PRIVATE src)
target_compile_definitions(test_anchor PRIVATE FAP_UNIT_TEST)
add_test(NAME test_anchor COMMAND test_anchor)

add_executable(test_projection tests/test_projection.cpp src/Projection.cpp)
target_include_directories(test_projection PRIVATE src)
add_test(NAME test_projection COMMAND test_projection)

add_executable(test_hud_controller tests/test_hud_controller.cpp src/HUDController.cpp src/OverlayLayout.cpp src/Projection.cpp src/AnchorManager.cpp src/TargetInfo.cpp src/Config.cpp)
target_include_directories(test_hud_controller PRIVATE src)
target_compile_definitions(test_hud_controller PRIVATE FAP_UNIT_TEST)
add_test(NAME test_hud_controller COMMAND test_hud_controller)

add_executable(test_target_info tests/test_target_info.cpp src/TargetInfo.cpp src/Config.cpp)
target_include_directories(test_target_info PRIVATE src)
target_compile_definitions(test_target_info PRIVATE FAP_UNIT_TEST)
add_test(NAME test_target_info COMMAND test_target_info)

add_executable(test_overlay_layout tests/test_overlay_layout.cpp src/OverlayLayout.cpp src/TargetInfo.cpp src/Config.cpp)
target_include_directories(test_overlay_layout PRIVATE src)
target_compile_definitions(test_overlay_layout PRIVATE FAP_UNIT_TEST)
add_test(NAME test_overlay_layout COMMAND test_overlay_layout)

add_executable(test_fallui_icons tests/test_fallui_icons.cpp src/FallUIIcons.cpp src/TargetInfo.cpp src/Config.cpp)
target_include_directories(test_fallui_icons PRIVATE src)
target_compile_definitions(test_fallui_icons PRIVATE FAP_UNIT_TEST)
add_test(NAME test_fallui_icons COMMAND test_fallui_icons)
