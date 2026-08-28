using System;
using System.Runtime.InteropServices;
using System.Collections.Generic;
using UnityEngine;

namespace Endfield.NativeInterop
{
    // Manages synchronization between Unity Scene and our Custom C++ ECS
    // (Bypassing Unity DOTS)
    public class CustomECSManager : MonoBehaviour
    {
        [System.Serializable]
        public struct NativeTransformData
        {
            public Matrix4x4 localToWorld;
        }

        // Scene에 있는 MeshRenderer나 특정 컴포넌트를 가진 객체들을 추적합니다.
        private List<Transform> m_TrackedTransforms = new List<Transform>();
        private NativeTransformData[] m_TransformDataArray;
        private GCHandle m_TransformDataHandle;

        private void Start()
        {
            // 예시로 현재 씬에 있는 모든 렌더러를 찾아 네이티브에 등록합니다.
            var renderers = FindObjectsOfType<MeshRenderer>();
            m_TransformDataArray = new NativeTransformData[renderers.Length];

            // Pinned GCHandle을 사용하여 C# 배열의 포인터를 C++로 안전하게 넘깁니다.
            m_TransformDataHandle = GCHandle.Alloc(m_TransformDataArray, GCHandleType.Pinned);

            for (int i = 0; i < renderers.Length; i++)
            {
                m_TrackedTransforms.Add(renderers[i].transform);
                m_TransformDataArray[i].localToWorld = renderers[i].transform.localToWorldMatrix;

                // 엔티티 ID(인덱스)와 함께 C++ 네이티브 ECS에 등록
                IntPtr ptr = m_TransformDataHandle.AddrOfPinnedObject() + (i * Marshal.SizeOf<NativeTransformData>());
                NativePluginWrapper.RegisterEntity((uint)i, ptr);
            }
        }

        private void Update()
        {
            // 매 프레임 Unity Transform 상태를 긁어모아 Pinned 메모리 영역에 업데이트
            // C++ 쪽에서는 포인터를 가지고 있기 때문에 별도의 추가 복사 없이 최신 Transform을 읽어갈 수 있습니다.
            for (int i = 0; i < m_TrackedTransforms.Count; i++)
            {
                if (m_TrackedTransforms[i].hasChanged)
                {
                    m_TransformDataArray[i].localToWorld = m_TrackedTransforms[i].localToWorldMatrix;
                    m_TrackedTransforms[i].hasChanged = false;
                }
            }
        }

        private void OnDestroy()
        {
            if (m_TransformDataHandle.IsAllocated)
            {
                m_TransformDataHandle.Free();
            }
        }
    }
}

