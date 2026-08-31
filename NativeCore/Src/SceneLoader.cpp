#include "SceneLoader.h"
#include <fstream>
#include <iostream>

namespace Endfield {

bool SceneLoader::LoadScene(const std::string& filePath, ECSManager& ecsManager, std::vector<AABB>& outAABBs, std::vector<VulkanBackend::InstanceData>& outInstances) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[SceneLoader] Failed to open scene file: " << filePath << "\n";
        return false;
    }

    // 1. 매직 넘버 확인
    char magic[4];
    file.read(magic, 4);
    if (magic[0] != 'E' || magic[1] != 'N' || magic[2] != 'D' || magic[3] != 'F') {
        std::cerr << "[SceneLoader] Invalid scene file format.\n";
        return false;
    }

    // 2. 오브젝트 갯수 읽기
    uint32_t objectCount = 0;
    file.read(reinterpret_cast<char*>(&objectCount), sizeof(uint32_t));
    std::cout << "[SceneLoader] Loading " << objectCount << " objects from scene...\n";

    outAABBs.reserve(objectCount);
    outInstances.reserve(objectCount);

    // 컴포넌트 마스크 준비 (0번 비트 = Transform(64바이트), 1번 비트 = SortKey(8바이트), 2번 비트 = AABB(24바이트))
    // 이 예시를 위해 ECS.cpp 혹은 PluginAPI.cpp 등에서 이 컴포넌트들을 RegisterComponent 했다고 가정합니다.
    ComponentMask mask;
    mask.low = 0b111; // 0, 1, 2 비트 활성화

    for (uint32_t i = 0; i < objectCount; i++) {
        // Transform 행렬 읽기
        float matrix[16];
        file.read(reinterpret_cast<char*>(matrix), sizeof(float) * 16);

        // AABB 읽기
        AABB bounds;
        file.read(reinterpret_cast<char*>(&bounds.minBounds), sizeof(float) * 3);
        file.read(reinterpret_cast<char*>(&bounds.maxBounds), sizeof(float) * 3);
        
        // Mesh ID, Material ID 읽기
        int32_t meshId, matId;
        file.read(reinterpret_cast<char*>(&meshId), sizeof(int32_t));
        file.read(reinterpret_cast<char*>(&matId), sizeof(int32_t));

        outAABBs.push_back(bounds);

        VulkanBackend::InstanceData inst;
        for (int k = 0; k < 16; ++k) inst.mvpMatrix[k] = matrix[k];
        inst.sortKey.materialID = static_cast<uint16_t>(matId);
        inst.sortKey.pipelineID = static_cast<uint16_t>(meshId); // meshId를 임시로 pipelineID에 저장
        inst.sortKey.depth = 0;
        outInstances.push_back(inst);

        // 실제 ECS에 생성
        Entity ent = ecsManager.CreateEntity(mask);
        
        // --- (실제로는 여기서 ECS의 GetComponentArray를 호출하여 데이터를 삽입해야 함) ---
        // 예: float* tfArray = ecsManager.GetComponentArray<float>(chunk, 0);
        // memcpy(tfArray + index*16, matrix, sizeof(float)*16);
    }

    std::cout << "[SceneLoader] Scene loaded successfully.\n";
    return true;
}

} // namespace Endfield

