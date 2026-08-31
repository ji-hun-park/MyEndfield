#include "SceneLoader.h"
#include <fstream>
#include <iostream>

namespace Endfield {

bool SceneLoader::LoadScene(const std::string& filePath, ECSManager& ecsManager, std::vector<AABB>& outAABBs, std::vector<VulkanBackend::InstanceData>& outInstances, std::vector<MeshData>& outMeshes) {
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

    // 2. 메쉬 데이터 읽기
    uint32_t meshCount = 0;
    file.read(reinterpret_cast<char*>(&meshCount), sizeof(uint32_t));
    VulkanBackend::LogToUnity("[SceneLoader] Loading " + std::to_string(meshCount) + " meshes...");

    outMeshes.resize(meshCount);
    for (uint32_t m = 0; m < meshCount; ++m) {
        uint32_t vertexCount = 0, subMeshCount = 0;
        file.read(reinterpret_cast<char*>(&vertexCount), sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(&subMeshCount), sizeof(uint32_t));

        outMeshes[m].vertices.resize(vertexCount);
        for (uint32_t v = 0; v < vertexCount; ++v) {
            Vertex& vtx = outMeshes[m].vertices[v];
            file.read(reinterpret_cast<char*>(&vtx.posX), sizeof(float));
            file.read(reinterpret_cast<char*>(&vtx.posY), sizeof(float));
            file.read(reinterpret_cast<char*>(&vtx.posZ), sizeof(float));
            file.read(reinterpret_cast<char*>(&vtx.normX), sizeof(float));
            file.read(reinterpret_cast<char*>(&vtx.normY), sizeof(float));
            file.read(reinterpret_cast<char*>(&vtx.normZ), sizeof(float));
            file.read(reinterpret_cast<char*>(&vtx.uvX), sizeof(float));
            file.read(reinterpret_cast<char*>(&vtx.uvY), sizeof(float));
        }

        outMeshes[m].subMeshes.resize(subMeshCount);
        for (uint32_t s = 0; s < subMeshCount; ++s) {
            uint32_t indexCount = 0;
            file.read(reinterpret_cast<char*>(&indexCount), sizeof(uint32_t));
            outMeshes[m].subMeshes[s].indices.resize(indexCount);
            for (uint32_t i = 0; i < indexCount; ++i) {
                file.read(reinterpret_cast<char*>(&outMeshes[m].subMeshes[s].indices[i]), sizeof(int32_t));
            }
        }
    }

    // 3. 오브젝트(인스턴스) 개수 읽기
    uint32_t objectCount = 0;
    file.read(reinterpret_cast<char*>(&objectCount), sizeof(uint32_t));
    VulkanBackend::LogToUnity("[SceneLoader] Loading " + std::to_string(objectCount) + " objects from scene...");

    outAABBs.reserve(objectCount);
    outInstances.reserve(objectCount);

    ComponentMask mask;
    mask.low = 0b111; // 0, 1, 2 비트
    for (uint32_t i = 0; i < objectCount; i++) {
        float matrix[16];
        file.read(reinterpret_cast<char*>(matrix), sizeof(float) * 16);

        AABB bounds;
        file.read(reinterpret_cast<char*>(&bounds.minBounds), sizeof(float) * 3);
        file.read(reinterpret_cast<char*>(&bounds.maxBounds), sizeof(float) * 3);
        
        int32_t meshId, subMeshIndex, matId;
        file.read(reinterpret_cast<char*>(&meshId), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&subMeshIndex), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&matId), sizeof(int32_t));

        if (meshId < 0) continue; // Dummy data (renderer without mesh)

        outAABBs.push_back(bounds);

        VulkanBackend::InstanceData inst;
        for (int k = 0; k < 16; ++k) inst.mvpMatrix[k] = matrix[k];
        inst.sortKey.materialID = static_cast<uint16_t>(matId);
        inst.sortKey.pipelineID = static_cast<uint16_t>(meshId); // meshId를 pipelineID로 씀 (임시)
        inst.sortKey.depth = 0;
        inst.subMeshIndex = static_cast<uint32_t>(subMeshIndex);
        outInstances.push_back(inst);

        Entity ent = ecsManager.CreateEntity(mask);
    }

    VulkanBackend::LogToUnity("[SceneLoader] Scene loaded successfully.");
    return true;
}

} // namespace Endfield

