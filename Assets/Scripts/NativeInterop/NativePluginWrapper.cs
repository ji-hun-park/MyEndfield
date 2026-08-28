using System;
using System.Runtime.InteropServices;
using UnityEngine;

namespace Endfield.NativeInterop
{
    // 유니티 렌더링 파이프라인(IUnityGraphics 등)을 거치지 않는 독립적인 Vulkan 호출
    public static class NativePluginWrapper
    {
        private const string pluginName = "NativeCore";

        [DllImport(pluginName)]
        public static extern void InitializeVulkanRenderer(IntPtr windowHandle, uint width, uint height);

        [DllImport(pluginName)]
        public static extern void ShutdownVulkanRenderer();

        [DllImport(pluginName)]
        public static extern void ExecuteNativeRenderLoop();

        [DllImport(pluginName)]
        public static extern void RegisterEntity(uint id, IntPtr transformData);
    }
}
