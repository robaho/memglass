#include <gtest/gtest.h>
#include <memglass/registry.hpp>

using namespace memglass;

class RegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        registry::clear();
    }

    void TearDown() override {
        registry::clear();
    }
};

TEST_F(RegistryTest, RegisterType) {
    TypeDescriptor desc;
    desc.name = "TestType";
    desc.size = 16;
    desc.alignment = 8;
    desc.fields = {
        {"field1", 0, 4, PrimitiveType::Int32, 0, 0, Atomicity::None, false},
        {"field2", 8, 8, PrimitiveType::Int64, 0, 0, Atomicity::None, false},
    };

    uint32_t type_id = registry::register_type(desc);
    EXPECT_NE(type_id, 0u);
    EXPECT_GE(type_id, static_cast<uint32_t>(PrimitiveType::UserTypeBase));

    // Get by ID
    const TypeDescriptor* retrieved = registry::get_type(type_id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "TestType");
    EXPECT_EQ(retrieved->size, 16u);
    EXPECT_EQ(retrieved->fields.size(), 2u);
}

TEST_F(RegistryTest, GetTypeByName) {
    TypeDescriptor desc;
    desc.name = "MyType";
    desc.size = 4;
    desc.alignment = 4;

    uint32_t type_id = registry::register_type(desc);
    EXPECT_EQ(registry::get_type_id("MyType"), type_id);
    EXPECT_EQ(registry::get_type_id("NonexistentType"), 0u);
}

TEST_F(RegistryTest, RegisterDuplicate) {
    TypeDescriptor desc;
    desc.name = "DuplicateType";
    desc.size = 8;
    desc.alignment = 8;

    uint32_t id1 = registry::register_type(desc);
    uint32_t id2 = registry::register_type(desc);

    // Should return same ID for duplicate registration
    EXPECT_EQ(id1, id2);
}

TEST_F(RegistryTest, MultipleTypes) {
    for (int i = 0; i < 10; ++i) {
        TypeDescriptor desc;
        desc.name = "Type" + std::to_string(i);
        desc.size = 8;
        desc.alignment = 8;
        registry::register_type(desc);
    }

    const auto& all_types = registry::get_all_types();
    EXPECT_EQ(all_types.size(), 10u);
}

TEST_F(RegistryTest, PrimitiveTypeMapping) {
    EXPECT_EQ(primitive_type_of<bool>(), PrimitiveType::Bool);
    EXPECT_EQ(primitive_type_of<int8_t>(), PrimitiveType::Int8);
    EXPECT_EQ(primitive_type_of<uint8_t>(), PrimitiveType::UInt8);
    EXPECT_EQ(primitive_type_of<int16_t>(), PrimitiveType::Int16);
    EXPECT_EQ(primitive_type_of<uint16_t>(), PrimitiveType::UInt16);
    EXPECT_EQ(primitive_type_of<int32_t>(), PrimitiveType::Int32);
    EXPECT_EQ(primitive_type_of<uint32_t>(), PrimitiveType::UInt32);
    EXPECT_EQ(primitive_type_of<int64_t>(), PrimitiveType::Int64);
    EXPECT_EQ(primitive_type_of<uint64_t>(), PrimitiveType::UInt64);
    EXPECT_EQ(primitive_type_of<float>(), PrimitiveType::Float32);
    EXPECT_EQ(primitive_type_of<double>(), PrimitiveType::Float64);
}
