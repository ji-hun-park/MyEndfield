using System;
using System.IO;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;
using Endfield.NativeInterop;

namespace Endfield.Editor
{
    public class BenchmarkEditorWindow : EditorWindow
    {
        private int m_SelectedTab = 0;
        private readonly string[] m_TabTitles = new string[] { "Headless C++ Benchmark", "Live In-Engine Runner", "Saved Reports" };

        // Headless settings
        private int m_HeadlessCount = 50000;
        private int m_HeadlessIterations = 3;
        private bool m_HeadlessCulling = true;
        private List<BenchmarkTierResult> m_HeadlessResults = new List<BenchmarkTierResult>();
        private bool m_HasRunHeadless = false;

        // Reports settings
        private string[] m_ReportFiles = new string[0];
        private int m_SelectedReportIndex = -1;
        private string m_SelectedReportContent = "";
        private Vector2 m_ReportScrollPos;
        private Vector2 m_MainScrollPos;

        [MenuItem("Endfield/Benchmark Runner")]
        public static void ShowWindow()
        {
            var window = GetWindow<BenchmarkEditorWindow>("Endfield Benchmark");
            window.minSize = new Vector2(550, 520);
            window.Show();
        }

        private void OnEnable()
        {
            RefreshReportsList();
        }

        private void OnGUI()
        {
            m_MainScrollPos = EditorGUILayout.BeginScrollView(m_MainScrollPos);

            EditorGUILayout.Space(6);
            EditorGUILayout.LabelField("Arknights: Endfield Benchmark Suite", EditorStyles.boldLabel);
            EditorGUILayout.HelpBox("Verify and measure Custom C++ ECS, Parallel Culling, 64-bit SortKey, and Zero-Alloc Batching performance against the Unite Seoul 2026 target metrics.", MessageType.Info);
            EditorGUILayout.Space(4);

            m_SelectedTab = GUILayout.Toolbar(m_SelectedTab, m_TabTitles, GUILayout.Height(26));
            EditorGUILayout.Space(8);

            switch (m_SelectedTab)
            {
                case 0:
                    DrawHeadlessTab();
                    break;
                case 1:
                    DrawLiveRunnerTab();
                    break;
                case 2:
                    DrawReportsTab();
                    break;
            }

            EditorGUILayout.EndScrollView();
        }

        private void DrawHeadlessTab()
        {
            EditorGUILayout.LabelField("Native C++ Headless Stress Test", EditorStyles.boldLabel);
            EditorGUILayout.LabelField("Executes pure C++ ECS, frustum culling, std::sort on 64-bit SortKey, and parallel worker batching without requiring a GPU window.");
            EditorGUILayout.Space(6);

            m_HeadlessCount = EditorGUILayout.IntSlider("Entity Count", m_HeadlessCount, 1000, 600000);

            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.PrefixLabel("Quick Presets");
            if (GUILayout.Button("10K")) m_HeadlessCount = 10000;
            if (GUILayout.Button("50K")) m_HeadlessCount = 50000;
            if (GUILayout.Button("100K")) m_HeadlessCount = 100000;
            if (GUILayout.Button("500K (Target)")) m_HeadlessCount = 500000;
            if (GUILayout.Button("600K (PC Limit)")) m_HeadlessCount = 600000;
            EditorGUILayout.EndHorizontal();

            m_HeadlessIterations = EditorGUILayout.IntSlider("Iterations", m_HeadlessIterations, 1, 10);
            m_HeadlessCulling = EditorGUILayout.Toggle("Enable Frustum Culling", m_HeadlessCulling);

            EditorGUILayout.Space(10);
            if (GUILayout.Button($"RUN C++ HEADLESS BENCHMARK ({m_HeadlessCount:N0} ENTITIES)", GUILayout.Height(32)))
            {
                RunHeadlessTest();
            }

            if (m_HasRunHeadless && m_HeadlessResults.Count > 0)
            {
                EditorGUILayout.Space(10);
                EditorGUILayout.LabelField("Benchmark Results Summary", EditorStyles.boldLabel);

                var r = m_HeadlessResults[0];
                var s = r.nativeStats;
                float cullRatio = s.totalInstances > 0 ? (1.0f - ((float)s.visibleInstances / s.totalInstances)) * 100.0f : 0.0f;
                float sortAndBatchTime = s.sortingTimeMs + s.batchingTimeMs;

                EditorGUILayout.BeginVertical(EditorStyles.helpBox);
                EditorGUILayout.LabelField($"Total Candidates: {s.totalInstances:N0}");
                EditorGUILayout.LabelField($"Surviving (Visible): {s.visibleInstances:N0} (Culled: {cullRatio:F1}%)");
                EditorGUILayout.Space(4);
                EditorGUILayout.LabelField($"1. ECS Query SoA: {s.ecsQueryTimeMs:F3} ms");
                EditorGUILayout.LabelField($"2. Frustum Culling: {s.frustumCullingTimeMs:F3} ms");
                EditorGUILayout.LabelField($"3. 64-bit SortKey Sort: {s.sortingTimeMs:F3} ms");
                EditorGUILayout.LabelField($"4. Parallel Batch & UBO Copy: {s.batchingTimeMs:F3} ms");
                EditorGUILayout.Space(4);
                EditorGUILayout.LabelField($"Sort + Batch Combined: {sortAndBatchTime:F3} ms (Endfield PC Target: ~1.0 ms)");
                EditorGUILayout.LabelField($"Total Native Frame Time: {s.totalNativeFrameTimeMs:F3} ms");
                EditorGUILayout.LabelField($"Theoretical Max Native FPS: {(s.totalNativeFrameTimeMs > 0 ? (1000.0f / s.totalNativeFrameTimeMs) : 0):F0} FPS");
                EditorGUILayout.EndVertical();

                EditorGUILayout.Space(6);
                if (GUILayout.Button("Export Benchmark Report to Markdown"))
                {
                    string md = BenchmarkReportGenerator.GenerateMarkdownReport("Headless C++ Benchmark", m_HeadlessResults);
                    BenchmarkReportGenerator.SaveReportToFile(md);
                    RefreshReportsList();
                }
            }
        }

        private void RunHeadlessTest()
        {
            m_HeadlessResults.Clear();
            NativePluginWrapper.RunNativeHeadlessBenchmark(m_HeadlessCount, m_HeadlessIterations, m_HeadlessCulling, out var averages);

            m_HeadlessResults.Add(new BenchmarkTierResult
            {
                tierName = $"{m_HeadlessCount / 1000}K",
                entityCount = m_HeadlessCount,
                unityFps = averages.totalNativeFrameTimeMs > 0 ? (1000.0f / averages.totalNativeFrameTimeMs) : 0,
                unityFrameTimeMs = averages.totalNativeFrameTimeMs,
                nativeStats = averages
            });

            m_HasRunHeadless = true;
        }

        private void DrawLiveRunnerTab()
        {
            EditorGUILayout.LabelField("Live-ops In-Engine Benchmark Suite", EditorStyles.boldLabel);

            if (!Application.isPlaying)
            {
                EditorGUILayout.HelpBox("To run in-engine live profiling and interactive HUD tests, enter PlayMode in Unity Editor.", MessageType.Warning);
                if (GUILayout.Button("Enter PlayMode", GUILayout.Height(30)))
                {
                    EditorApplication.isPlaying = true;
                }
                return;
            }

            if (BenchmarkManager.Instance == null)
            {
                EditorGUILayout.HelpBox("BenchmarkManager component is not found in the current scene. Click below to add it to the scene.", MessageType.Info);
                if (GUILayout.Button("Create BenchmarkManager in Scene"))
                {
                    GameObject go = new GameObject("BenchmarkController");
                    go.AddComponent<BenchmarkManager>();
                    go.AddComponent<BenchmarkHUD>();
                }
                return;
            }

            var bm = BenchmarkManager.Instance;
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.LabelField($"Current FPS: {bm.CurrentFps:F0} (Avg: {bm.AverageFps:F0}, 1% Low: {bm.OnePercentLowFps:F0})");
            var s = bm.LatestNativeStats;
            EditorGUILayout.LabelField($"Native Instances: {s.visibleInstances:N0} / {s.totalInstances:N0}");
            EditorGUILayout.LabelField($"Native Total Frame Time: {s.totalNativeFrameTimeMs:F3} ms");
            EditorGUILayout.EndVertical();

            EditorGUILayout.Space(8);
            if (bm.IsBenchmarkRunning)
            {
                EditorGUILayout.LabelField($"Running: {bm.BenchmarkStatusMessage}", EditorStyles.boldLabel);
                EditorGUI.ProgressBar(EditorGUILayout.GetControlRect(), bm.BenchmarkProgress, $"{bm.BenchmarkProgress * 100:F0}%");
            }
            else
            {
                if (GUILayout.Button("RUN FULL AUTOMATED BENCHMARK (1K - 500K)", GUILayout.Height(32)))
                {
                    bm.StartAutomatedBenchmark();
                }
            }
        }

        private void DrawReportsTab()
        {
            EditorGUILayout.LabelField("Generated Benchmark Reports", EditorStyles.boldLabel);

            EditorGUILayout.BeginHorizontal();
            if (GUILayout.Button("Refresh Reports List", GUILayout.Width(150)))
            {
                RefreshReportsList();
            }
            if (GUILayout.Button("Open Reports Folder", GUILayout.Width(150)))
            {
                string dir = Path.Combine(Application.dataPath, "../docs/benchmark_reports");
                if (!Directory.Exists(dir)) Directory.CreateDirectory(dir);
                EditorUtility.RevealInFinder(dir);
            }
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.Space(6);

            if (m_ReportFiles.Length == 0)
            {
                EditorGUILayout.LabelField("No reports found yet. Run a benchmark to generate reports.");
                return;
            }

            EditorGUILayout.LabelField("Select Report:");
            for (int i = 0; i < m_ReportFiles.Length; i++)
            {
                string fileName = Path.GetFileName(m_ReportFiles[i]);
                if (GUILayout.Toggle(m_SelectedReportIndex == i, fileName, "Button"))
                {
                    if (m_SelectedReportIndex != i)
                    {
                        m_SelectedReportIndex = i;
                        m_SelectedReportContent = File.ReadAllText(m_ReportFiles[i]);
                    }
                }
            }

            if (m_SelectedReportIndex >= 0 && !string.IsNullOrEmpty(m_SelectedReportContent))
            {
                EditorGUILayout.Space(8);
                EditorGUILayout.LabelField($"Viewing: {Path.GetFileName(m_ReportFiles[m_SelectedReportIndex])}", EditorStyles.boldLabel);
                m_ReportScrollPos = EditorGUILayout.BeginScrollView(m_ReportScrollPos, GUILayout.Height(250));
                EditorGUILayout.TextArea(m_SelectedReportContent, GUILayout.ExpandHeight(true));
                EditorGUILayout.EndScrollView();
            }
        }

        private void RefreshReportsList()
        {
            string dir = Path.Combine(Application.dataPath, "../docs/benchmark_reports");
            if (Directory.Exists(dir))
            {
                m_ReportFiles = Directory.GetFiles(dir, "*.md");
                Array.Sort(m_ReportFiles);
                Array.Reverse(m_ReportFiles); // newest first
            }
            else
            {
                m_ReportFiles = new string[0];
            }
            m_SelectedReportIndex = -1;
            m_SelectedReportContent = "";
        }
    }
}

