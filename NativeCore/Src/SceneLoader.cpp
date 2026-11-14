#include "SceneLoader.h"
#include <fstream>
#include <iostream>

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

    // 3. 오브젝트(템플릿 인스턴스) 데이터 읽기
    uint32_t objectCount = 0;
    file.read(reinterpret_cast<char*>(&objectCount), sizeof(uint32_t));
    VulkanBackend::LogToUnity("[SceneLoader] Loading " + std::to_string(objectCount) + " template objects from scene...");

    outTemplates.clear();
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

        if (meshId < 0) continue; // Dummy data (renderer without mesh)

        MeshComponent meshComp;
        meshComp.meshId = meshId;
        meshComp.subMeshIndex = subMeshIndex;
        meshComp.materialId = matId;

        outTemplates.push_back({transform, bounds, meshComp});
    }

    VulkanBackend::LogToUnity("[SceneLoader] Loaded " + std::to_string(outTemplates.size()) + " template objects successfully (deferred spawn).");
    return true;
}

} // namespace Endfield

