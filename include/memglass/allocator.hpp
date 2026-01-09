#pragma once

#include "types.hpp"
#include "detail/shm.hpp"
#include <memory>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace memglass {

// Forward declarations
class Context;

// Region manager - handles allocation across shared memory regions
class RegionManager {
public:
    explicit RegionManager(Context& ctx);
    ~RegionManager();

    // Initialize with first region
    bool init(std::string_view session_name, size_t initial_size);

    // Allocate memory from regions
    void* allocate(size_t size, size_t alignment);

    // Get region by ID
    void* get_region_data(uint64_t region_id);

    // Get offset within region for a pointer
    bool get_location(const void* ptr, uint64_t& region_id, uint64_t& offset);

private:
    struct Region {
        detail::SharedMemory shm;
        uint64_t id;
        RegionDescriptor* descriptor;
    };

    Context& ctx_;
    std::string session_name_;
    std::vector<std::unique_ptr<Region>> regions_;
    std::mutex mutex_;
    uint64_t next_region_id_ = 1;
    size_t current_region_size_;

    Region* create_region(size_t size);
    Region* current_region();
};

// Object manager - tracks object lifecycle
class ObjectManager {
public:
    explicit ObjectManager(Context& ctx);

    // Register an object in the directory
    ObjectEntry* register_object(void* ptr, uint32_t type_id, std::string_view label);

    // Mark object as destroyed
    void destroy_object(void* ptr);

    // Find object by label
    ObjectEntry* find_object(std::string_view label);

    // Get all objects
    std::vector<ObjectEntry*> get_all_objects();

private:
    Context& ctx_;
    std::unordered_map<void*, ObjectEntry*> ptr_to_entry_;
    std::mutex mutex_;
};

} // namespace memglass
