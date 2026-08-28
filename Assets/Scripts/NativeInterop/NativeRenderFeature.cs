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
            public override void Execute(ScriptableRenderContext context, ref RenderingData renderingData)
            {
                // 유니티의 IssuePluginEvent를 쓰지 않고, 
                // C++ 네이티브의 독자적인 렌더 루프 함수를 직접 호출합니다.
                NativePluginWrapper.ExecuteNativeRenderLoop();
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
    }
}
