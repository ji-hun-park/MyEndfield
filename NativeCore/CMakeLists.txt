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
    Src/ThreadPool.h
    Src/ThreadPool.cpp
    Src/Benchmark.h
    Src/Benchmark.cpp
)

# Shared library for Unity Plugin
add_library(NativeCore SHARED ${SOURCES})

set(UNITY_PLUGIN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Assets/Plugins/x86_64")

add_custom_command(TARGET NativeCore POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${UNITY_PLUGIN_DIR}"
    # 1. 이전 백업본(.old) 삭제 시도 (잠겨 있어도 에러 무시)
    COMMAND cmd /c "if exist \"${UNITY_PLUGIN_DIR}\\NativeCore.dll.old\" del /F /Q \"${UNITY_PLUGIN_DIR}\\NativeCore.dll.old\" >nul 2>&1 || exit /b 0"
    # 2. Unity가 잠그고 있는 기존 NativeCore.dll을 .old로 이름 변경 (Windows는 로드된 DLL의 이름 변경을 허용함)
    COMMAND cmd /c "if exist \"${UNITY_PLUGIN_DIR}\\NativeCore.dll\" move /Y \"${UNITY_PLUGIN_DIR}\\NativeCore.dll\" \"${UNITY_PLUGIN_DIR}\\NativeCore.dll.old\" >nul 2>&1 || exit /b 0"
    # 3. 새로 빌드된 NativeCore.dll 복사 (잠금 시 MSB3073 에러 대신 경고 출력 후 정상 종료)
    COMMAND cmd /c "copy /Y \"$<TARGET_FILE:NativeCore>\" \"${UNITY_PLUGIN_DIR}\\NativeCore.dll\" >nul || (echo [Post-Build WARNING] Unity Editor is locking NativeCore.dll. Close Unity to apply new DLL. & exit /b 0)"
    COMMENT "[Post-Build] Deploying latest NativeCore.dll to Unity Assets/Plugins..."
)

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
    Tests/BenchmarkTests.cpp
    Src/ECS.cpp
    Src/Culling.cpp
    Src/VulkanBackend.cpp
    Src/SceneLoader.cpp
    Src/RenderGraph.cpp
    Src/PluginAPI.cpp
    Src/ThreadPool.cpp
    Src/Benchmark.cpp
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
