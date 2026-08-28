using System.IO;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

namespace Endfield.Editor
{
    public class SceneExporter : EditorWindow
    {
        [MenuItem("Endfield/Export Scene to Native")]
        public static void ExportScene()
        {
            // 타겟 바이너리 파일 경로
            string exportPath = Path.Combine(Application.dataPath, "../NativeCore/ExportedScene.bin");

            // 씬 내의 모든 MeshRenderer 찾기 (Unity 6 최신 API)
            MeshRenderer[] renderers = Object.FindObjectsByType<MeshRenderer>(FindObjectsInactive.Exclude);

            using (BinaryWriter writer = new BinaryWriter(File.Open(exportPath, FileMode.Create)))
            {
                // 1. 헤더 작성 (엔드필드 씬 매직 넘버 및 렌더러 개수)
                writer.Write(new char[] { 'E', 'N', 'D', 'F' });
                writer.Write(renderers.Length);

                // 고유 ID 발급용 딕셔너리
                Dictionary<Mesh, int> meshMap = new Dictionary<Mesh, int>();
                Dictionary<Material, int> materialMap = new Dictionary<Material, int>();

                int nextMeshId = 0;
                int nextMaterialId = 0;

                foreach (var renderer in renderers)
                {
                    MeshFilter filter = renderer.GetComponent<MeshFilter>();
                    if (filter == null || filter.sharedMesh == null)
                        continue;

                    // Mesh 고유 ID 맵핑
                    if (!meshMap.TryGetValue(filter.sharedMesh, out int meshId))
                    {
                        meshId = nextMeshId++;
                        meshMap[filter.sharedMesh] = meshId;
                        // TODO: 이 시점에 실제 Mesh 정점 데이터(Vertex, Index)도 바이너리로 빼야 합니다.
                    }

                    // Material 고유 ID 맵핑
                    Material mat = renderer.sharedMaterial;
                    int matId = 0;
                    if (mat != null)
                    {
                        if (!materialMap.TryGetValue(mat, out matId))
                        {
                            matId = nextMaterialId++;
                            materialMap[mat] = matId;
                        }
                    }

                    // 2. Transform 데이터 기록 (LocalToWorld Matrix - 16 floats)
                    Matrix4x4 matrix = renderer.transform.localToWorldMatrix;
                    for (int i = 0; i < 16; i++)
                    {
                        writer.Write(matrix[i]);
                    }

                    // 3. AABB 데이터 기록 (World Space Bounds)
                    Bounds bounds = renderer.bounds;
                    writer.Write(bounds.min.x);
                    writer.Write(bounds.min.y);
                    writer.Write(bounds.min.z);

                    writer.Write(bounds.max.x);
                    writer.Write(bounds.max.y);
                    writer.Write(bounds.max.z);

                    // 4. Mesh ID 및 Material ID 기록
                    writer.Write(meshId);
                    writer.Write(matId);
                }
            }

            Debug.Log($"[Endfield SceneExporter] Successfully exported {renderers.Length} objects to: {exportPath}");
            EditorUtility.DisplayDialog("Export Complete", "Scene exported successfully for Native C++ backend.", "OK");
        }
    }
}

