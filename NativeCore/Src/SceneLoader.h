#pragma once
#include <string>
#include <vector>
#include "ECS.h"
#include "Culling.h"
#include "SortKey.h"

namespace Endfield {

class SceneLoader {
public:
    // 바이너리 파일 경로를 받아서 ECS에 엔티티들을 등록합니다.
    static bool LoadScene(const std::string& filePath, ECSManager& ecsManager, std::vector<AABB>& outAABBs);
};

} // namespace Endfield

