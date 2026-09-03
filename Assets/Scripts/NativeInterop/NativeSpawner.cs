using System.Collections;
using UnityEngine;
using Endfield.NativeInterop;

namespace Endfield.NativeInterop
{
    public class NativeSpawner : MonoBehaviour
    {
        [Header("Native Spawner Settings")]
        [Tooltip("Number of times to clone the currently exported FBX scene")]
        public int spawnCount = 1000;

        [Tooltip("Radius around the origin to randomly scatter the cloned instances")]
        public float spreadRadius = 50f;

        [Tooltip("Enable continuous movement for spawned objects")]
        public bool animateObjects = true;

        [Tooltip("Speed of the animation")]
        public float animationSpeed = 1.0f;

        private bool isSpawned = false;

        private IEnumerator Start()
        {
            // 렌더 패스가 한 번 실행되어 네이티브 렌더러와 씬이 로드될 시간을 줍니다.
            yield return new WaitForEndOfFrame();

            try
            {
                // 로드된 기본 인스턴스들을 C++에서 대량 복제합니다.
                NativePluginWrapper.SpawnNativeInstances(spawnCount, spreadRadius);
                Debug.Log($"[NativeSpawner] Successfully spawned {spawnCount} clone instances in native ECS with a spread of {spreadRadius}.");
                isSpawned = true;
            }
            catch (System.Exception e)
            {
                Debug.LogError($"[NativeSpawner] Failed to spawn native instances: {e.Message}");
            }
        }

        private void Update()
        {
            if (isSpawned && animateObjects)
            {
                NativePluginWrapper.AnimateNativeInstances(Time.time * animationSpeed, Time.deltaTime * animationSpeed);
            }
        }
    }
}
