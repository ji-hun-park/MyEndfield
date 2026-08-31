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
            string exportPath = Path.Combine(Application.dataPath, "../NativeCore/ExportedScene.bin");
            MeshRenderer[] renderers = Object.FindObjectsByType<MeshRenderer>(FindObjectsInactive.Exclude);

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
                    int[] indices = mesh.triangles;

                    writer.Write(vertices.Length);
                    writer.Write(indices.Length);

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

                    for (int i = 0; i < indices.Length; i++)
                    {
                        writer.Write(indices[i]);
                    }
                }

                // 3. 인스턴스 데이터 기록
                writer.Write(renderers.Length);
                Dictionary<Material, int> materialMap = new Dictionary<Material, int>();
                int nextMaterialId = 0;

                foreach (var renderer in renderers)
                {
                    MeshFilter filter = renderer.GetComponent<MeshFilter>();
                    if (filter == null || filter.sharedMesh == null)
                    {
                        // 유효하지 않은 경우 더미 데이터 기록
                        for (int i = 0; i < 16; i++) writer.Write(0.0f); // Matrix
                        for (int i = 0; i < 6; i++) writer.Write(0.0f);  // AABB
                        writer.Write(-1); // MeshID
                        writer.Write(-1); // MatID
                        continue;
                    }

                    int meshId = meshMap[filter.sharedMesh];

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

                    Matrix4x4 matrix = renderer.transform.localToWorldMatrix;
                    for (int i = 0; i < 16; i++) writer.Write(matrix[i]);

                    Bounds bounds = renderer.bounds;
                    writer.Write(bounds.min.x); writer.Write(bounds.min.y); writer.Write(bounds.min.z);
                    writer.Write(bounds.max.x); writer.Write(bounds.max.y); writer.Write(bounds.max.z);

                    writer.Write(meshId);
                    writer.Write(matId);
                }
            }

            Debug.Log($"[Endfield SceneExporter] Successfully exported {uniqueMeshes.Count} meshes and {renderers.Length} objects to: {exportPath}");
            EditorUtility.DisplayDialog("Export Complete", "Scene exported successfully for Native C++ backend.", "OK");
        }
    }
}

