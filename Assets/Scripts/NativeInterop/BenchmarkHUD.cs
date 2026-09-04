using UnityEngine;

namespace Endfield.NativeInterop
{
    [RequireComponent(typeof(BenchmarkManager))]
    public class BenchmarkHUD : MonoBehaviour
    {
        [Header("HUD Settings")]
        [SerializeField] private KeyCode m_ToggleKey = KeyCode.F1;
        [SerializeField] private KeyCode m_AlternateToggleKey = KeyCode.B;
        [SerializeField] private bool m_ShowHUD = true;

        private BenchmarkManager m_Manager;
        private GUIStyle m_BoxStyle;
        private GUIStyle m_HeaderStyle;
        private GUIStyle m_LabelStyle;
        private GUIStyle m_MetricStyle;
        private GUIStyle m_HighlightStyle;
        private GUIStyle m_ButtonStyle;
        private bool m_StylesInitialized = false;

        private string m_LastReportPath = "";

        private void Start()
        {
            m_Manager = GetComponent<BenchmarkManager>();
            m_Manager.OnBenchmarkCompleted += (path) => m_LastReportPath = path;
        }

        private void Update()
        {
            if (Input.GetKeyDown(m_ToggleKey) || Input.GetKeyDown(m_AlternateToggleKey))
            {
                m_ShowHUD = !m_ShowHUD;
            }
        }

        private void InitializeStyles()
        {
            if (m_StylesInitialized) return;

            m_BoxStyle = new GUIStyle(GUI.skin.box);
            m_BoxStyle.normal.background = MakeTex(2, 2, new Color(0.08f, 0.09f, 0.11f, 0.92f));

            m_HeaderStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 14,
                fontStyle = FontStyle.Bold,
                alignment = TextAnchor.MiddleLeft
            };
            m_HeaderStyle.normal.textColor = new Color(0.35f, 0.75f, 1.0f);

            m_LabelStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 12
            };
            m_LabelStyle.normal.textColor = new Color(0.85f, 0.85f, 0.85f);

            m_MetricStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 12,
                fontStyle = FontStyle.Bold
            };
            m_MetricStyle.normal.textColor = new Color(0.4f, 0.95f, 0.5f);

            m_HighlightStyle = new GUIStyle(GUI.skin.label)
            {
                fontSize = 12,
                fontStyle = FontStyle.Bold
            };
            m_HighlightStyle.normal.textColor = new Color(1.0f, 0.85f, 0.3f);

            m_ButtonStyle = new GUIStyle(GUI.skin.button)
            {
                fontSize = 11,
                fontStyle = FontStyle.Bold
            };

            m_StylesInitialized = true;
        }

        private Texture2D MakeTex(int width, int height, Color col)
        {
            Color[] pix = new Color[width * height];
            for (int i = 0; i < pix.Length; ++i) pix[i] = col;
            Texture2D result = new Texture2D(width, height);
            result.SetPixels(pix);
            result.Apply();
            return result;
        }

        private void OnGUI()
        {
            if (!m_ShowHUD) return;
            InitializeStyles();

            float panelWidth = 360f;
            float panelHeight = m_Manager.IsBenchmarkRunning ? 480f : 430f;
            Rect panelRect = new Rect(15, 15, panelWidth, panelHeight);

            GUI.Box(panelRect, GUIContent.none, m_BoxStyle);

            GUILayout.BeginArea(new Rect(panelRect.x + 12, panelRect.y + 10, panelRect.width - 24, panelRect.height - 20));

            // Title Header
            GUILayout.Label("ARKKNIGHTS: ENDFIELD BENCHMARK", m_HeaderStyle);
            GUILayout.Label($"Press [{m_ToggleKey}] or [{m_AlternateToggleKey}] to toggle HUD", m_LabelStyle);
            GUILayout.Space(6);

            // FPS Section
            GUILayout.BeginHorizontal();
            GUILayout.Label("FPS:", m_LabelStyle, GUILayout.Width(45));
            GUILayout.Label($"{m_Manager.CurrentFps:F0}", m_MetricStyle, GUILayout.Width(50));
            GUILayout.Label("Avg:", m_LabelStyle, GUILayout.Width(35));
            GUILayout.Label($"{m_Manager.AverageFps:F0}", m_MetricStyle, GUILayout.Width(50));
            GUILayout.Label("1% Low:", m_LabelStyle, GUILayout.Width(55));
            GUILayout.Label($"{m_Manager.OnePercentLowFps:F0}", m_HighlightStyle);
            GUILayout.EndHorizontal();

            GUILayout.Space(6);

            // Entity & Culling Metrics
            var s = m_Manager.LatestNativeStats;
            float cullPercent = s.totalInstances > 0 ? (1.0f - ((float)s.visibleInstances / s.totalInstances)) * 100.0f : 0.0f;

            GUILayout.Label($"Candidate Entities: {s.totalInstances:N0}", m_LabelStyle);
            GUILayout.BeginHorizontal();
            GUILayout.Label($"Visible: {s.visibleInstances:N0}", m_MetricStyle, GUILayout.Width(130));
            GUILayout.Label($"Culled: {s.culledFrustum + s.culledOcclusion:N0} ({cullPercent:F1}%)", m_HighlightStyle);
            GUILayout.EndHorizontal();

            GUILayout.Space(6);
            GUILayout.Box(GUIContent.none, GUILayout.Height(1)); // Divider

            // Pipeline Breakdown
            GUILayout.Label("Native C++ Pipeline Timings:", m_HeaderStyle);
            DrawMetricRow("1. ECS Query SoA:", $"{s.ecsQueryTimeMs:F3} ms");
            DrawMetricRow("2. Frustum Culling:", $"{s.frustumCullingTimeMs:F3} ms");
            DrawMetricRow("3. Occlusion Culling:", $"{s.occlusionCullingTimeMs:F3} ms");
            DrawMetricRow("4. 64-bit SortKey Sort:", $"{s.sortingTimeMs:F3} ms");
            DrawMetricRow("5. Parallel Batch & UBO:", $"{s.batchingTimeMs:F3} ms");
            DrawMetricRow("6. Vulkan Submit:", $"{s.renderSubmitTimeMs:F3} ms");
            GUILayout.Space(3);
            DrawMetricRow(">> Total Native Frame:", $"{s.totalNativeFrameTimeMs:F3} ms", true);

            GUILayout.Space(6);
            GUILayout.Box(GUIContent.none, GUILayout.Height(1)); // Divider

            // Controls
            GUILayout.Label("Benchmark Controls:", m_HeaderStyle);
            GUILayout.BeginHorizontal();
            bool frustum = GUILayout.Toggle(m_Manager.EnableFrustumCulling, " Frustum Cull", GUILayout.Width(140));
            if (frustum != m_Manager.EnableFrustumCulling) m_Manager.EnableFrustumCulling = frustum;

            bool occlusion = GUILayout.Toggle(m_Manager.EnableOcclusionCulling, " Occlusion Cull");
            if (occlusion != m_Manager.EnableOcclusionCulling) m_Manager.EnableOcclusionCulling = occlusion;
            GUILayout.EndHorizontal();

            GUILayout.Space(5);
            GUILayout.BeginHorizontal();
            if (GUILayout.Button("+1K", m_ButtonStyle)) NativePluginWrapper.SpawnNativeInstances(1000, 200.0f);
            if (GUILayout.Button("+10K", m_ButtonStyle)) NativePluginWrapper.SpawnNativeInstances(10000, 300.0f);
            if (GUILayout.Button("+50K", m_ButtonStyle)) NativePluginWrapper.SpawnNativeInstances(50000, 500.0f);
            if (GUILayout.Button("+100K", m_ButtonStyle)) NativePluginWrapper.SpawnNativeInstances(100000, 700.0f);
            GUILayout.EndHorizontal();

            GUILayout.Space(6);
            if (m_Manager.IsBenchmarkRunning)
            {
                GUILayout.Label($"[STATUS] {m_Manager.BenchmarkStatusMessage}", m_HighlightStyle);
                GUILayout.HorizontalSlider(m_Manager.BenchmarkProgress, 0.0f, 1.0f);
            }
            else
            {
                if (GUILayout.Button("RUN AUTOMATED BENCHMARK SUITE", m_ButtonStyle, GUILayout.Height(26)))
                {
                    m_Manager.StartAutomatedBenchmark();
                }
            }

            if (!string.IsNullOrEmpty(m_LastReportPath))
            {
                GUILayout.Space(4);
                GUILayout.Label($"Report: {System.IO.Path.GetFileName(m_LastReportPath)}", m_MetricStyle);
            }

            GUILayout.EndArea();
        }

        private void DrawMetricRow(string label, string value, bool isHighlight = false)
        {
            GUILayout.BeginHorizontal();
            GUILayout.Label(label, m_LabelStyle, GUILayout.Width(190));
            GUILayout.Label(value, isHighlight ? m_HighlightStyle : m_MetricStyle);
            GUILayout.EndHorizontal();
        }
    }
}

