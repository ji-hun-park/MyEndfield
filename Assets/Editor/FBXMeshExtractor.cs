using UnityEngine;
using UnityEditor;
using System.Collections.Generic;

namespace Endfield.Editor
{
    public class FBXMeshExtractor
    {
        // 1. Project 창에서 FBX 에셋을 우클릭하여 모든 Mesh 정보 가져오기
        [MenuItem("Assets/Endfield/Extract Meshes from FBX Asset")]
        public static void ExtractMeshesFromFBXAsset()
        {
            Object selectedAsset = Selection.activeObject;
            if (selectedAsset == null) return;

            string assetPath = AssetDatabase.GetAssetPath(selectedAsset);
            if (!assetPath.ToLower().EndsWith(".fbx"))
            {
                Debug.LogWarning("선택한 에셋이 FBX 파일이 아닙니다.");
                return;
            }

            // FBX 파일 내부에 패키징된 모든 에셋(Mesh, Animation, Material 등)을 로드
            Object[] allAssets = AssetDatabase.LoadAllAssetsAtPath(assetPath);
            List<Mesh> fbMeshes = new List<Mesh>();

            foreach (var asset in allAssets)
            {
                if (asset is Mesh mesh)
                {
                    fbMeshes.Add(mesh);
                    Debug.Log($"[Asset 추출] FBX 내 메쉬 발견: {mesh.name} / 버텍스 수: {mesh.vertexCount} / 서브메쉬 수: {mesh.subMeshCount}");
                }
            }
        }

        // 2. Hierarchy 창에서 오브젝트를 우클릭하여 자식들의 모든 Mesh 정보 가져오기
        [MenuItem("GameObject/Endfield/Extract Meshes from Hierarchy", false, 10)]
        public static void ExtractMeshesFromHierarchy()
        {
            GameObject selectedGO = Selection.activeGameObject;
            if (selectedGO == null) return;

            // 일반 MeshRenderer를 사용하는 경우 (MeshFilter 검색)
            MeshFilter[] meshFilters = selectedGO.GetComponentsInChildren<MeshFilter>(true); // true: 비활성화된 자식도 포함
            foreach (var filter in meshFilters)
            {
                if (filter.sharedMesh != null)
                {
                    Debug.Log($"[Hierarchy 추출 - Static] 게임오브젝트 '{filter.gameObject.name}'의 메쉬: {filter.sharedMesh.name}");
                    AnalyzeSubMeshes(filter.sharedMesh);
                }
            }

            // 캐릭터 등 애니메이션이 있는 SkinnedMeshRenderer를 사용하는 경우
            SkinnedMeshRenderer[] skinnedRenderers = selectedGO.GetComponentsInChildren<SkinnedMeshRenderer>(true);
            foreach (var skin in skinnedRenderers)
            {
                if (skin.sharedMesh != null)
                {
                    Debug.Log($"[Hierarchy 추출 - Skinned] 게임오브젝트 '{skin.gameObject.name}'의 메쉬: {skin.sharedMesh.name}");
                    AnalyzeSubMeshes(skin.sharedMesh);
                }
            }
        }

        // 3. 단일 메쉬 내의 여러 서브메쉬(마테리얼 단위 분리) 정보 분석
        private static void AnalyzeSubMeshes(Mesh mesh)
        {
            int subMeshCount = mesh.subMeshCount;
            if (subMeshCount > 1)
            {
                Debug.Log($"  ㄴ 메쉬 '{mesh.name}'는 {subMeshCount}개의 서브메쉬로 이루어져 있습니다. (마테리얼 슬롯이 여러 개인 경우)");

                for (int i = 0; i < subMeshCount; i++)
                {
                    // 각 서브메쉬 별 독립적인 인덱스(삼각형) 배열 추출
                    int[] subMeshIndices = mesh.GetTriangles(i);
                    Debug.Log($"     - 서브메쉬 Index {i}: {subMeshIndices.Length / 3} 개의 폴리곤(삼각형) 보유");
                }
            }
        }
    }
}
