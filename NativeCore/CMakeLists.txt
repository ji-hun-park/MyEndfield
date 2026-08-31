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
    Src/SceneLoader.h
    Src/SceneLoader.cpp
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
    target_compile_options(NativeCore PRIVATE "/utf-8")
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

# ==============================================================================
# Unit Tests (Google Test)
# ==============================================================================
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(NativeCoreTests
    Tests/ECSTests.cpp
    Tests/CullingTests.cpp
    Src/ECS.cpp
    Src/Culling.cpp
    Src/VulkanBackend.cpp
    Src/SceneLoader.cpp
    Src/RenderGraph.cpp
    Src/PluginAPI.cpp
)

target_include_directories(NativeCoreTests PRIVATE 
    ${CMAKE_SOURCE_DIR}/Src
)

if (WIN32)
    target_compile_definitions(NativeCoreTests PRIVATE UNITY_WIN=1)
    target_compile_options(NativeCoreTests PRIVATE "/utf-8")
endif()

# We link the test executable against Vulkan and gtest
target_link_libraries(NativeCoreTests
    PRIVATE
    Vulkan::Vulkan
    gtest_main
)

add_test(NAME NativeCoreTests COMMAND NativeCoreTests)
