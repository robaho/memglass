# API Reference

Complete reference for the memglass C++ API.

## Producer API

### Initialization

```cpp
#include <memglass/memglass.hpp>
```

#### `memglass::init`

```cpp
bool init(std::string_view session_name);
```

Initialize memglass with a session name. Creates the header shared memory region.

**Parameters:**
- `session_name` - Unique identifier for this session (max 63 characters)

**Returns:** `true` on success, `false` if initialization fails

**Example:**
```cpp
if (!memglass::init("my_application")) {
    std::cerr << "Failed to initialize memglass\n";
    return 1;
}
```

---

#### `memglass::shutdown`

```cpp
void shutdown();
```

Cleanup memglass resources. Destroys all objects and unlinks shared memory.

**Example:**
```cpp
memglass::shutdown();
```

---

### Object Creation

#### `memglass::create<T>`

```cpp
template<typename T>
T* create(std::string_view label);

template<typename T>
T* create(std::string_view label, const T& initial_value);
```

Create an object in shared memory.

**Template Parameters:**
- `T` - POD type (must be trivially copyable)

**Parameters:**
- `label` - Unique label for this object (max 63 characters)
- `initial_value` - Optional initial value

**Returns:** Pointer to the allocated object, or `nullptr` on failure

**Example:**
```cpp
auto* counter = memglass::create<Counter>("main_counter");
counter->value = 0;

auto* preset = memglass::create<Settings>("defaults", Settings{.volume = 50});
```

---

#### `memglass::destroy`

```cpp
template<typename T>
void destroy(T* ptr);
```

Mark an object as destroyed. The memory is not immediately freed.

**Example:**
```cpp
memglass::destroy(counter);
```

---

### Type Registration

#### `memglass::registry::register_type_for<T>`

```cpp
template<typename T>
uint32_t register_type_for(const TypeDescriptor& desc);
```

Register a type with the given descriptor. Usually called by generated code.

**Returns:** Type ID

---

## Observer API

### Observer Class

```cpp
#include <memglass/observer.hpp>
```

#### Constructor

```cpp
explicit Observer(std::string_view session_name);
```

Create an observer for the given session.

---

#### `connect`

```cpp
bool connect();
```

Connect to the shared memory session.

**Returns:** `true` on success, `false` if session doesn't exist or is invalid

---

#### `disconnect`

```cpp
void disconnect();
```

Disconnect from the session, releasing mapped memory.

---

#### `is_connected`

```cpp
bool is_connected() const;
```

Check if currently connected.

---

#### `refresh`

```cpp
void refresh();
```

Reload types and regions if the sequence number has changed.

---

#### `sequence`

```cpp
uint64_t sequence() const;
```

Get the current sequence number. Changes on structural modifications.

---

#### `producer_pid`

```cpp
uint64_t producer_pid() const;
```

Get the producer process ID.

---

#### `types`

```cpp
const std::vector<ObservedType>& types() const;
```

Get all registered types.

---

#### `objects`

```cpp
std::vector<ObservedObject> objects() const;
```

Get all live objects.

---

#### `find`

```cpp
ObjectView find(std::string_view label);
```

Find an object by label.

**Returns:** Valid `ObjectView` if found, invalid if not

---

#### `get`

```cpp
ObjectView get(const ObservedObject& obj);
```

Get a view for an object info struct.

---

### ObservedType Struct

```cpp
struct ObservedType {
    uint32_t type_id;
    std::string name;
    uint32_t size;
    uint32_t alignment;
    std::vector<FieldEntry> fields;
};
```

---

### ObservedObject Struct

```cpp
struct ObservedObject {
    std::string label;
    std::string type_name;
    uint32_t type_id;
    uint64_t region_id;
    uint64_t offset;
    uint64_t generation;
    ObjectState state;
};
```

---

### ObjectView Class

Provides access to an object's fields.

#### Validity Check

```cpp
explicit operator bool() const;
```

Check if the view is valid.

**Example:**
```cpp
auto view = obs.find("my_object");
if (view) {
    // Object exists
}
```

---

#### Field Access

```cpp
FieldProxy operator[](std::string_view field_name);
FieldProxy operator[](const char* field_name);
```

Access a field by name. Supports dot notation for nested fields.

**Example:**
```cpp
int64_t value = view["counter"];
int64_t nested = view["quote.bid_price"];
```

---

#### `as<T>`

```cpp
template<typename T>
T as() const;
```

Read the entire object as type T.

---

#### `data`

```cpp
const void* data() const;
void* mutable_data();
```

Raw pointer access to object data.

---

### FieldProxy Class

Represents a field reference with type-aware access.

#### Implicit Conversion

```cpp
template<typename T>
operator T() const;
```

Implicitly convert to the field's type.

**Example:**
```cpp
int64_t value = view["field"];  // Implicit conversion
```

---

#### `as<T>`

```cpp
template<typename T>
T as() const;
```

Explicit type conversion.

---

#### `read<T>`

```cpp
template<typename T>
T read() const;
```

Explicit read with atomicity handling.

---

#### `try_get<T>`

```cpp
template<typename T>
std::optional<T> try_get() const;
```

Non-blocking read for seqlock fields. Returns `nullopt` if write in progress.

---

#### `unsafe<T>`

```cpp
template<typename T>
T unsafe() const;
```

Direct read bypassing atomicity (for debugging).

---

#### `info`

```cpp
const FieldEntry* info() const;
```

Get field metadata.

---

#### Validity Check

```cpp
explicit operator bool() const;
```

Check if field reference is valid.

---

#### Nested Access

```cpp
FieldProxy operator[](std::string_view name) const;
FieldProxy operator[](size_t index) const;
```

Access nested fields or array elements.

---

## Types

### PrimitiveType

```cpp
enum class PrimitiveType : uint32_t {
    Unknown = 0,
    Bool = 1,
    Int8 = 2,
    UInt8 = 3,
    Int16 = 4,
    UInt16 = 5,
    Int32 = 6,
    UInt32 = 7,
    Int64 = 8,
    UInt64 = 9,
    Float32 = 10,
    Float64 = 11,
    Char = 12,
};
```

---

### Atomicity

```cpp
enum class Atomicity : uint8_t {
    None = 0,     // Direct read/write
    Atomic = 1,   // std::atomic<T>
    Seqlock = 2,  // Guarded<T>
    Locked = 3,   // Locked<T>
};
```

---

### ObjectState

```cpp
enum class ObjectState : uint32_t {
    Free = 0,
    Alive = 1,
    Destroyed = 2,
};
```

---

### FieldEntry

```cpp
struct FieldEntry {
    char name[64];
    uint32_t offset;
    uint32_t size;
    uint32_t type_id;
    uint32_t array_size;
    uint32_t flags;
    Atomicity atomicity;
    bool is_nested;
};
```

---

## Synchronization Wrappers

### Guarded<T> (Seqlock)

```cpp
template<typename T>
struct Guarded {
    void write(const T& value);
    T read() const;
    std::optional<T> try_read() const;
};
```

Seqlock wrapper for consistent reads of compound types.

---

### Locked<T> (Mutex)

```cpp
template<typename T>
struct Locked {
    void write(const T& value);
    T read() const;

    template<typename F>
    void update(F&& func);
};
```

Mutex wrapper for exclusive access.

---

## Code Generator

### Command Line

```bash
memglass-gen [options] <input-files...>
```

**Options:**
- `-o <file>` - Output file (required)
- `-I <path>` - Add include path
- `--` - Pass remaining args to clang

**Example:**
```bash
memglass-gen my_types.hpp -I./include -o my_types_generated.hpp
```

---

### Annotations

Annotations are specified in field comments:

| Annotation | Description |
|------------|-------------|
| `@atomic` | Use `std::atomic<T>` |
| `@seqlock` | Use `Guarded<T>` |
| `@locked` | Use `Locked<T>` |

**Example:**
```cpp
struct [[memglass::observe]] Data {
    uint64_t counter;    // @atomic
    Quote quote;         // @seqlock
    char msg[256];       // @locked
    int32_t debug;       // (no annotation)
};
```
