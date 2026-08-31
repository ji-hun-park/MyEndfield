#pragma once
#include <string>
#include <vector>
#include "ECS.h"
#include "Culling.h"
#include "SortKey.h"
#include "VulkanBackend.h"

namespace Endfield {

class SceneLoader {
public:
    struct Vertex {
        float posX, posY, posZ;
        float normX, normY, normZ;
        float uvX, uvY;
    };

    struct SubMeshData {
        std::vector<int32_t> indices;
    };

    struct MeshData {
        std::vector<Vertex> vertices;
        std::vector<SubMeshData> subMeshes;
    };

    // 바이너리 파일 경로를 받아서 ECS(SoA Chunk)에 엔티티들을 등록합니다.
    static bool LoadScene(const std::string& filePath, ECSManager& ecsManager, std::vector<MeshData>& outMeshes);
};

} // namespace Endfield

