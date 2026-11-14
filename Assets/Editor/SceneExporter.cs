using System.IO;
using System.Collections.Generic;
using UnityEditor;
using UnityEngine;

namespace Endfield.Editor
{
    public class SceneExporter : EditorWindow
    {
        private const int MAX_INLINE_SUB_RENDERERS = 64;
        private const int CHUNK_CAPACITY = 1024;

        struct ExportInstance
        {
            public Matrix4x4 matrix;
            public Bounds bounds;
            public int meshId;
            public int subMeshIndex;
            public int matId;
        }

        struct ExportCharacterEntity
        {
            public Matrix4x4 matrix;
            public Bounds bounds;
            public int meshId;
            public int subMeshCount;
            public ushort[] subMeshIndices;
            public ushort[] materialIds;
        }

        [MenuItem("Endfield/Export Scene to Native")]
        public static void ExportScene()
        {
            string exportPath = Path.Combine(Application.dataPath, "../NativeCore/ExportedScene.bin");
            MeshRenderer[] staticRenderers = Object.FindObjectsByType<MeshRenderer>(FindObjectsInactive.Exclude);
            SkinnedMeshRenderer[] skinnedRenderers = Object.FindObjectsByType<SkinnedMeshRenderer>(FindObjectsInactive.Exclude);

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

                    // 1. 고유 메쉬 수집 (MeshRenderer + SkinnedMeshRenderer 모두 지원)
                    foreach (var renderer in staticRenderers)
                    {
                        MeshFilter filter = renderer.GetComponent<MeshFilter>();
                        if (filter == null || filter.sharedMesh == null) continue;

                        if (!meshMap.ContainsKey(filter.sharedMesh))
                        {
                            meshMap[filter.sharedMesh] = nextMeshId++;
                            uniqueMeshes.Add(filter.sharedMesh);
                        }
                    }

                    foreach (var skin in skinnedRenderers)
                    {
                        if (skin == null || skin.sharedMesh == null) continue;

                        if (!meshMap.ContainsKey(skin.sharedMesh))
                        {
                            meshMap[skin.sharedMesh] = nextMeshId++;
                            uniqueMeshes.Add(skin.sharedMesh);
                        }
                    }

                    // 2. 메쉬 데이터 기록 (1세트 공유)
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

                    // 3. 인스턴스 및 인라인 캐릭터 엔티티 수집
                    Dictionary<Material, int> materialMap = new Dictionary<Material, int>();
                    int nextMaterialId = 0;
                    List<ExportInstance> exportInstances = new List<ExportInstance>();
                    List<ExportCharacterEntity> characterEntities = new List<ExportCharacterEntity>();

                    void ProcessRenderer(Transform tr, Bounds bounds, Mesh sharedMesh, Material[] mats, Material fallbackMat)
                    {
                        if (sharedMesh == null || !meshMap.ContainsKey(sharedMesh)) return;

                        int meshId = meshMap[sharedMesh];
                        int subMeshCount = Mathf.Min(sharedMesh.subMeshCount, MAX_INLINE_SUB_RENDERERS);

                        ExportCharacterEntity charEnt = new ExportCharacterEntity
                        {
                            matrix = tr.localToWorldMatrix,
                            bounds = bounds,
                            meshId = meshId,
                            subMeshCount = subMeshCount,
                            subMeshIndices = new ushort[MAX_INLINE_SUB_RENDERERS],
                            materialIds = new ushort[MAX_INLINE_SUB_RENDERERS]
                        };

                        for (int subIdx = 0; subIdx < subMeshCount; subIdx++)
                        {
                            Material mat = (mats != null && subIdx < mats.Length) ? mats[subIdx] : fallbackMat;
                            int matId = 0;
                            if (mat != null && !materialMap.TryGetValue(mat, out matId))
                            {
                                matId = nextMaterialId++;
                                materialMap[mat] = matId;
                            }

                            charEnt.subMeshIndices[subIdx] = (ushort)subIdx;
                            charEnt.materialIds[subIdx] = (ushort)matId;

                            exportInstances.Add(new ExportInstance
                            {
                                matrix = tr.localToWorldMatrix,
                                bounds = bounds,
                                meshId = meshId,
                                subMeshIndex = subIdx,
                                matId = matId
                            });
                        }

                        characterEntities.Add(charEnt);
                    }

                    foreach (var renderer in staticRenderers)
                    {
                        MeshFilter filter = renderer.GetComponent<MeshFilter>();
                        if (filter != null && filter.sharedMesh != null)
                        {
                            ProcessRenderer(renderer.transform, renderer.bounds, filter.sharedMesh, renderer.sharedMaterials, renderer.sharedMaterial);
                        }
                    }

                    foreach (var skin in skinnedRenderers)
                    {
                        if (skin != null && skin.sharedMesh != null)
                        {
                            ProcessRenderer(skin.transform, skin.bounds, skin.sharedMesh, skin.sharedMaterials, skin.sharedMaterial);
                        }
                    }

                    // 4. 기존 하위 호환용 템플릿 인스턴스 기록
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

                    // 5. "ECSC" Zero-Parsing SoA Chunk Block 기록
                    writer.Write(new char[] { 'E', 'C', 'S', 'C' });
                    int totalChars = characterEntities.Count;
                    int chunkCount = (totalChars + CHUNK_CAPACITY - 1) / CHUNK_CAPACITY;
                    writer.Write((uint)chunkCount);

                    // MASK_TRANSFORM(Bit 1) | MASK_BOUNDS(Bit 2) | MASK_SUB_RENDERERS(Bit 4) | MASK_VISIBILITY(Bit 5)
                    ulong maskLow = (1UL << 1) | (1UL << 2) | (1UL << 4) | (1UL << 5);
                    ulong maskHigh = 0UL;

                    for (int c = 0; c < chunkCount; c++)
                    {
                        int startIdx = c * CHUNK_CAPACITY;
                        int countInChunk = Mathf.Min(CHUNK_CAPACITY, totalChars - startIdx);

                        writer.Write(maskLow);
                        writer.Write(maskHigh);
                        writer.Write((uint)countInChunk);

                        // Column 0 (Bit 1): TransformComponent 연속 배열 (64B * countInChunk)
                        for (int i = 0; i < countInChunk; i++)
                        {
                            Matrix4x4 m = characterEntities[startIdx + i].matrix;
                            for (int k = 0; k < 16; k++) writer.Write(m[k]);
                        }

                        // Column 1 (Bit 2): BoundsComponent 연속 배열 (24B * countInChunk)
                        for (int i = 0; i < countInChunk; i++)
                        {
                            Bounds b = characterEntities[startIdx + i].bounds;
                            writer.Write(b.min.x); writer.Write(b.min.y); writer.Write(b.min.z);
                            writer.Write(b.max.x); writer.Write(b.max.y); writer.Write(b.max.z);
                        }

                        // Column 2 (Bit 4): InlineSubRendererComponent 연속 배열 (alignas(64), 320B * countInChunk)
                        for (int i = 0; i < countInChunk; i++)
                        {
                            var ch = characterEntities[startIdx + i];
                            writer.Write((uint)ch.subMeshCount); // 4B
                            writer.Write((int)ch.meshId);        // 4B
                            for (int s = 0; s < MAX_INLINE_SUB_RENDERERS; s++) writer.Write(ch.subMeshIndices[s]); // 128B
                            for (int s = 0; s < MAX_INLINE_SUB_RENDERERS; s++) writer.Write(ch.materialIds[s]);    // 128B
                            // alignas(64) 구조체 크기(320 Bytes) 정합을 위한 56바이트 패딩 기록 (264B + 56B = 320B)
                            for (int pad = 0; pad < 56; pad++) writer.Write((byte)0);
                        }

                        // Column 3 (Bit 5): Visibility Mask 연속 배열 (4B * countInChunk)
                        for (int i = 0; i < countInChunk; i++)
                        {
                            writer.Write(1u);
                        }
                    }

                    exportedMeshCount = uniqueMeshes.Count;
                    exportedInstanceCount = exportInstances.Count;
                }

                Debug.Log($"[Endfield SceneExporter] Successfully exported {exportedMeshCount} meshes and {exportedInstanceCount} submesh templates (+ Zero-Parsing SoA Chunks) to: {exportPath}");
            }
            catch (System.Exception e)
            {
                Debug.LogError($"[Endfield SceneExporter] Failed to export scene to {exportPath}: {e.Message}\n{e.StackTrace}");
            }
        }
    }
}
