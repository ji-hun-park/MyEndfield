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

    struct TemplateData {
        TransformComponent transform;
        BoundsComponent bounds;
        MeshComponent mesh;
    };

    // 바이너리 파일 경로를 받아서 메쉬와 템플릿 인스턴스 데이터를 로드합니다 (지연 스폰용).
    static bool LoadScene(const std::string& filePath, ECSManager& ecsManager, std::vector<MeshData>& outMeshes, std::vector<TemplateData>& outTemplates);
};

} // namespace Endfield

