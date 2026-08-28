cmake_minimum_required(VERSION 3.10)
project(NativeCore)

set(CMAKE_CXX_STANDARD 17)

# Add source files
set(SOURCES
    Src/PluginAPI.h
    Src/ECS.h
    Src/Culling.h
    Src/SortKey.h
    Src/RenderGraph.h
    Src/VulkanBackend.h
)

# Shared library for Unity Plugin
add_library(NativeCore SHARED ${SOURCES})

# Target OS defines
if (WIN32)
    target_compile_definitions(NativeCore PRIVATE UNITY_WIN=1)
endif()

# We need Vulkan SDK headers and Unity Native Plugin headers (Assuming they exist or will be added)
# target_include_directories(NativeCore PRIVATE ${CMAKE_SOURCE_DIR}/Src)

