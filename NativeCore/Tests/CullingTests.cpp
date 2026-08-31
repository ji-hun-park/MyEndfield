#include <gtest/gtest.h>
#include "Culling.h"

using namespace Endfield;

TEST(CullingTests, FrustumExtractAndIntersect) {
    Frustum frustum;
    // Simple Ortho projection matrix equivalent for viewing bounds [-1, 1] in all axis
    float orthoVP[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    frustum.ExtractFromMatrix(orthoVP);

    // AABB inside
    AABB insideAABB;
    insideAABB.minBounds[0] = -0.5f; insideAABB.minBounds[1] = -0.5f; insideAABB.minBounds[2] = 0.1f;
    insideAABB.maxBounds[0] =  0.5f; insideAABB.maxBounds[1] =  0.5f; insideAABB.maxBounds[2] = 0.9f;

    EXPECT_TRUE(frustum.Intersects(insideAABB));

    // AABB outside (too far right)
    AABB outsideAABB;
    outsideAABB.minBounds[0] =  1.5f; outsideAABB.minBounds[1] = -0.5f; outsideAABB.minBounds[2] = 0.1f;
    outsideAABB.maxBounds[0] =  2.5f; outsideAABB.maxBounds[1] =  0.5f; outsideAABB.maxBounds[2] = 0.9f;

    EXPECT_FALSE(frustum.Intersects(outsideAABB));
}

TEST(CullingTests, ParallelFrustumCulling) {
    CullingSystem cullingSys;
    cullingSys.Initialize(4);

    Frustum frustum;
    float orthoVP[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    frustum.ExtractFromMatrix(orthoVP);

    std::vector<AABB> aabbs;
    for (int i = 0; i < 100; ++i) {
        AABB box;
        box.minBounds[0] = (i % 2 == 0) ? -0.5f : 1.5f; // Even inside, Odd outside
        box.minBounds[1] = -0.5f; box.minBounds[2] = 0.1f;
        box.maxBounds[0] = box.minBounds[0] + 1.0f;
        box.maxBounds[1] = 0.5f; box.maxBounds[2] = 0.9f;
        aabbs.push_back(box);
    }

    std::vector<bool> visibility;
    cullingSys.PerformFrustumCullingParallel(frustum, aabbs, visibility);

    EXPECT_EQ(visibility.size(), 100);
    for (int i = 0; i < 100; ++i) {
        if (i % 2 == 0) {
            EXPECT_TRUE(visibility[i]);
        } else {
            EXPECT_FALSE(visibility[i]);
        }
    }
    
    cullingSys.Shutdown();
}

