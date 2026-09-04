using System;
using System.Collections;
using System.Collections.Generic;
using UnityEngine;

namespace Endfield.NativeInterop
{
    public class BenchmarkManager : MonoBehaviour
    {
        public static BenchmarkManager Instance { get; private set; }

        [Header("Culling Settings")]
        [SerializeField] private bool m_EnableFrustumCulling = true;
        [SerializeField] private bool m_EnableOcclusionCulling = false;

        [Header("Automated Benchmark Configuration")]
        [SerializeField] private int[] m_BenchmarkTiers = new int[] { 1000, 10000, 50000, 100000, 500000 };
        [SerializeField] private int m_WarmupFrames = 10;
        [SerializeField] private int m_SampleFrames = 30;

        public bool IsBenchmarkRunning { get; private set; } = false;
        public string BenchmarkStatusMessage { get; private set; } = "Idle";
        public float BenchmarkProgress { get; private set; } = 0.0f;

        // Live stats
        private NativePluginWrapper.NativeBenchmarkStats m_LatestNativeStats;
        private readonly Queue<float> m_FrameTimeBuffer = new Queue<float>();
        private const int MAX_FRAME_SAMPLES = 60;

        public float CurrentFps { get; private set; }
        public float AverageFps { get; private set; }
        public float OnePercentLowFps { get; private set; }
        public float MinFps { get; private set; }
        public float MaxFps { get; private set; }
        public NativePluginWrapper.NativeBenchmarkStats LatestNativeStats => m_LatestNativeStats;

        public bool EnableFrustumCulling
        {
            get => m_EnableFrustumCulling;
            set
            {
                m_EnableFrustumCulling = value;
                NativePluginWrapper.SetBenchmarkCullingOptions(m_EnableFrustumCulling, m_EnableOcclusionCulling);
            }
        }

        public bool EnableOcclusionCulling
        {
            get => m_EnableOcclusionCulling;
            set
            {
                m_EnableOcclusionCulling = value;
                NativePluginWrapper.SetBenchmarkCullingOptions(m_EnableFrustumCulling, m_EnableOcclusionCulling);
            }
        }

        public event Action<string> OnBenchmarkCompleted;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.AfterSceneLoad)]
        private static void AutoInitialize()
        {
            if (Instance == null)
            {
                var go = new GameObject("[Endfield Benchmark Controller]");
                go.AddComponent<BenchmarkManager>();
                go.AddComponent<BenchmarkHUD>();
            }
        }

        private void Awake()
        {
            if (Instance != null && Instance != this)
            {
                Destroy(gameObject);
                return;
            }
            Instance = this;
            DontDestroyOnLoad(gameObject);
        }

        private void Start()
        {
            NativePluginWrapper.SetBenchmarkCullingOptions(m_EnableFrustumCulling, m_EnableOcclusionCulling);
        }

        private void Update()
        {
            // 1. Update live Unity FPS & frame times
            float dt = Time.unscaledDeltaTime;
            if (dt > 0.0001f)
            {
                CurrentFps = 1.0f / dt;
                m_FrameTimeBuffer.Enqueue(CurrentFps);
                if (m_FrameTimeBuffer.Count > MAX_FRAME_SAMPLES)
                {
                    m_FrameTimeBuffer.Dequeue();
                }

                CalculateFpsMetrics();
            }

            // 2. Poll latest Native C++ stats
            try
            {
                NativePluginWrapper.GetLatestBenchmarkStats(out m_LatestNativeStats);
            }
            catch
            {
                // Native core might not be initialized yet
            }
        }

        private void CalculateFpsMetrics()
        {
            if (m_FrameTimeBuffer.Count == 0) return;

            float sum = 0.0f;
            float min = float.MaxValue;
            float max = float.MinValue;
            List<float> sortedSamples = new List<float>(m_FrameTimeBuffer.Count);

            foreach (float fps in m_FrameTimeBuffer)
            {
                sum += fps;
                if (fps < min) min = fps;
                if (fps > max) max = fps;
                sortedSamples.Add(fps);
            }

            AverageFps = sum / m_FrameTimeBuffer.Count;
            MinFps = min;
            MaxFps = max;

            sortedSamples.Sort();
            int onePercentIndex = Mathf.Max(0, Mathf.FloorToInt(sortedSamples.Count * 0.01f));
            OnePercentLowFps = sortedSamples[onePercentIndex];
        }

        public void StartAutomatedBenchmark()
        {
            if (IsBenchmarkRunning) return;
            StartCoroutine(RunAutomatedBenchmarkRoutine());
        }

        private IEnumerator RunAutomatedBenchmarkRoutine()
        {
            IsBenchmarkRunning = true;
            BenchmarkStatusMessage = "Starting Benchmark Suite...";
            List<BenchmarkTierResult> results = new List<BenchmarkTierResult>();

            for (int t = 0; t < m_BenchmarkTiers.Length; t++)
            {
                int count = m_BenchmarkTiers[t];
                string tierName = count >= 1000 ? $"{count / 1000}K" : count.ToString();
                BenchmarkStatusMessage = $"Running Tier {tierName} ({count:N0} entities)...";
                BenchmarkProgress = (float)t / m_BenchmarkTiers.Length;

                // Spawn instances in native ECS
                try
                {
                    NativePluginWrapper.SpawnNativeInstances(count, 300.0f);
                }
                catch (Exception e)
                {
                    Debug.LogWarning($"[BenchmarkManager] Spawn error: {e.Message}");
                }

                // Warmup
                for (int w = 0; w < m_WarmupFrames; w++)
                {
                    yield return null;
                }

                // Sample
                float fpsSum = 0.0f;
                NativePluginWrapper.NativeBenchmarkStats accumulatedStats = default;

                for (int s = 0; s < m_SampleFrames; s++)
                {
                    yield return null;
                    fpsSum += CurrentFps;

                    NativePluginWrapper.GetLatestBenchmarkStats(out var current);
                    accumulatedStats.totalInstances = current.totalInstances;
                    accumulatedStats.visibleInstances += current.visibleInstances;
                    accumulatedStats.culledFrustum += current.culledFrustum;
                    accumulatedStats.culledOcclusion += current.culledOcclusion;
                    accumulatedStats.ecsQueryTimeMs += current.ecsQueryTimeMs;
                    accumulatedStats.frustumCullingTimeMs += current.frustumCullingTimeMs;
                    accumulatedStats.occlusionCullingTimeMs += current.occlusionCullingTimeMs;
                    accumulatedStats.sortingTimeMs += current.sortingTimeMs;
                    accumulatedStats.batchingTimeMs += current.batchingTimeMs;
                    accumulatedStats.renderSubmitTimeMs += current.renderSubmitTimeMs;
                    accumulatedStats.totalNativeFrameTimeMs += current.totalNativeFrameTimeMs;
                }

                // Average
                accumulatedStats.visibleInstances /= (uint)m_SampleFrames;
                accumulatedStats.culledFrustum /= (uint)m_SampleFrames;
                accumulatedStats.culledOcclusion /= (uint)m_SampleFrames;
                accumulatedStats.ecsQueryTimeMs /= m_SampleFrames;
                accumulatedStats.frustumCullingTimeMs /= m_SampleFrames;
                accumulatedStats.occlusionCullingTimeMs /= m_SampleFrames;
                accumulatedStats.sortingTimeMs /= m_SampleFrames;
                accumulatedStats.batchingTimeMs /= m_SampleFrames;
                accumulatedStats.renderSubmitTimeMs /= m_SampleFrames;
                accumulatedStats.totalNativeFrameTimeMs /= m_SampleFrames;

                float avgFps = fpsSum / m_SampleFrames;

                results.Add(new BenchmarkTierResult
                {
                    tierName = tierName,
                    entityCount = count,
                    unityFps = avgFps,
                    unityFrameTimeMs = avgFps > 0 ? (1000.0f / avgFps) : 0,
                    nativeStats = accumulatedStats
                });
            }

            BenchmarkProgress = 1.0f;
            BenchmarkStatusMessage = "Generating Report...";

            string sysInfo = $"{SystemInfo.processorType} ({SystemInfo.processorCount} cores) | {SystemInfo.graphicsDeviceName} ({SystemInfo.graphicsMemorySize} MB VRAM)";
            string reportMarkdown = BenchmarkReportGenerator.GenerateMarkdownReport("Live-ops In-Engine Benchmark", results, sysInfo);
            string savedPath = BenchmarkReportGenerator.SaveReportToFile(reportMarkdown);

            BenchmarkStatusMessage = "Benchmark Completed!";
            IsBenchmarkRunning = false;

            OnBenchmarkCompleted?.Invoke(savedPath);
            Debug.Log($"[BenchmarkManager] Benchmark complete! Report saved at: {savedPath}");
        }

        public static List<BenchmarkTierResult> RunHeadlessBenchmarkSuite(int[] counts, int iterations = 3, bool enableCulling = true)
        {
            List<BenchmarkTierResult> results = new List<BenchmarkTierResult>();

            foreach (int count in counts)
            {
                string tierName = count >= 1000 ? $"{count / 1000}K" : count.ToString();
                NativePluginWrapper.RunNativeHeadlessBenchmark(count, iterations, enableCulling, out var averages);

                results.Add(new BenchmarkTierResult
                {
                    tierName = tierName,
                    entityCount = count,
                    unityFps = averages.totalNativeFrameTimeMs > 0 ? (1000.0f / averages.totalNativeFrameTimeMs) : 0,
                    unityFrameTimeMs = averages.totalNativeFrameTimeMs,
                    nativeStats = averages
                });
            }

            return results;
        }
    }
}
