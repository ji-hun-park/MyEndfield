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

        public delegate void DebugCallbackDelegate(string message);

        [DllImport(pluginName)]
        public static extern void RegisterDebugCallback(DebugCallbackDelegate callback);

        [DllImport(pluginName)]
        public static extern void ShutdownVulkanRenderer();

        [DllImport(pluginName)]
        public static extern void ExecuteNativeRenderLoop();

        [DllImport(pluginName)]
        public static extern void UpdateCameraState(ref Matrix4x4 viewMatrix, ref Matrix4x4 projMatrix);

        [DllImport(pluginName)]
        public static extern void RegisterEntity(uint id, IntPtr transformData);

        [DllImport(pluginName, CharSet = CharSet.Ansi)]
        public static extern void LoadNativeScene(string path);

        [DllImport(pluginName)]
        public static extern void SpawnNativeInstances(int count, float spread);

        [DllImport(pluginName)]
        public static extern void AnimateNativeInstances(float time, float deltaTime);

        [StructLayout(LayoutKind.Sequential, Pack = 16)]
        public struct InstanceData
        {
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
            public float[] mvpMatrix;   // 64 Bytes (Offset 0..63)
            public ulong sortKeyLow;    // 8 Bytes  (Offset 64..71: Depth 16b | Material 16b | SubMesh 16b | Mesh 16b)
            public ulong sortKeyHigh;   // 8 Bytes  (Offset 72..79: PipelineID 32b | PassID 32b)
            public uint subMeshIndex;   // 4 Bytes  (Offset 80..83)
            private uint _pad0, _pad1, _pad2; // 12 Bytes Tail Padding for 16-byte alignment (Total 96 Bytes)
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct NativeBenchmarkStats
        {
            public uint totalInstances;
            public uint visibleInstances;
            public uint culledFrustum;
            public uint culledOcclusion;
            public uint drawCallCount;
            public uint pipelineBindCount;
            public uint descriptorSetBindCount;
            public float ecsQueryTimeMs;
            public float frustumCullingTimeMs;
            public float occlusionCullingTimeMs;
            public float sortingTimeMs;
            public float batchingTimeMs;
            public float renderSubmitTimeMs;
            public float totalNativeFrameTimeMs;

            public float CullingRatio => totalInstances > 0
                ? (1.0f - ((float)visibleInstances / totalInstances)) * 100.0f
                : 0.0f;
        }

        [DllImport(pluginName)]
        public static extern void GetLatestBenchmarkStats(out NativeBenchmarkStats outStats);

        [DllImport(pluginName)]
        public static extern void SetBenchmarkCullingOptions(bool enableFrustum, bool enableOcclusion);

        [DllImport(pluginName)]
        public static extern void RunNativeHeadlessBenchmark(int instanceCount, int iterations, bool enableCulling, out NativeBenchmarkStats outAverages);
    }
}
