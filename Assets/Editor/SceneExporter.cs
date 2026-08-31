using System.IO;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

namespace Endfield.Editor
{
    public class SceneExporter : EditorWindow
    {
        struct ExportInstance
        {
            public Matrix4x4 matrix;
            public Bounds bounds;
            public int meshId;
            public int subMeshIndex;
            public int matId;
        }

        [MenuItem("Endfield/Export Scene to Native")]
        public static void ExportScene()
        {
            string exportPath = Path.Combine(Application.dataPath, "../NativeCore/ExportedScene.bin");
            MeshRenderer[] renderers = Object.FindObjectsByType<MeshRenderer>(FindObjectsInactive.Exclude);

            try
            {
                int exportedMeshCount = 0;
                int exportedInstanceCount = 0;

                using (BinaryWriter writer = new BinaryWriter(File.Open(exportPath, FileMode.Create)))
                {
                    writer.Write(new char[] { 'E', 'N', 'D', 'F' });

                    Dictionary<Mesh, int> meshMap = new Dictionary<Mesh, int>();
                    List<Mesh> uniqueMeshes = new List<Mesh>();
                    int nextMeshId = 0;

                    // 1. 고유 메쉬 수집
                    foreach (var renderer in renderers)
                    {
                        MeshFilter filter = renderer.GetComponent<MeshFilter>();
                        if (filter == null || filter.sharedMesh == null) continue;

                        if (!meshMap.ContainsKey(filter.sharedMesh))
                        {
                            meshMap[filter.sharedMesh] = nextMeshId++;
                            uniqueMeshes.Add(filter.sharedMesh);
                        }
                    }

                    // 2. 메쉬 데이터 기록
                    writer.Write(uniqueMeshes.Count);
                    foreach (var mesh in uniqueMeshes)
                    {
                        Vector3[] vertices = mesh.vertices;
                        Vector3[] normals = mesh.normals;
                        Vector2[] uvs = mesh.uv;

                        writer.Write(vertices.Length);

                        // 서브메쉬 개수 기록
                        int subMeshCount = mesh.subMeshCount;
                        writer.Write((uint)subMeshCount);

                        // UV나 법선이 없을 경우 대비
                        bool hasNormals = normals.Length == vertices.Length;
                        bool hasUVs = uvs.Length == vertices.Length;

                        for (int i = 0; i < vertices.Length; i++)
                        {
                            writer.Write(vertices[i].x);
                            writer.Write(vertices[i].y);
                            writer.Write(vertices[i].z);

                            if (hasNormals)
                            {
                                writer.Write(normals[i].x);
                                writer.Write(normals[i].y);
                                writer.Write(normals[i].z);
                            }
                            else
                            {
                                writer.Write(0.0f);
                                writer.Write(1.0f);
                                writer.Write(0.0f);
                            }

                            if (hasUVs)
                            {
                                writer.Write(uvs[i].x);
                                writer.Write(uvs[i].y);
                            }
                            else
                            {
                                writer.Write(0.0f);
                                writer.Write(0.0f);
                            }
                        }

                        // 서브메쉬별 인덱스 데이터 기록
                        for (int i = 0; i < subMeshCount; i++)
                        {
                            int[] indices = mesh.GetTriangles(i);
                            writer.Write((uint)indices.Length);
                            for (int j = 0; j < indices.Length; j++)
                            {
                                writer.Write(indices[j]);
                            }
                        }
                    }

                    // 3. 인스턴스 데이터 수집 (하나의 렌더러가 여러 마테리얼/서브메쉬를 가질 수 있으므로 인스턴스 분할)
                    Dictionary<Material, int> materialMap = new Dictionary<Material, int>();
                    int nextMaterialId = 0;
                    List<ExportInstance> exportInstances = new List<ExportInstance>();

                    foreach (var renderer in renderers)
                    {
                        MeshFilter filter = renderer.GetComponent<MeshFilter>();
                        if (filter == null || filter.sharedMesh == null)
                        {
                            // 유효하지 않은 경우 더미 데이터 기록 (또는 생략)
                            continue;
                        }

                        int meshId = meshMap[filter.sharedMesh];
                        Material[] mats = renderer.sharedMaterials;
                        int subMeshCount = filter.sharedMesh.subMeshCount;

                        for (int subIdx = 0; subIdx < subMeshCount; subIdx++)
                        {
                            Material mat = subIdx < mats.Length ? mats[subIdx] : renderer.sharedMaterial;
                            int matId = 0;
                            if (mat != null)
                            {
                                if (!materialMap.TryGetValue(mat, out matId))
                                {
                                    matId = nextMaterialId++;
                                    materialMap[mat] = matId;
                                }
                            }

                            ExportInstance inst = new ExportInstance();
                            inst.matrix = renderer.transform.localToWorldMatrix;
                            inst.bounds = renderer.bounds;
                            inst.meshId = meshId;
                            inst.subMeshIndex = subIdx;
                            inst.matId = matId;
                            exportInstances.Add(inst);
                        }
                    }

                    // 기록
                    writer.Write((uint)exportInstances.Count);
                    foreach (var inst in exportInstances)
                    {
                        for (int i = 0; i < 16; i++) writer.Write(inst.matrix[i]);

                        writer.Write(inst.bounds.min.x); writer.Write(inst.bounds.min.y); writer.Write(inst.bounds.min.z);
                        writer.Write(inst.bounds.max.x); writer.Write(inst.bounds.max.y); writer.Write(inst.bounds.max.z);

                        writer.Write(inst.meshId);
                        writer.Write(inst.subMeshIndex);
                        writer.Write(inst.matId);
                    }

                    exportedMeshCount = uniqueMeshes.Count;
                    exportedInstanceCount = exportInstances.Count;
                }

                Debug.Log($"[Endfield SceneExporter] Successfully exported {exportedMeshCount} meshes and {exportedInstanceCount} objects to: {exportPath}");
                EditorUtility.DisplayDialog("Export Complete", "Scene exported successfully for Native C++ backend.", "OK");
            }
            catch (System.Exception e)
            {
                Debug.LogError($"[Endfield SceneExporter] Failed to export scene to {exportPath}: {e.Message}\n{e.StackTrace}");
                EditorUtility.DisplayDialog("Export Failed", $"Failed to export scene to {exportPath}.\nSee console for details.", "OK");
            }
        }
    }
}

