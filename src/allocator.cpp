#include "memglass/allocator.hpp"
#include "memglass/memglass.hpp"

#include <cstring>

namespace memglass {

// RegionManager implementation

RegionManager::RegionManager(Context& ctx)
    : ctx_(ctx)
    , current_region_size_(ctx.config().initial_region_size)
{
}

RegionManager::~RegionManager() = default;

bool RegionManager::init(std::string_view session_name, size_t initial_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    session_name_ = std::string(session_name);
    current_region_size_ = initial_size;

    // Create first region
    Region* region = create_region(initial_size);
    if (!region) {
        return false;
    }

    // Update header with first region ID
    ctx_.header()->first_region_id.store(region->id, std::memory_order_release);

    return true;
}

RegionManager::Region* RegionManager::create_region(size_t size) {
    auto region = std::make_unique<Region>();
    region->id = next_region_id_++;

    std::string shm_name = detail::make_region_shm_name(session_name_, region->id);

    // Size includes RegionDescriptor at the start
    size_t total_size = sizeof(RegionDescriptor) + size;

    if (!region->shm.create(shm_name, total_size)) {
        return nullptr;
    }

    // Initialize descriptor
    region->descriptor = static_cast<RegionDescriptor*>(region->shm.data());
    region->descriptor->magic = REGION_MAGIC;
    region->descriptor->region_id = region->id;
    region->descriptor->size = total_size;
    region->descriptor->used.store(sizeof(RegionDescriptor), std::memory_order_release);
    region->descriptor->next_region_id.store(0, std::memory_order_release);
    region->descriptor->set_shm_name(shm_name);

    // Link to previous region if exists
    if (!regions_.empty()) {
        regions_.back()->descriptor->next_region_id.store(
            region->id, std::memory_order_release);
    }

    Region* ptr = region.get();
    regions_.push_back(std::move(region));
    return ptr;
}

RegionManager::Region* RegionManager::current_region() {
    if (regions_.empty()) return nullptr;
    return regions_.back().get();
}

void* RegionManager::allocate(size_t size, size_t alignment) {
    std::lock_guard<std::mutex> lock(mutex_);

    Region* region = current_region();
    if (!region) return nullptr;

    // Align the current position
    uint64_t current = region->descriptor->used.load(std::memory_order_acquire);
    uint64_t aligned = (current + alignment - 1) & ~(alignment - 1);
    uint64_t new_used = aligned + size;

    // Check if fits in current region
    if (new_used > region->descriptor->size) {
        // Need new region
        size_t new_size = std::max(size + sizeof(RegionDescriptor),
                                   current_region_size_ * 2);
        new_size = std::min(new_size, ctx_.config().max_region_size);
        current_region_size_ = new_size;

        region = create_region(new_size);
        if (!region) return nullptr;

        // Update header sequence
        ctx_.header()->sequence.fetch_add(1, std::memory_order_release);

        // Recalculate
        current = region->descriptor->used.load(std::memory_order_acquire);
        aligned = (current + alignment - 1) & ~(alignment - 1);
        new_used = aligned + size;
    }

    // Allocate
    region->descriptor->used.store(new_used, std::memory_order_release);

    return static_cast<char*>(region->shm.data()) + aligned;
}

void* RegionManager::get_region_data(uint64_t region_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& region : regions_) {
        if (region->id == region_id) {
            return region->shm.data();
        }
    }
    return nullptr;
}

bool RegionManager::get_location(const void* ptr, uint64_t& region_id, uint64_t& offset) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& region : regions_) {
        const char* base = static_cast<const char*>(region->shm.data());
        const char* end = base + region->descriptor->size;
        const char* p = static_cast<const char*>(ptr);

        if (p >= base && p < end) {
            region_id = region->id;
            offset = static_cast<uint64_t>(p - base);
            return true;
        }
    }
    return false;
}

// ObjectManager implementation

ObjectManager::ObjectManager(Context& ctx)
    : ctx_(ctx)
{
}

ObjectEntry* ObjectManager::register_object(void* ptr, uint32_t type_id, std::string_view label) {
    std::lock_guard<std::mutex> lock(mutex_);

    TelemetryHeader* header = ctx_.header();
    uint32_t count = header->object_count.load(std::memory_order_acquire);

    if (count >= header->object_dir_capacity) {
        return nullptr;  // Directory full
    }

    // Get location
    uint64_t region_id, offset;
    if (!ctx_.regions().get_location(ptr, region_id, offset)) {
        return nullptr;
    }

    // Get entry slot
    auto* entries = reinterpret_cast<ObjectEntry*>(
        static_cast<char*>(ctx_.header_shm().data()) + header->object_dir_offset);
    ObjectEntry* entry = &entries[count];

    // Initialize entry
    entry->state.store(static_cast<uint32_t>(ObjectState::Alive), std::memory_order_release);
    entry->type_id = type_id;
    entry->region_id = region_id;
    entry->offset = offset;
    entry->generation = 1;
    entry->set_label(label);

    // Update count
    header->object_count.store(count + 1, std::memory_order_release);
    header->sequence.fetch_add(1, std::memory_order_release);

    ptr_to_entry_[ptr] = entry;

    return entry;
}

void ObjectManager::destroy_object(void* ptr) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = ptr_to_entry_.find(ptr);
    if (it != ptr_to_entry_.end()) {
        it->second->state.store(static_cast<uint32_t>(ObjectState::Destroyed),
                                std::memory_order_release);
        ctx_.header()->sequence.fetch_add(1, std::memory_order_release);
        ptr_to_entry_.erase(it);
    }
}

ObjectEntry* ObjectManager::find_object(std::string_view label) {
    std::lock_guard<std::mutex> lock(mutex_);

    TelemetryHeader* header = ctx_.header();
    uint32_t count = header->object_count.load(std::memory_order_acquire);

    auto* entries = reinterpret_cast<ObjectEntry*>(
        static_cast<char*>(ctx_.header_shm().data()) + header->object_dir_offset);

    for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].state.load(std::memory_order_acquire) ==
                static_cast<uint32_t>(ObjectState::Alive) &&
            std::string_view(entries[i].label) == label) {
            return &entries[i];
        }
    }
    return nullptr;
}

std::vector<ObjectEntry*> ObjectManager::get_all_objects() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<ObjectEntry*> result;
    TelemetryHeader* header = ctx_.header();
    uint32_t count = header->object_count.load(std::memory_order_acquire);

    auto* entries = reinterpret_cast<ObjectEntry*>(
        static_cast<char*>(ctx_.header_shm().data()) + header->object_dir_offset);

    for (uint32_t i = 0; i < count; ++i) {
        if (entries[i].state.load(std::memory_order_acquire) ==
                static_cast<uint32_t>(ObjectState::Alive)) {
            result.push_back(&entries[i]);
        }
    }
    return result;
}

} // namespace memglass
