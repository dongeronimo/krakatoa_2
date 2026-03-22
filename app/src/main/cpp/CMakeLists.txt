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

# Eigen - header-only math library (required by OpenChisel)
FetchContent_Declare(
        eigen
        GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
        GIT_TAG 3.4.0
        GIT_SHALLOW TRUE
)

# OpenChisel - chunked TSDF reconstruction
FetchContent_Declare(
        open_chisel
        GIT_REPOSITORY https://github.com/personalrobotics/OpenChisel.git
        GIT_TAG master
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
# Eigen is header-only: just download, don't run its CMake (avoids SuperLU/BLAS issues)
FetchContent_GetProperties(eigen)
if(NOT eigen_POPULATED)
    FetchContent_Populate(eigen)
endif()
# OpenChisel: just download, skip its catkin-based CMake
FetchContent_GetProperties(open_chisel)
if(NOT open_chisel_POPULATED)
    FetchContent_Populate(open_chisel)
endif()
FetchContent_MakeAvailable(glm assimp json)

# Build OpenChisel core library (skip its catkin/ROS build system entirely)
add_library(open_chisel STATIC
        ${open_chisel_SOURCE_DIR}/open_chisel/src/Chisel.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/Chunk.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/ChunkManager.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/ColorVoxel.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/DistVoxel.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/ProjectionIntegrator.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/camera/Intrinsics.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/camera/PinholeCamera.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/geometry/AABB.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/geometry/Frustum.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/geometry/Plane.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/geometry/Raycast.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/io/PLY.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/marching_cubes/MarchingCubes.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/mesh/Mesh.cpp
        ${open_chisel_SOURCE_DIR}/open_chisel/src/pointcloud/PointCloud.cpp
)
target_include_directories(open_chisel SYSTEM PUBLIC
        ${open_chisel_SOURCE_DIR}/open_chisel/include
        ${eigen_SOURCE_DIR}
)
target_compile_options(open_chisel PRIVATE -O2 -DNDEBUG -Wno-error)

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
        mesh_loader.cpp
        mesh_loader.h
        static_mesh.cpp
        static_mesh.h
        vk_debug.cpp
        vk_debug.h
        rdo.cpp
        rdo.h
        renderable.h
        renderable.cpp
        transform.h
        transform.cpp
        frame_timer.h
        frame_timer.cpp
        ar_manager.cpp
        ar_manager.h
        egl_dummy_context.cpp
        egl_dummy_context.h
        ar_camera_image.cpp
        ar_camera_image.h
        ar_plane.cpp
        ar_plane.h
        mutable_mesh.cpp
        mutable_mesh.h
        mesh.h
        texture2d.cpp
        texture2d.h
        stb/stb_image.h
        image_load.cpp
        image_load.h
        ar_depth_image.cpp
        ar_depth_image.h
        compute_pipeline.cpp
        compute_pipeline.h
        compute_operation.cpp
        compute_operation.h
        depth_deprojection_op.cpp
        depth_deprojection_op.h
        voxelization_op.cpp
        voxelization_op.h
        marching_cubes_op.cpp
        marching_cubes_op.h
        voxel_volume.cpp
        voxel_volume.h
        marching_cubes_tables.h
        gpu_mesh.cpp
        gpu_mesh.h
        tsdf_volume.cpp
        tsdf_volume.h
        tsdf_fusion_op.cpp
        tsdf_fusion_op.h
        thread_event.h
        chisel_manager.h
        chisel_manager.cpp
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
        open_chisel
        EGL
        GLESv3
)

# Compile definitions
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        GLM_FORCE_RADIANS
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_SIZE_T_LENGTH
        VK_USE_PLATFORM_ANDROID_KHR
        WORLD_MESH_MAX_VERTICES=500000
        WORLD_MESH_MAX_INDICES=1500000
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
        MAX_DESCRIPTOR_SETS_PER_POOL=1024
        GLM_FORCE_DEPTH_ZERO_TO_ONE
)
# Strip symbols in Release
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_STRIP} --strip-unneeded $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    )
endif()

