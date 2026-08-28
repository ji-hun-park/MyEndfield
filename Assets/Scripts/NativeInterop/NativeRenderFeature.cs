using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;

namespace Endfield.NativeInterop
{
    // Hooks into Unity's SRP to delegate rendering to our C++ Native Pipeline
    public class NativeRenderFeature : ScriptableRendererFeature
    {
        class NativeRenderPass : ScriptableRenderPass
        {
            public override void Execute(ScriptableRenderContext context, ref RenderingData renderingData)
            {
                CommandBuffer cmd = CommandBufferPool.Get("NativeRenderPass");

                // Call into our C++ render loop, bypassing Unity's managed render overhead
                // Passes event ID 1 (e.g., MainRenderLoop)
                cmd.IssuePluginEvent(NativePluginWrapper.GetRenderEventFunc(), 1);

                context.ExecuteCommandBuffer(cmd);
                CommandBufferPool.Release(cmd);
            }
        }

        NativeRenderPass m_ScriptablePass;

        public override void Create()
        {
            m_ScriptablePass = new NativeRenderPass
            {
                // Execute before standard Unity rendering to hijack the process
                renderPassEvent = RenderPassEvent.BeforeRenderingOpaques
            };
        }

        public override void AddRenderPasses(ScriptableRenderer renderer, ref RenderingData renderingData)
        {
            renderer.EnqueuePass(m_ScriptablePass);
        }
    }
}

