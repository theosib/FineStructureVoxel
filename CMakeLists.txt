cmake_minimum_required(VERSION 3.20)
project(finevox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Options
option(FINEVOX_BUILD_TESTS "Build unit tests" ON)
option(FINEVOX_BUILD_RENDER "Build Vulkan render module (requires FineStructureVK)" OFF)
option(FINEVOX_BUILD_AUDIO "Build audio module (miniaudio)" OFF)

# Dependencies
include(FetchContent)

# LZ4 compression library
FetchContent_Declare(
    lz4
    GIT_REPOSITORY https://github.com/lz4/lz4.git
    GIT_TAG v1.9.4
    SOURCE_SUBDIR build/cmake
)
FetchContent_MakeAvailable(lz4)

# GLM math library (header-only)
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
)
FetchContent_MakeAvailable(glm)

# ============================================================================
# Core library (namespace: finevox)
# ============================================================================

add_library(finevox SHARED
    src/core/position.cpp
    src/core/string_interner.cpp
    src/core/palette.cpp
    src/core/subchunk.cpp
    src/core/chunk_column.cpp
    src/core/rotation.cpp
    src/core/world.cpp
    src/core/column_manager.cpp
    src/core/batch_builder.cpp
    src/core/data_container.cpp
    src/core/serialization.cpp
    src/core/region_file.cpp
    src/core/io_manager.cpp
    src/core/config.cpp
    src/core/resource_locator.cpp
    src/core/physics.cpp
    src/core/mesh.cpp
    src/core/block_type.cpp
    src/core/block_model.cpp
    src/core/block_model_loader.cpp
    src/core/block_handler.cpp
    src/core/block_event.cpp
    src/core/module.cpp
    src/core/entity_registry.cpp
    src/core/item_registry.cpp
    src/core/name_registry.cpp
    src/core/inventory.cpp
    src/core/item_drop_entity.cpp
    src/core/config_parser.cpp
    src/core/config_file.cpp
    src/core/mesh_worker_pool.cpp
    src/core/lod.cpp
    src/core/light_data.cpp
    src/core/light_engine.cpp
    src/core/event_queue.cpp
    src/core/entity.cpp
    src/core/graphics_event_queue.cpp
    src/core/entity_manager.cpp
    src/core/player_controller.cpp
    src/core/key_bindings.cpp
    src/core/tag_registry.cpp
    src/core/unification.cpp
    src/core/world_time.cpp
    src/core/sound_event.cpp
    src/core/sound_registry.cpp
)

target_include_directories(finevox PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(finevox PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

target_link_libraries(finevox PUBLIC lz4_static glm::glm)

# ============================================================================
# Script Module (namespace: finevox::script) - always built
# ============================================================================

# Find finescript
if(NOT DEFINED FINESCRIPT_DIR)
    set(FINESCRIPT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../game-language"
        CACHE PATH "Path to finescript")
endif()

if(EXISTS "${FINESCRIPT_DIR}/include/finescript/script_engine.h")
    add_library(finevox_script SHARED
        src/script/script_cache.cpp
        src/script/block_context_proxy.cpp
        src/script/data_container_proxy.cpp
        src/script/script_block_handler.cpp
        src/script/game_script_engine.cpp
    )

    target_include_directories(finevox_script PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${FINESCRIPT_DIR}/include
    )

    target_compile_options(finevox_script PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )

    target_link_libraries(finevox_script PUBLIC finevox)

    # Link finescript shared library
    if(EXISTS "${FINESCRIPT_DIR}/build/libfinescript.dylib")
        target_link_libraries(finevox_script PUBLIC
            "${FINESCRIPT_DIR}/build/libfinescript.dylib")
    elseif(EXISTS "${FINESCRIPT_DIR}/build/libfinescript.so")
        target_link_libraries(finevox_script PUBLIC
            "${FINESCRIPT_DIR}/build/libfinescript.so")
    elseif(EXISTS "${FINESCRIPT_DIR}/build/libfinescript.a")
        target_link_libraries(finevox_script PUBLIC
            "${FINESCRIPT_DIR}/build/libfinescript.a")
    endif()

    target_compile_definitions(finevox_script PUBLIC FINEVOX_HAS_SCRIPT=1)

    message(STATUS "FineStructureVoxel: Building script module with finescript at ${FINESCRIPT_DIR}")
else()
    message(WARNING "finescript not found at ${FINESCRIPT_DIR} - script module disabled")
endif()

# ============================================================================
# World Generation Module (namespace: finevox::worldgen)
# ============================================================================

add_library(finevox_worldgen SHARED
    src/worldgen/noise_perlin.cpp
    src/worldgen/noise_simplex.cpp
    src/worldgen/noise_voronoi.cpp
    src/worldgen/noise_ops.cpp
    src/worldgen/schematic.cpp
    src/worldgen/schematic_io.cpp
    src/worldgen/clipboard_manager.cpp
    src/worldgen/biome.cpp
    src/worldgen/biome_map.cpp
    src/worldgen/biome_loader.cpp
    src/worldgen/feature_tree.cpp
    src/worldgen/feature_ore.cpp
    src/worldgen/feature_schematic.cpp
    src/worldgen/feature_registry.cpp
    src/worldgen/feature_loader.cpp
    src/worldgen/world_generator.cpp
    src/worldgen/generation_passes.cpp
)

target_include_directories(finevox_worldgen PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(finevox_worldgen PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

target_link_libraries(finevox_worldgen PUBLIC finevox)

# ============================================================================
# Vulkan Render Module (namespace: finevox::render)
# ============================================================================

if(FINEVOX_BUILD_RENDER)
    # Find Vulkan
    find_package(Vulkan REQUIRED)

    # Find FineStructureVK
    # User must set FINEVK_DIR to point to FineStructureVK root
    if(NOT DEFINED FINEVK_DIR)
        set(FINEVK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../FineStructureVK" CACHE PATH "Path to FineStructureVK")
    endif()

    if(NOT EXISTS "${FINEVK_DIR}/include/finevk/finevk.hpp")
        message(FATAL_ERROR "FineStructureVK not found at ${FINEVK_DIR}. Set FINEVK_DIR to the correct path.")
    endif()

    # Find finegui
    if(NOT DEFINED FINEGUI_DIR)
        set(FINEGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../finegui" CACHE PATH "Path to finegui")
    endif()

    # Add the render module library
    add_library(finevox_render SHARED
        src/render/subchunk_view.cpp
        src/render/world_renderer.cpp
        src/render/block_atlas.cpp
        src/render/texture_manager.cpp
    )

    target_include_directories(finevox_render PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${FINEVK_DIR}/include
    )

    # Need GLFW for FineStructureVK headers
    find_package(glfw3 REQUIRED)

    target_link_libraries(finevox_render PUBLIC
        finevox
        Vulkan::Vulkan
        glfw
    )

    # Link against FineStructureVK libraries
    if(EXISTS "${FINEVK_DIR}/build/libfinevk-core.a")
        target_link_libraries(finevox_render PUBLIC
            "${FINEVK_DIR}/build/libfinevk-engine.a"
            "${FINEVK_DIR}/build/libfinevk-core.a"
        )
    elseif(EXISTS "${FINEVK_DIR}/build/finevk-core.lib")
        target_link_libraries(finevox_render PUBLIC
            "${FINEVK_DIR}/build/finevk-engine.lib"
            "${FINEVK_DIR}/build/finevk-core.lib"
        )
    else()
        message(WARNING "FineStructureVK library not found at ${FINEVK_DIR}/build - you may need to build it first")
    endif()

    target_compile_definitions(finevox_render PUBLIC FINEVOX_HAS_RENDER=1)

    message(STATUS "FineStructureVoxel: Building render module with FineStructureVK at ${FINEVK_DIR}")

    # Compile shaders
    find_program(GLSLC glslc HINTS $ENV{VULKAN_SDK}/bin)
    if(GLSLC)
        set(SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
        set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
        file(MAKE_DIRECTORY ${SHADER_OUTPUT_DIR})

        set(SHADER_SOURCES
            ${SHADER_DIR}/chunk.vert
            ${SHADER_DIR}/chunk.frag
            ${SHADER_DIR}/overlay.vert
            ${SHADER_DIR}/overlay.frag
        )

        foreach(SHADER ${SHADER_SOURCES})
            get_filename_component(SHADER_NAME ${SHADER} NAME)
            set(SHADER_OUTPUT ${SHADER_OUTPUT_DIR}/${SHADER_NAME}.spv)
            add_custom_command(
                OUTPUT ${SHADER_OUTPUT}
                COMMAND ${GLSLC} ${SHADER} -o ${SHADER_OUTPUT}
                DEPENDS ${SHADER}
                COMMENT "Compiling shader ${SHADER_NAME}"
            )
            list(APPEND SHADER_OUTPUTS ${SHADER_OUTPUT})
        endforeach()

        add_custom_target(finevox_shaders ALL DEPENDS ${SHADER_OUTPUTS})
        add_dependencies(finevox_render finevox_shaders)

        message(STATUS "FineStructureVoxel: Shader compilation enabled (output: ${SHADER_OUTPUT_DIR})")
    else()
        message(WARNING "glslc not found - shaders will not be compiled. Install Vulkan SDK.")
    endif()

    # Render demo executable
    option(FINEVOX_BUILD_EXAMPLES "Build example applications" ON)
    if(FINEVOX_BUILD_EXAMPLES)
        add_executable(finevox_render_demo
            examples/render_demo.cpp
        )

        # finevox_render already links FineStructureVK libraries transitively (PUBLIC)
        target_link_libraries(finevox_render_demo PRIVATE
            finevox_render
            finevox_worldgen
        )

        # Link finegui if available
        if(EXISTS "${FINEGUI_DIR}/include/finegui/finegui.hpp")
            target_include_directories(finevox_render_demo PRIVATE
                ${FINEGUI_DIR}/include
                ${FINEGUI_DIR}/third_party/imgui
            )
            if(EXISTS "${FINEGUI_DIR}/build/libfinegui.a")
                target_link_libraries(finevox_render_demo PRIVATE
                    "${FINEGUI_DIR}/build/libfinegui.a"
                )
            endif()
            target_compile_definitions(finevox_render_demo PRIVATE FINEVOX_HAS_GUI=1)
            message(STATUS "FineStructureVoxel: finegui found at ${FINEGUI_DIR}")
        else()
            message(STATUS "FineStructureVoxel: finegui not found, GUI disabled")
        endif()

        # Copy shaders to build directory
        add_dependencies(finevox_render_demo finevox_shaders)

        # Set working directory for IDE
        set_target_properties(finevox_render_demo PROPERTIES
            VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
            XCODE_GENERATE_SCHEME TRUE
            XCODE_SCHEME_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        )

        message(STATUS "FineStructureVoxel: Building render demo")
    endif()
endif()

# ============================================================================
# Audio Module (namespace: finevox::audio)
# ============================================================================

if(FINEVOX_BUILD_AUDIO)
    # Fetch miniaudio (single-header library)
    FetchContent_Declare(
        miniaudio
        GIT_REPOSITORY https://github.com/mackron/miniaudio.git
        GIT_TAG 0.11.21
    )
    FetchContent_MakeAvailable(miniaudio)

    add_library(finevox_audio SHARED
        src/audio/audio_engine.cpp
        src/audio/sound_loader.cpp
        src/audio/footstep_tracker.cpp
    )

    target_include_directories(finevox_audio PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        ${miniaudio_SOURCE_DIR}
    )

    target_compile_options(finevox_audio PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
    )

    target_link_libraries(finevox_audio PUBLIC finevox)

    # Platform-specific audio libraries
    if(APPLE)
        find_library(COREAUDIO_LIB CoreAudio)
        find_library(AUDIOUNIT_LIB AudioUnit)
        find_library(COREFOUND_LIB CoreFoundation)
        target_link_libraries(finevox_audio PRIVATE
            ${COREAUDIO_LIB} ${AUDIOUNIT_LIB} ${COREFOUND_LIB})
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(finevox_audio PRIVATE pthread dl m)
    endif()

    target_compile_definitions(finevox_audio PUBLIC FINEVOX_HAS_AUDIO=1)

    message(STATUS "FineStructureVoxel: Building audio module")

    # Audio test executable
    add_executable(finevox_audio_test examples/audio_test.cpp)
    target_link_libraries(finevox_audio_test PRIVATE finevox_audio)

    # Link audio to render demo if both are being built
    if(FINEVOX_BUILD_RENDER AND FINEVOX_BUILD_EXAMPLES)
        target_link_libraries(finevox_render_demo PRIVATE finevox_audio)
    endif()
endif()

# Tests
if(FINEVOX_BUILD_TESTS)
    enable_testing()

    # Fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(finevox_tests
        tests/test_position.cpp
        tests/test_string_interner.cpp
        tests/test_palette.cpp
        tests/test_subchunk.cpp
        tests/test_chunk_column.cpp
        tests/test_rotation.cpp
        tests/test_world.cpp
        tests/test_lru_cache.cpp
        tests/test_column_manager.cpp
        tests/test_batch_builder.cpp
        tests/test_data_container.cpp
        tests/test_serialization.cpp
        tests/test_region_file.cpp
        tests/test_io_manager.cpp
        tests/test_config.cpp
        tests/test_resource_locator.cpp
        tests/test_physics.cpp
        tests/test_mesh.cpp
        tests/test_block_type.cpp
        tests/test_block_model.cpp
        tests/test_config_parser.cpp
        tests/test_config_file.cpp
        tests/test_blocking_queue.cpp
        tests/test_mesh_worker_pool.cpp
        tests/test_lod.cpp
        tests/test_module.cpp
        tests/test_lighting.cpp
        tests/test_event_system.cpp
        tests/test_queue_primitives.cpp
        tests/test_noise.cpp
        tests/test_schematic.cpp
        tests/test_biome.cpp
        tests/test_feature.cpp
        tests/test_generation.cpp
        tests/test_player_controller.cpp
        tests/test_inventory.cpp
        tests/test_tags.cpp
        tests/test_sky.cpp
        tests/test_sound_system.cpp
    )

    target_link_libraries(finevox_tests PRIVATE
        finevox_worldgen
        GTest::gtest_main
    )

    include(GoogleTest)
    gtest_discover_tests(finevox_tests)

    # Script integration tests (separate binary, links finescript)
    if(TARGET finevox_script)
        add_executable(finevox_script_tests
            tests/test_script_integration.cpp
        )

        target_link_libraries(finevox_script_tests PRIVATE
            finevox_script
            finevox_worldgen
            GTest::gtest_main
        )

        gtest_discover_tests(finevox_script_tests)
    endif()
endif()
