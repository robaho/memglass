#pragma once

#include <clang-c/Index.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <map>

namespace memglass::gen {

// Field metadata parsed from comments
struct FieldMeta {
    bool readonly = false;
    bool has_range = false;
    double range_min = 0;
    double range_max = 0;
    double step = 0;
    std::string regex_pattern;
    std::string format;
    std::string unit;
    std::string desc;
    std::vector<std::pair<std::string, int64_t>> enum_values;
    std::vector<std::pair<std::string, uint64_t>> flags;

    // Atomicity
    enum class Atomicity { None, Atomic, Seqlock, Locked };
    Atomicity atomicity = Atomicity::None;
};

// Field information
struct FieldInfo {
    std::string name;
    std::string type_name;
    uint32_t offset = 0;
    uint32_t size = 0;
    bool is_array = false;
    uint32_t array_size = 0;
    bool is_nested = false;
    std::string nested_type_name;
    FieldMeta meta;
};

// Type information
struct TypeInfo {
    std::string name;
    std::string qualified_name;
    uint32_t size = 0;
    uint32_t alignment = 0;
    std::vector<FieldInfo> fields;
};

// Generator class
class Generator {
public:
    Generator();
    ~Generator();

    // Parse a source file
    bool parse(const std::string& filename, const std::vector<std::string>& args);

    // Get discovered types
    const std::vector<TypeInfo>& types() const { return types_; }

    // Generate output header
    std::string generate_header() const;

    // Verbose mode
    void set_verbose(bool v) { verbose_ = v; }

private:
    CXIndex index_ = nullptr;
    std::vector<TypeInfo> types_;
    bool verbose_ = false;

    // Visitor callback
    static CXChildVisitResult visit_cursor(CXCursor cursor, CXCursor parent, CXClientData data);

    // Check if cursor has memglass::observe attribute
    bool has_observe_attribute(CXCursor cursor);

    // Extract type info from struct cursor
    TypeInfo extract_type_info(CXCursor cursor);

    // Extract field info from field cursor
    FieldInfo extract_field_info(CXCursor cursor, CXCursor parent);

    // Parse comment for metadata
    FieldMeta parse_comment(CXCursor cursor);

    // Map clang type to primitive type name
    std::string map_primitive_type(CXType type);
};

} // namespace memglass::gen
