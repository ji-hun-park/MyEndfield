#define NOMINMAX
#include "SceneLoader.h"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>

namespace Endfield {

bool SceneLoader::LoadScene(const std::string& filePath, ECSManager& ecsManager, std::vector<MeshData>& outMeshes, std::vector<TemplateData>& outTemplates) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        VulkanBackend::LogToUnity("[SceneLoader ERROR] Failed to open scene file: " + filePath);
        return false;
    }

    // 1. 매직 넘버 확인
    char magic[4];
    file.read(magic, 4);
    if (magic[0] != 'E' || magic[1] != 'N' || magic[2] != 'D' || magic[3] != 'F') {
        VulkanBackend::LogToUnity("[SceneLoader ERROR] Invalid scene file format.");
        return false;
    }

    // 2. 메쉬 데이터 Zero-Parsing 벌크 로드 (정점·인덱스 루프 제거)
    uint32_t meshCount = 0;
    file.read(reinterpret_cast<char*>(&meshCount), sizeof(uint32_t));
    VulkanBackend::LogToUnity("[SceneLoader] Loading " + std::to_string(meshCount) + " meshes (Zero-Parsing Bulk Read)...");

    outMeshes.resize(meshCount);
    for (uint32_t m = 0; m < meshCount; ++m) {
        uint32_t vertexCount = 0, subMeshCount = 0;
        file.read(reinterpret_cast<char*>(&vertexCount), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&subMeshCount), sizeof(uint32_t));

        outMeshes[m].vertices.resize(vertexCount);
        if (vertexCount > 0) {
            // Vertex 구조체(32바이트) 배열 전체를 1회 시스템 콜로 메모리에 직결 로드
            file.read(reinterpret_cast<char*>(outMeshes[m].vertices.data()), static_cast<std::streamsize>(vertexCount) * sizeof(Vertex));
        }

        outMeshes[m].subMeshes.resize(subMeshCount);
        for (uint32_t s = 0; s < subMeshCount; ++s) {
            uint32_t indexCount = 0;
            file.read(reinterpret_cast<char*>(&indexCount), sizeof(uint32_t));
            outMeshes[m].subMeshes[s].indices.resize(indexCount);
            if (indexCount > 0) {
                // 서브메시 인덱스 배열 전체를 1회 시스템 콜로 메모리에 직결 로드
                file.read(reinterpret_cast<char*>(outMeshes[m].subMeshes[s].indices.data()), static_cast<std::streamsize>(indexCount) * sizeof(int32_t));
            }
        }
    }

    // 3. 오브젝트(템플릿 인스턴스) 데이터 읽기
    uint32_t objectCount = 0;
    file.read(reinterpret_cast<char*>(&objectCount), sizeof(uint32_t));

    outTemplates.clear();
    outTemplates.reserve(objectCount);

    for (uint32_t i = 0; i < objectCount; i++) {
        TransformComponent transform;
        file.read(reinterpret_cast<char*>(transform.localToWorld), sizeof(float) * 16);

        BoundsComponent bounds;
        file.read(reinterpret_cast<char*>(&bounds.minBounds), sizeof(float) * 3);
        file.read(reinterpret_cast<char*>(&bounds.maxBounds), sizeof(float) * 3);
        
        int32_t meshId, subMeshIndex, matId;
        file.read(reinterpret_cast<char*>(&meshId), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&subMeshIndex), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&matId), sizeof(int32_t));

        if (meshId < 0) continue;

        MeshComponent meshComp;
        meshComp.meshId = meshId;
        meshComp.subMeshIndex = subMeshIndex;
        meshComp.materialId = matId;

        outTemplates.push_back({transform, bounds, meshComp});
    }

    // 4. 파일 내 SoA 청크 블록("ECSC")이 존재하면 파싱 없이 청크 컬럼으로 직결 read
    char chunkMagic[4] = { 0 };
    if (file.read(chunkMagic, 4) &&
        chunkMagic[0] == 'E' && chunkMagic[1] == 'C' && chunkMagic[2] == 'S' && chunkMagic[3] == 'C')
    {
        uint32_t chunkCount = 0;
        file.read(reinterpret_cast<char*>(&chunkCount), sizeof(uint32_t));
        ecsManager.Clear();

        for (uint32_t cIdx = 0; cIdx < chunkCount; ++cIdx) {
            ComponentMask mask{};
            uint32_t entityCount = 0;
            file.read(reinterpret_cast<char*>(&mask.low), sizeof(uint64_t));
            file.read(reinterpret_cast<char*>(&mask.high), sizeof(uint64_t));
            file.read(reinterpret_cast<char*>(&entityCount), sizeof(uint32_t));

            Chunk* chunk = ecsManager.AllocateChunkRaw(mask, entityCount);
            if (!chunk) break;

            for (size_t col = 0; col < chunk->columns.size(); ++col) {
                size_t compSize = ComponentRegistry::GetComponentSize(chunk->componentIndices[col]);
                if (compSize > 0 && chunk->columns[col] != nullptr) {
                    // 디스크의 연속된 컬럼 블록을 청크의 64B 정렬 컬럼 배열로 1회 직결 복사
                    file.read(reinterpret_cast<char*>(chunk->columns[col]), static_cast<std::streamsize>(compSize) * entityCount);
                }
            }
        }
        VulkanBackend::LogToUnity("[SceneLoader] Zero-Parsing SoA Chunks loaded directly from ExportedScene.bin.");
    }

    VulkanBackend::LogToUnity("[SceneLoader] Loaded " + std::to_string(outTemplates.size()) + " template objects successfully.");
    return true;
}

} // namespace Endfield
