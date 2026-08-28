cmake_minimum_required(VERSION 3.10)
project(NativeCore)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add source files
set(SOURCES
    Src/PluginAPI.h
    Src/PluginAPI.cpp
    Src/ECS.h
    Src/ECS.cpp
    Src/Culling.h
    Src/Culling.cpp
    Src/SortKey.h
    Src/RenderGraph.h
    Src/RenderGraph.cpp
    Src/VulkanBackend.h
    Src/VulkanBackend.cpp
)

# Shared library for Unity Plugin
add_library(NativeCore SHARED ${SOURCES})

# Target OS defines
if (WIN32)
    target_compile_definitions(NativeCore PRIVATE UNITY_WIN=1)
endif()

# Find Vulkan
find_package(Vulkan REQUIRED)

# We need Vulkan SDK headers and Source headers
target_include_directories(NativeCore PRIVATE 
    ${CMAKE_SOURCE_DIR}/Src
    ${Vulkan_INCLUDE_DIRS}
)

# Link Vulkan
target_link_libraries(NativeCore PRIVATE Vulkan::Vulkan)
