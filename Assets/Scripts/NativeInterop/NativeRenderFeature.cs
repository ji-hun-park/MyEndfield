using System;
using System.Diagnostics;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace Endfield.NativeInterop
{
    // 유니티 렌더링을 가로채서 우리의 독자적 Vulkan 루프를 실행하도록 호출
    public class NativeRenderFeature : ScriptableRendererFeature
    {
        class NativeRenderPass : ScriptableRenderPass
        {
            private bool m_Initialized = false;

            class PassData { }

            public override void RecordRenderGraph(UnityEngine.Rendering.RenderGraphModule.RenderGraph renderGraph, ContextContainer frameData)
            {
                using (var builder = renderGraph.AddUnsafePass<PassData>("Native Render Pass", out var passData))
                {
                    builder.AllowPassCulling(false);

                    builder.SetRenderFunc((PassData data, UnityEngine.Rendering.RenderGraphModule.UnsafeGraphContext context) =>
                    {
                        if (!m_Initialized)
                        {
                            try
                            {
                                IntPtr hwnd = IntPtr.Zero;
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
                                hwnd = Process.GetCurrentProcess().MainWindowHandle;
#endif
                                NativePluginWrapper.InitializeVulkanRenderer(hwnd, (uint)Screen.width, (uint)Screen.height);

                                // 에디터에서 익스포트한 씬 바이너리를 네이티브 C++로 즉시 로드
                                string exportPath = Application.dataPath + "/../NativeCore/ExportedScene.bin";
                                if (!System.IO.File.Exists(exportPath))
                                {
                                    Debug.LogError($"[NativeRenderFeature] Exported scene file not found at: {exportPath}. Please export the scene first.");
                                }
                                else
                                {
                                    NativePluginWrapper.LoadNativeScene(exportPath);
                                }
                            }
                            catch (Exception e)
                            {
                                Debug.LogError($"[NativeRenderFeature] Failed to initialize native renderer: {e.Message}\n{e.StackTrace}");
                            }

                            m_Initialized = true;
                        }

                        // CameraData에서 View/Proj 매트릭스 획득
                        UniversalCameraData cameraData = frameData.Get<UniversalCameraData>();
                        Camera cam = cameraData.camera;

                        Matrix4x4 viewMatrix = cam.worldToCameraMatrix;
                        Matrix4x4 projMatrix = GL.GetGPUProjectionMatrix(cam.projectionMatrix, false);

                        try
                        {
                            NativePluginWrapper.UpdateCameraState(ref viewMatrix, ref projMatrix);
                            NativePluginWrapper.ExecuteNativeRenderLoop();
                        }
                        catch (Exception e)
                        {
                            Debug.LogError($"[NativeRenderFeature] Exception during native render loop: {e.Message}\n{e.StackTrace}");
                        }
                    });
                }
            }

            public void Cleanup()
            {
                if (m_Initialized)
                {
                    try
                    {
                        NativePluginWrapper.ShutdownVulkanRenderer();
                    }
                    catch (Exception e)
                    {
                        Debug.LogError($"[NativeRenderFeature] Failed to shutdown native renderer: {e.Message}\n{e.StackTrace}");
                    }
                    m_Initialized = false;
                }
            }
        }

        NativeRenderPass m_ScriptablePass;

        public override void Create()
        {
            m_ScriptablePass = new NativeRenderPass
            {
                // 기존 유니티의 렌더링이 일어나기 전에 제어권을 가져옵니다.
                renderPassEvent = RenderPassEvent.BeforeRenderingOpaques
            };
        }

        public override void AddRenderPasses(ScriptableRenderer renderer, ref RenderingData renderingData)
        {
            renderer.EnqueuePass(m_ScriptablePass);
        }

        protected override void Dispose(bool disposing)
        {
            if (m_ScriptablePass != null)
            {
                m_ScriptablePass.Cleanup();
            }
        }
    }
}
