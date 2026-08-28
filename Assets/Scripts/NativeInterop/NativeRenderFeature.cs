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

            public override void Execute(ScriptableRenderContext context, ref RenderingData renderingData)
            {
                if (!m_Initialized)
                {
                    IntPtr hwnd = IntPtr.Zero;
#if UNITY_EDITOR_WIN || UNITY_STANDALONE_WIN
                    hwnd = Process.GetCurrentProcess().MainWindowHandle;
#endif
                    // 해상도는 임시로 Screen 값을 전달
                    NativePluginWrapper.InitializeVulkanRenderer(hwnd, (uint)Screen.width, (uint)Screen.height);
                    m_Initialized = true;
                }

                // 현재 카메라의 뷰/프로젝션 행렬을 네이티브로 넘김
                Camera cam = renderingData.cameraData.camera;
                Matrix4x4 viewMatrix = cam.worldToCameraMatrix;
                Matrix4x4 projMatrix = GL.GetGPUProjectionMatrix(cam.projectionMatrix, false);

                NativePluginWrapper.UpdateCameraState(ref viewMatrix, ref projMatrix);

                // 유니티의 IssuePluginEvent를 쓰지 않고, 
                // C++ 네이티브의 독자적인 렌더 루프 함수를 직접 호출하여 유니티 스레드를 블로킹하고 네이티브 렌더링 수행
                NativePluginWrapper.ExecuteNativeRenderLoop();
            }

            public void Cleanup()
            {
                if (m_Initialized)
                {
                    NativePluginWrapper.ShutdownVulkanRenderer();
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
