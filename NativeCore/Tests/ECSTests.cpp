#include <gtest/gtest.h>
#include "ECS.h"

using namespace Endfield;

// Test Component Mask bits
TEST(ECSTests, ComponentMaskContains) {
    ComponentMask mask1;
    mask1.low = 0b011; // Has component 0, 1

    ComponentMask mask2;
    mask2.low = 0b001; // Has component 0

    EXPECT_TRUE(mask1.Contains(mask2));
    EXPECT_FALSE(mask2.Contains(mask1));
}

TEST(ECSTests, CreateAndDestroyEntity) {
    ECSManager manager;
    ComponentMask mask;
    mask.low = 0b001;

    Entity ent1 = manager.CreateEntity(mask);
    EXPECT_EQ(ent1.id, 0u);

    Entity ent2 = manager.CreateEntity(mask);
    EXPECT_EQ(ent2.id, 1u);

    std::vector<Chunk*> chunks = manager.QueryChunks(mask);
    ASSERT_FALSE(chunks.empty());
    Chunk* firstChunk = chunks.front();
    EXPECT_EQ(firstChunk->entityCount, 2u);

    manager.DestroyEntity(ent1);
    EXPECT_EQ(firstChunk->entityCount, 1u);
    
    // Test if the last entity was swapped
    EXPECT_EQ(firstChunk->entityArray[0].id, 1u);
}
