#include <gtest/gtest.h>
#include <memglass/memglass.hpp>
#include <cstring>

using namespace memglass;

class AllocatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry::clear();
        ASSERT_TRUE(memglass::init("test_allocator"));
    }

    void TearDown() override {
        memglass::shutdown();
        registry::clear();
    }
};

TEST_F(AllocatorTest, BasicAllocation) {
    auto* ctx = detail::get_context();
    ASSERT_NE(ctx, nullptr);

    void* ptr1 = ctx->regions().allocate(100, 8);
    ASSERT_NE(ptr1, nullptr);

    void* ptr2 = ctx->regions().allocate(200, 16);
    ASSERT_NE(ptr2, nullptr);

    // Pointers should be different
    EXPECT_NE(ptr1, ptr2);

    // Second pointer should be aligned
    EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr2) % 16, 0u);
}

TEST_F(AllocatorTest, GetLocation) {
    auto* ctx = detail::get_context();
    ASSERT_NE(ctx, nullptr);

    void* ptr = ctx->regions().allocate(64, 8);
    ASSERT_NE(ptr, nullptr);

    uint64_t region_id, offset;
    ASSERT_TRUE(ctx->regions().get_location(ptr, region_id, offset));

    EXPECT_GT(region_id, 0u);
    EXPECT_GT(offset, 0u);

    // Get region data and verify offset matches
    void* region_data = ctx->regions().get_region_data(region_id);
    ASSERT_NE(region_data, nullptr);

    void* calculated_ptr = static_cast<char*>(region_data) + offset;
    EXPECT_EQ(calculated_ptr, ptr);
}

TEST_F(AllocatorTest, MultipleAllocations) {
    auto* ctx = detail::get_context();
    ASSERT_NE(ctx, nullptr);

    std::vector<void*> ptrs;
    for (int i = 0; i < 100; ++i) {
        void* ptr = ctx->regions().allocate(1024, 8);
        ASSERT_NE(ptr, nullptr);
        ptrs.push_back(ptr);
    }

    // All pointers should be unique
    std::set<void*> unique_ptrs(ptrs.begin(), ptrs.end());
    EXPECT_EQ(unique_ptrs.size(), ptrs.size());
}

TEST_F(AllocatorTest, LargeAllocation) {
    auto* ctx = detail::get_context();
    ASSERT_NE(ctx, nullptr);

    // Allocate something larger than initial region
    size_t large_size = 2 * 1024 * 1024;  // 2 MB
    void* ptr = ctx->regions().allocate(large_size, 64);
    ASSERT_NE(ptr, nullptr);

    // Should be able to write to it
    std::memset(ptr, 0xAB, large_size);
}
