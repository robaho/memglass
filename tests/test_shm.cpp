#include <gtest/gtest.h>
#include <memglass/detail/shm.hpp>
#include <cstring>

using namespace memglass::detail;

class SharedMemoryTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Cleanup any leftover shared memory
        SharedMemory cleanup;
        cleanup.open("/memglass_test_shm");
        cleanup.close();
    }
};

TEST_F(SharedMemoryTest, CreateAndOpen) {
    const char* name = "/memglass_test_shm";
    const size_t size = 4096;

    // Create shared memory
    SharedMemory creator;
    ASSERT_TRUE(creator.create(name, size));
    EXPECT_EQ(creator.size(), size);
    EXPECT_TRUE(creator.is_owner());
    EXPECT_NE(creator.data(), nullptr);

    // Write some data
    std::memset(creator.data(), 0xAB, size);

    // Open from another handle
    SharedMemory opener;
    ASSERT_TRUE(opener.open(name));
    EXPECT_EQ(opener.size(), size);
    EXPECT_FALSE(opener.is_owner());
    EXPECT_NE(opener.data(), nullptr);

    // Verify data
    const auto* data = static_cast<const uint8_t*>(opener.data());
    EXPECT_EQ(data[0], 0xAB);
    EXPECT_EQ(data[size - 1], 0xAB);

    // Close opener first
    opener.close();
    EXPECT_EQ(opener.data(), nullptr);

    // Creator still works
    EXPECT_NE(creator.data(), nullptr);

    // Close creator (will unlink)
    creator.close();
}

TEST_F(SharedMemoryTest, MoveSemantics) {
    SharedMemory shm1;
    ASSERT_TRUE(shm1.create("/memglass_test_shm", 1024));

    void* original_data = shm1.data();

    // Move construct
    SharedMemory shm2(std::move(shm1));
    EXPECT_EQ(shm1.data(), nullptr);
    EXPECT_EQ(shm2.data(), original_data);

    // Move assign
    SharedMemory shm3;
    shm3 = std::move(shm2);
    EXPECT_EQ(shm2.data(), nullptr);
    EXPECT_EQ(shm3.data(), original_data);
}

TEST_F(SharedMemoryTest, ShmNaming) {
    EXPECT_EQ(make_header_shm_name("test"), "/memglass_test_header");
    EXPECT_EQ(make_region_shm_name("test", 1), "/memglass_test_region_0001");
    EXPECT_EQ(make_region_shm_name("test", 42), "/memglass_test_region_0042");
}

TEST_F(SharedMemoryTest, OpenNonexistent) {
    SharedMemory shm;
    EXPECT_FALSE(shm.open("/memglass_nonexistent_shm"));
}
