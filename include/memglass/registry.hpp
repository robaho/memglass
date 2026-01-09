#pragma once

#include "types.hpp"
#include <functional>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace memglass {

// Forward declarations
class Context;

// Runtime type descriptor (used by generated code)
struct FieldDescriptor {
    std::string_view name;
    uint32_t offset;
    uint32_t size;
    PrimitiveType primitive_type;
    uint32_t user_type_id;  // For nested types
    uint32_t array_size;    // 0 = not array
    Atomicity atomicity;
    bool readonly;
};

struct TypeDescriptor {
    std::string_view name;
    uint32_t size;
    uint32_t alignment;
    std::vector<FieldDescriptor> fields;
};

// Type registry - manages type registration
namespace registry {

// Register a type descriptor (called by generated code)
uint32_t register_type(const TypeDescriptor& desc);

// Register a type alias (maps typeid name to type_id)
void register_type_alias(std::string_view alias, uint32_t type_id);

// Get type ID by name (checks both names and aliases)
uint32_t get_type_id(std::string_view name);

// Get type descriptor by ID
const TypeDescriptor* get_type(uint32_t type_id);

// Get all registered types
const std::vector<std::pair<uint32_t, TypeDescriptor>>& get_all_types();

// Clear registry (for testing)
void clear();

// Write types to shared memory header
void write_to_header(TelemetryHeader* header, void* base);

// Template helper to register a type with typeid mapping
// This allows create<T>() to find types by their C++ typeid name
template<typename T>
uint32_t register_type_for(const TypeDescriptor& desc) {
    uint32_t type_id = register_type(desc);
    // Also register typeid name as an alias so create<T>() can find it
    register_type_alias(typeid(T).name(), type_id);
    return type_id;
}

} // namespace registry

// Helper to map C++ types to PrimitiveType
template<typename T>
constexpr PrimitiveType primitive_type_of() {
    if constexpr (std::is_same_v<T, bool>) return PrimitiveType::Bool;
    else if constexpr (std::is_same_v<T, int8_t>) return PrimitiveType::Int8;
    else if constexpr (std::is_same_v<T, uint8_t>) return PrimitiveType::UInt8;
    else if constexpr (std::is_same_v<T, int16_t>) return PrimitiveType::Int16;
    else if constexpr (std::is_same_v<T, uint16_t>) return PrimitiveType::UInt16;
    else if constexpr (std::is_same_v<T, int32_t>) return PrimitiveType::Int32;
    else if constexpr (std::is_same_v<T, uint32_t>) return PrimitiveType::UInt32;
    else if constexpr (std::is_same_v<T, int64_t>) return PrimitiveType::Int64;
    else if constexpr (std::is_same_v<T, uint64_t>) return PrimitiveType::UInt64;
    else if constexpr (std::is_same_v<T, float>) return PrimitiveType::Float32;
    else if constexpr (std::is_same_v<T, double>) return PrimitiveType::Float64;
    else if constexpr (std::is_same_v<T, char>) return PrimitiveType::Char;
    else return PrimitiveType::Unknown;
}

} // namespace memglass
