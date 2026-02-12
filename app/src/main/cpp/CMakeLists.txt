cmake_minimum_required(VERSION 3.22.1)

project("krakatoa")
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

###### FETCHCONTENT #######
include(FetchContent)

# GLM - Math library
FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG 0.9.9.8
        GIT_SHALLOW TRUE
)

# nlohmann/json - JSON library
FetchContent_Declare(
        json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
)

# Assimp - Asset loading
FetchContent_Declare(
        assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG v5.3.1
        GIT_SHALLOW TRUE
)

# Configure Assimp - minimal build
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLB_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_STL_EXPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_ANDROID_JNIIOSYSTEM OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)

message(STATUS "Fetching dependencies...")
FetchContent_MakeAvailable(glm assimp json)

# 16KB page size alignment for Android 15+
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")

# Find Android libraries
find_library(log-lib log REQUIRED)
find_library(android-lib android REQUIRED)
find_library(vulkan-lib vulkan REQUIRED)
find_library(jnigraphics-lib jnigraphics REQUIRED)
# Camera2 NDK - Disponível a partir de API 24
find_library(camera2ndk-lib camera2ndk REQUIRED)
find_library(mediandk-lib mediandk REQUIRED)  # Para AImage/AImageReader

set(ARCORE_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third-party.arcore.include)
if(NOT EXISTS ${ARCORE_INCLUDE_DIR})
    message(WARNING "ARCore headers not found at ${ARCORE_INCLUDE_DIR}")
endif()

# Main library
add_library(${CMAKE_PROJECT_NAME} SHARED
        ar_loader.h
        android_log.h
        native-lib.cpp
        vk_mem_alloc.h
        vk_context.cpp
        vk_context.h
        queue_family_indices.h
        vma_impl.cpp
        pipeline.h
        pipeline.cpp
        render_pass.h
        render_pass.cpp
        offscreen_render_pass.cpp
        offscreen_render_pass.h
        swap_chain_render_pass.cpp
        swap_chain_render_pass.h
        command_pool_manager.cpp
        command_pool_manager.h
        asset_loader.h
        asset_loader.cpp
        concatenate.h
        pipeline_layout.h
        frame_sync.cpp
        frame_sync.h
)

# Include directories - adiciona tanto a raiz quanto a pasta arcore
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${ARCORE_INCLUDE_DIR}
)

# Link libraries
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
        ${vulkan-lib}
        ${android-lib}
        ${log-lib}
        ${jnigraphics-lib}
        ${camera2ndk-lib}
        ${mediandk-lib}
        nlohmann_json::nlohmann_json
        assimp::assimp
        glm::glm
)

# Compile definitions
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_SIZE_T_LENGTH
        VK_USE_PLATFORM_ANDROID_KHR
)

# Compiler options
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Werror
        -Wno-unused-parameter
        -fexceptions
        -frtti
        $<$<CONFIG:Debug>:-O0 -g>
        $<$<CONFIG:Release>:-O3 -DNDEBUG>
)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        MAX_FRAMES_IN_FLIGHT=3
)
# Strip symbols in Release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    )
endif()
