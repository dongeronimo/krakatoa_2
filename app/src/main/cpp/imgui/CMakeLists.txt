cmake_minimum_required(VERSION 3.22.1)

# imgui static library
# Sources are fetched by the parent CMakeLists.txt via FetchContent.
# The variable imgui_SOURCE_DIR must be set before add_subdirectory(imgui).

add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)

target_include_directories(imgui PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
)

find_library(imgui_vulkan_lib vulkan REQUIRED)
target_link_libraries(imgui PRIVATE ${imgui_vulkan_lib})

target_compile_definitions(imgui PUBLIC
        VK_USE_PLATFORM_ANDROID_KHR
)

# Compile with optimizations even in debug builds — ImGui is third-party
# code and we want it fast without slowing incremental builds of our own code.
target_compile_options(imgui PRIVATE
        -O2
        -DNDEBUG
        -Wno-error
        -Wno-unused-parameter
)
