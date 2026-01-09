#include "memglass/registry.hpp"
#include "memglass/types.hpp"

#include <cstring>
#include <map>
#include <mutex>
#include <string>

namespace memglass::registry {

namespace {

std::mutex g_mutex;
std::vector<std::pair<uint32_t, TypeDescriptor>> g_types;
std::map<std::string, uint32_t> g_name_to_id;
uint32_t g_next_type_id = static_cast<uint32_t>(PrimitiveType::UserTypeBase);

// Simple hash function for type names
uint32_t hash_name(std::string_view name) {
    uint32_t hash = 5381;
    for (char c : name) {
        hash = ((hash << 5) + hash) + static_cast<uint32_t>(c);
    }
    return hash | static_cast<uint32_t>(PrimitiveType::UserTypeBase);
}

} // anonymous namespace

uint32_t register_type(const TypeDescriptor& desc) {
    std::lock_guard<std::mutex> lock(g_mutex);

    std::string name_str(desc.name);

    // Check if already registered
    auto it = g_name_to_id.find(name_str);
    if (it != g_name_to_id.end()) {
        return it->second;
    }

    // Generate type ID
    uint32_t type_id = hash_name(desc.name);

    // Ensure uniqueness
    while (true) {
        bool found = false;
        for (const auto& [id, _] : g_types) {
            if (id == type_id) {
                found = true;
                type_id++;
                break;
            }
        }
        if (!found) break;
    }

    g_types.emplace_back(type_id, desc);
    g_name_to_id[name_str] = type_id;

    return type_id;
}

void register_type_alias(std::string_view alias, uint32_t type_id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_name_to_id[std::string(alias)] = type_id;
}

uint32_t get_type_id(std::string_view name) {
    std::lock_guard<std::mutex> lock(g_mutex);

    std::string name_str(name);
    auto it = g_name_to_id.find(name_str);
    if (it != g_name_to_id.end()) {
        return it->second;
    }
    return 0;
}

const TypeDescriptor* get_type(uint32_t type_id) {
    std::lock_guard<std::mutex> lock(g_mutex);

    for (const auto& [id, desc] : g_types) {
        if (id == type_id) {
            return &desc;
        }
    }
    return nullptr;
}

const std::vector<std::pair<uint32_t, TypeDescriptor>>& get_all_types() {
    return g_types;
}

void clear() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_types.clear();
    g_name_to_id.clear();
}

void write_to_header(TelemetryHeader* header, void* base) {
    std::lock_guard<std::mutex> lock(g_mutex);

    auto* type_entries = reinterpret_cast<TypeEntry*>(
        static_cast<char*>(base) + header->type_registry_offset);
    auto* field_entries = reinterpret_cast<FieldEntry*>(
        static_cast<char*>(base) + header->field_entries_offset);

    uint32_t type_count = 0;
    uint32_t field_count = 0;

    for (const auto& [type_id, desc] : g_types) {
        if (type_count >= header->type_registry_capacity) break;

        TypeEntry& entry = type_entries[type_count];
        entry.type_id = type_id;
        entry.size = desc.size;
        entry.alignment = desc.alignment;
        entry.field_count = static_cast<uint32_t>(desc.fields.size());
        entry.fields_offset = header->field_entries_offset +
                              field_count * sizeof(FieldEntry);
        entry.set_name(desc.name);

        // Write fields
        for (const auto& field_desc : desc.fields) {
            if (field_count >= header->field_entries_capacity) break;

            FieldEntry& field = field_entries[field_count];
            field.offset = field_desc.offset;
            field.size = field_desc.size;
            field.type_id = (field_desc.primitive_type != PrimitiveType::Unknown)
                ? static_cast<uint32_t>(field_desc.primitive_type)
                : field_desc.user_type_id;
            field.flags = field_desc.readonly ? static_cast<uint32_t>(FieldFlags::ReadOnly) : 0;
            if (field_desc.array_size > 0) {
                field.flags |= static_cast<uint32_t>(FieldFlags::IsArray);
            }
            field.array_size = field_desc.array_size;
            field.atomicity = field_desc.atomicity;
            field.set_name(field_desc.name);

            field_count++;
        }

        type_count++;
    }

    header->type_count.store(type_count, std::memory_order_release);
    header->field_count.store(field_count, std::memory_order_release);
}

} // namespace memglass::registry
