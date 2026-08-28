#pragma once
#include "IUnityInterface.h"

// Entry points for Unity Native Plugin
extern "C" {
    UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces);
    UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginUnload();
    
    // Custom events for our C++ Render Pipeline
    UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API ExecuteNativeRenderLoop(int eventID);
    
    // ECS Bridge
    UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API RegisterEntity(uint32_t id, float* transformData);
}

