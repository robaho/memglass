// memglass-diff: Snapshot diff tool for memglass sessions
// Takes periodic snapshots and outputs diffs (changed fields only)
// Supports both text/JSON and compact binary output formats

#include <memglass/observer.hpp>

#include <fmt/format.h>
#include <csignal>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <optional>
#include <cstdint>

static volatile bool g_running = true;

void signal_handler(int) {
    g_running = false;
}

// ============================================================================
// Value storage for comparison
// ============================================================================

// Union-like storage for field values
struct FieldValue {
    memglass::PrimitiveType type = memglass::PrimitiveType::Unknown;
    memglass::Atomicity atomicity = memglass::Atomicity::None;

    union {
        bool b;
        int8_t i8;
        uint8_t u8;
        int16_t i16;
        uint16_t u16;
        int32_t i32;
        uint32_t u32;
        int64_t i64;
        uint64_t u64;
        float f32;
        double f64;
        char c;
    } data;

    bool operator==(const FieldValue& other) const {
        if (type != other.type) return false;
        switch (type) {
            case memglass::PrimitiveType::Bool: return data.b == other.data.b;
            case memglass::PrimitiveType::Int8: return data.i8 == other.data.i8;
            case memglass::PrimitiveType::UInt8: return data.u8 == other.data.u8;
            case memglass::PrimitiveType::Int16: return data.i16 == other.data.i16;
            case memglass::PrimitiveType::UInt16: return data.u16 == other.data.u16;
            case memglass::PrimitiveType::Int32: return data.i32 == other.data.i32;
            case memglass::PrimitiveType::UInt32: return data.u32 == other.data.u32;
            case memglass::PrimitiveType::Int64: return data.i64 == other.data.i64;
            case memglass::PrimitiveType::UInt64: return data.u64 == other.data.u64;
            case memglass::PrimitiveType::Float32: return data.f32 == other.data.f32;
            case memglass::PrimitiveType::Float64: return data.f64 == other.data.f64;
            case memglass::PrimitiveType::Char: return data.c == other.data.c;
            default: return true;
        }
    }

    bool operator!=(const FieldValue& other) const { return !(*this == other); }

    // For delta encoding in binary format
    int64_t as_int64() const {
        switch (type) {
            case memglass::PrimitiveType::Bool: return data.b ? 1 : 0;
            case memglass::PrimitiveType::Int8: return data.i8;
            case memglass::PrimitiveType::UInt8: return data.u8;
            case memglass::PrimitiveType::Int16: return data.i16;
            case memglass::PrimitiveType::UInt16: return data.u16;
            case memglass::PrimitiveType::Int32: return data.i32;
            case memglass::PrimitiveType::UInt32: return data.u32;
            case memglass::PrimitiveType::Int64: return data.i64;
            case memglass::PrimitiveType::UInt64: return static_cast<int64_t>(data.u64);
            case memglass::PrimitiveType::Char: return data.c;
            default: return 0;
        }
    }

    std::string to_string() const {
        switch (type) {
            case memglass::PrimitiveType::Bool: return data.b ? "true" : "false";
            case memglass::PrimitiveType::Int8: return std::to_string(data.i8);
            case memglass::PrimitiveType::UInt8: return std::to_string(data.u8);
            case memglass::PrimitiveType::Int16: return std::to_string(data.i16);
            case memglass::PrimitiveType::UInt16: return std::to_string(data.u16);
            case memglass::PrimitiveType::Int32: return std::to_string(data.i32);
            case memglass::PrimitiveType::UInt32: return std::to_string(data.u32);
            case memglass::PrimitiveType::Int64: return std::to_string(data.i64);
            case memglass::PrimitiveType::UInt64: return std::to_string(data.u64);
            case memglass::PrimitiveType::Float32: return fmt::format("{:.6g}", data.f32);
            case memglass::PrimitiveType::Float64: return fmt::format("{:.6g}", data.f64);
            case memglass::PrimitiveType::Char: return fmt::format("'{}'", data.c);
            default: return "?";
        }
    }

    std::string to_json() const {
        switch (type) {
            case memglass::PrimitiveType::Bool: return data.b ? "true" : "false";
            case memglass::PrimitiveType::Int8: return std::to_string(data.i8);
            case memglass::PrimitiveType::UInt8: return std::to_string(data.u8);
            case memglass::PrimitiveType::Int16: return std::to_string(data.i16);
            case memglass::PrimitiveType::UInt16: return std::to_string(data.u16);
            case memglass::PrimitiveType::Int32: return std::to_string(data.i32);
            case memglass::PrimitiveType::UInt32: return std::to_string(data.u32);
            case memglass::PrimitiveType::Int64: return std::to_string(data.i64);
            case memglass::PrimitiveType::UInt64: return std::to_string(data.u64);
            case memglass::PrimitiveType::Float32: return fmt::format("{:.6g}", data.f32);
            case memglass::PrimitiveType::Float64: return fmt::format("{:.6g}", data.f64);
            case memglass::PrimitiveType::Char: return fmt::format("\"{}\"", data.c);
            default: return "null";
        }
    }
};

FieldValue read_field_value(const memglass::FieldProxy& field) {
    FieldValue v;
    auto* info = field.info();
    if (!info) return v;

    v.type = static_cast<memglass::PrimitiveType>(info->type_id);
    v.atomicity = info->atomicity;

    switch (v.type) {
        case memglass::PrimitiveType::Bool: v.data.b = field.as<bool>(); break;
        case memglass::PrimitiveType::Int8: v.data.i8 = field.as<int8_t>(); break;
        case memglass::PrimitiveType::UInt8: v.data.u8 = field.as<uint8_t>(); break;
        case memglass::PrimitiveType::Int16: v.data.i16 = field.as<int16_t>(); break;
        case memglass::PrimitiveType::UInt16: v.data.u16 = field.as<uint16_t>(); break;
        case memglass::PrimitiveType::Int32: v.data.i32 = field.as<int32_t>(); break;
        case memglass::PrimitiveType::UInt32: v.data.u32 = field.as<uint32_t>(); break;
        case memglass::PrimitiveType::Int64: v.data.i64 = field.as<int64_t>(); break;
        case memglass::PrimitiveType::UInt64: v.data.u64 = field.as<uint64_t>(); break;
        case memglass::PrimitiveType::Float32: v.data.f32 = field.as<float>(); break;
        case memglass::PrimitiveType::Float64: v.data.f64 = field.as<double>(); break;
        case memglass::PrimitiveType::Char: v.data.c = field.as<char>(); break;
        default: break;
    }
    return v;
}

// ============================================================================
// Snapshot storage
// ============================================================================

struct ObjectSnapshot {
    std::string label;
    std::string type_name;
    std::map<std::string, FieldValue> fields;  // field_name -> value
};

struct Snapshot {
    uint64_t timestamp_ns;  // nanoseconds since epoch
    uint64_t sequence;
    uint64_t pid;
    std::map<std::string, ObjectSnapshot> objects;  // label -> snapshot
};

Snapshot take_snapshot(memglass::Observer& obs) {
    Snapshot snap;
    snap.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    snap.sequence = obs.sequence();
    snap.pid = obs.producer_pid();

    obs.refresh();

    const auto& types = obs.types();
    for (const auto& obj : obs.objects()) {
        ObjectSnapshot os;
        os.label = obj.label;
        os.type_name = obj.type_name;

        // Find type info
        const memglass::ObservedType* type_info = nullptr;
        for (const auto& t : types) {
            if (t.name == obj.type_name) {
                type_info = &t;
                break;
            }
        }

        if (type_info) {
            auto view = obs.get(obj);
            if (view) {
                for (const auto& field : type_info->fields) {
                    auto fv = view[field.name];
                    if (fv) {
                        os.fields[field.name] = read_field_value(fv);
                    }
                }
            }
        }

        snap.objects[obj.label] = std::move(os);
    }

    return snap;
}

// ============================================================================
// Diff computation
// ============================================================================

struct FieldChange {
    std::string object_label;
    std::string field_name;
    FieldValue old_value;
    FieldValue new_value;
};

struct SnapshotDiff {
    uint64_t timestamp_ns;
    uint64_t old_sequence;
    uint64_t new_sequence;
    std::vector<std::string> added_objects;
    std::vector<std::string> removed_objects;
    std::vector<FieldChange> field_changes;

    bool empty() const {
        return added_objects.empty() && removed_objects.empty() && field_changes.empty();
    }
};

SnapshotDiff compute_diff(const Snapshot& old_snap, const Snapshot& new_snap) {
    SnapshotDiff diff;
    diff.timestamp_ns = new_snap.timestamp_ns;
    diff.old_sequence = old_snap.sequence;
    diff.new_sequence = new_snap.sequence;

    // Find added and removed objects
    for (const auto& [label, obj] : new_snap.objects) {
        if (old_snap.objects.find(label) == old_snap.objects.end()) {
            diff.added_objects.push_back(label);
        }
    }
    for (const auto& [label, obj] : old_snap.objects) {
        if (new_snap.objects.find(label) == new_snap.objects.end()) {
            diff.removed_objects.push_back(label);
        }
    }

    // Find field changes in existing objects
    for (const auto& [label, new_obj] : new_snap.objects) {
        auto old_it = old_snap.objects.find(label);
        if (old_it == old_snap.objects.end()) continue;  // new object, skip

        const auto& old_obj = old_it->second;

        for (const auto& [field_name, new_value] : new_obj.fields) {
            auto old_field_it = old_obj.fields.find(field_name);
            if (old_field_it == old_obj.fields.end()) {
                // New field (shouldn't happen normally)
                FieldChange change;
                change.object_label = label;
                change.field_name = field_name;
                change.new_value = new_value;
                diff.field_changes.push_back(change);
            } else if (old_field_it->second != new_value) {
                // Changed field
                FieldChange change;
                change.object_label = label;
                change.field_name = field_name;
                change.old_value = old_field_it->second;
                change.new_value = new_value;
                diff.field_changes.push_back(change);
            }
        }
    }

    return diff;
}

// ============================================================================
// Text/JSON output format
// ============================================================================

std::string json_escape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result += c; break;
        }
    }
    return result;
}

void write_diff_json(std::ostream& out, const SnapshotDiff& diff, bool pretty = false) {
    const char* nl = pretty ? "\n" : "";
    const char* sp = pretty ? "  " : "";

    out << "{" << nl;
    out << sp << "\"timestamp_ns\":" << diff.timestamp_ns << "," << nl;
    out << sp << "\"old_sequence\":" << diff.old_sequence << "," << nl;
    out << sp << "\"new_sequence\":" << diff.new_sequence << "," << nl;

    // Added objects
    out << sp << "\"added\":[";
    for (size_t i = 0; i < diff.added_objects.size(); ++i) {
        if (i > 0) out << ",";
        out << "\"" << json_escape(diff.added_objects[i]) << "\"";
    }
    out << "]," << nl;

    // Removed objects
    out << sp << "\"removed\":[";
    for (size_t i = 0; i < diff.removed_objects.size(); ++i) {
        if (i > 0) out << ",";
        out << "\"" << json_escape(diff.removed_objects[i]) << "\"";
    }
    out << "]," << nl;

    // Field changes
    out << sp << "\"changes\":[";
    for (size_t i = 0; i < diff.field_changes.size(); ++i) {
        if (i > 0) out << ",";
        const auto& c = diff.field_changes[i];
        out << nl << sp << sp << "{";
        out << "\"obj\":\"" << json_escape(c.object_label) << "\",";
        out << "\"field\":\"" << json_escape(c.field_name) << "\",";
        out << "\"old\":" << c.old_value.to_json() << ",";
        out << "\"new\":" << c.new_value.to_json();
        out << "}";
    }
    out << nl << sp << "]" << nl;
    out << "}" << nl;
}

void write_diff_text(std::ostream& out, const SnapshotDiff& diff) {
    // Compact human-readable format
    out << "@" << diff.timestamp_ns << " seq:" << diff.old_sequence << "->" << diff.new_sequence;

    if (!diff.added_objects.empty()) {
        out << " +objs:[";
        for (size_t i = 0; i < diff.added_objects.size(); ++i) {
            if (i > 0) out << ",";
            out << diff.added_objects[i];
        }
        out << "]";
    }

    if (!diff.removed_objects.empty()) {
        out << " -objs:[";
        for (size_t i = 0; i < diff.removed_objects.size(); ++i) {
            if (i > 0) out << ",";
            out << diff.removed_objects[i];
        }
        out << "]";
    }

    out << "\n";

    for (const auto& c : diff.field_changes) {
        out << "  " << c.object_label << "." << c.field_name
            << ": " << c.old_value.to_string() << " -> " << c.new_value.to_string() << "\n";
    }
}

// ============================================================================
// Binary output format
// ============================================================================

// Binary format:
// Header (per file):
//   Magic: "MGDF" (4 bytes)
//   Version: uint8 (1)
//   Flags: uint8 (0 = normal, 1 = with string table)
//   Reserved: 2 bytes
//
// Per diff record:
//   Record type: uint8 (1 = diff, 0 = end)
//   Timestamp delta: varint (ns since last record or file start)
//   Sequence: varint
//   Num added objects: varint
//   Num removed objects: varint
//   Num field changes: varint
//   For each added/removed: string_id (varint)
//   For each change:
//     object_id: varint (index into object list)
//     field_id: varint (index into field list for that type)
//     value_delta: varint (for integers) or raw bytes (for floats)

class BinaryWriter {
public:
    explicit BinaryWriter(std::ostream& out) : out_(out) {}

    void write_header() {
        out_.write("MGDF", 4);  // Magic
        write_u8(1);            // Version
        write_u8(0);            // Flags
        write_u8(0);            // Reserved
        write_u8(0);            // Reserved
    }

    void write_diff(const SnapshotDiff& diff, uint64_t last_timestamp) {
        write_u8(1);  // Record type: diff

        // Timestamp delta (can be negative if clock skew, but usually positive)
        int64_t ts_delta = static_cast<int64_t>(diff.timestamp_ns - last_timestamp);
        write_varint_signed(ts_delta);

        write_varint(diff.new_sequence);
        write_varint(diff.added_objects.size());
        write_varint(diff.removed_objects.size());
        write_varint(diff.field_changes.size());

        // Added objects (write full strings for now)
        for (const auto& obj : diff.added_objects) {
            write_string(obj);
        }

        // Removed objects
        for (const auto& obj : diff.removed_objects) {
            write_string(obj);
        }

        // Field changes
        for (const auto& c : diff.field_changes) {
            write_string(c.object_label);
            write_string(c.field_name);
            write_u8(static_cast<uint8_t>(c.new_value.type));

            // For integer types, write delta; for others, write raw value
            if (is_integer_type(c.new_value.type)) {
                int64_t delta = c.new_value.as_int64() - c.old_value.as_int64();
                write_varint_signed(delta);
            } else {
                write_raw_value(c.new_value);
            }
        }
    }

    void write_end() {
        write_u8(0);  // Record type: end
    }

private:
    std::ostream& out_;

    void write_u8(uint8_t v) {
        out_.put(static_cast<char>(v));
    }

    void write_varint(uint64_t v) {
        // Standard LEB128 encoding
        while (v >= 0x80) {
            out_.put(static_cast<char>((v & 0x7F) | 0x80));
            v >>= 7;
        }
        out_.put(static_cast<char>(v));
    }

    void write_varint_signed(int64_t v) {
        // ZigZag encoding then varint
        uint64_t encoded = (static_cast<uint64_t>(v) << 1) ^ (v >> 63);
        write_varint(encoded);
    }

    void write_string(const std::string& s) {
        write_varint(s.size());
        out_.write(s.data(), s.size());
    }

    bool is_integer_type(memglass::PrimitiveType t) {
        switch (t) {
            case memglass::PrimitiveType::Bool:
            case memglass::PrimitiveType::Int8:
            case memglass::PrimitiveType::UInt8:
            case memglass::PrimitiveType::Int16:
            case memglass::PrimitiveType::UInt16:
            case memglass::PrimitiveType::Int32:
            case memglass::PrimitiveType::UInt32:
            case memglass::PrimitiveType::Int64:
            case memglass::PrimitiveType::UInt64:
            case memglass::PrimitiveType::Char:
                return true;
            default:
                return false;
        }
    }

    void write_raw_value(const FieldValue& v) {
        switch (v.type) {
            case memglass::PrimitiveType::Float32:
                out_.write(reinterpret_cast<const char*>(&v.data.f32), sizeof(float));
                break;
            case memglass::PrimitiveType::Float64:
                out_.write(reinterpret_cast<const char*>(&v.data.f64), sizeof(double));
                break;
            default:
                // For integers, this shouldn't be called
                write_varint_signed(v.as_int64());
                break;
        }
    }
};

// ============================================================================
// Binary reader (for decoding)
// ============================================================================

class BinaryReader {
public:
    explicit BinaryReader(std::istream& in) : in_(in) {}

    bool read_header() {
        char magic[4];
        in_.read(magic, 4);
        if (std::memcmp(magic, "MGDF", 4) != 0) {
            return false;
        }

        version_ = read_u8();
        flags_ = read_u8();
        read_u8();  // Reserved
        read_u8();  // Reserved

        return version_ == 1;
    }

    std::optional<SnapshotDiff> read_diff(uint64_t& last_timestamp) {
        uint8_t record_type = read_u8();
        if (record_type == 0 || in_.eof()) {
            return std::nullopt;
        }

        SnapshotDiff diff;

        int64_t ts_delta = read_varint_signed();
        diff.timestamp_ns = last_timestamp + ts_delta;
        last_timestamp = diff.timestamp_ns;

        diff.new_sequence = read_varint();
        size_t num_added = read_varint();
        size_t num_removed = read_varint();
        size_t num_changes = read_varint();

        for (size_t i = 0; i < num_added; ++i) {
            diff.added_objects.push_back(read_string());
        }

        for (size_t i = 0; i < num_removed; ++i) {
            diff.removed_objects.push_back(read_string());
        }

        for (size_t i = 0; i < num_changes; ++i) {
            FieldChange c;
            c.object_label = read_string();
            c.field_name = read_string();
            c.new_value.type = static_cast<memglass::PrimitiveType>(read_u8());

            if (is_integer_type(c.new_value.type)) {
                int64_t delta = read_varint_signed();
                // We only have the new value (delta from old)
                // For display, we'll just show the delta
                set_int_value(c.new_value, delta);
            } else {
                read_raw_value(c.new_value);
            }

            diff.field_changes.push_back(c);
        }

        return diff;
    }

private:
    std::istream& in_;
    uint8_t version_ = 0;
    uint8_t flags_ = 0;

    uint8_t read_u8() {
        return static_cast<uint8_t>(in_.get());
    }

    uint64_t read_varint() {
        uint64_t result = 0;
        int shift = 0;
        while (true) {
            uint8_t b = read_u8();
            result |= static_cast<uint64_t>(b & 0x7F) << shift;
            if ((b & 0x80) == 0) break;
            shift += 7;
        }
        return result;
    }

    int64_t read_varint_signed() {
        uint64_t encoded = read_varint();
        return static_cast<int64_t>((encoded >> 1) ^ -(encoded & 1));
    }

    std::string read_string() {
        size_t len = read_varint();
        std::string s(len, '\0');
        in_.read(&s[0], len);
        return s;
    }

    bool is_integer_type(memglass::PrimitiveType t) {
        switch (t) {
            case memglass::PrimitiveType::Bool:
            case memglass::PrimitiveType::Int8:
            case memglass::PrimitiveType::UInt8:
            case memglass::PrimitiveType::Int16:
            case memglass::PrimitiveType::UInt16:
            case memglass::PrimitiveType::Int32:
            case memglass::PrimitiveType::UInt32:
            case memglass::PrimitiveType::Int64:
            case memglass::PrimitiveType::UInt64:
            case memglass::PrimitiveType::Char:
                return true;
            default:
                return false;
        }
    }

    void set_int_value(FieldValue& v, int64_t val) {
        switch (v.type) {
            case memglass::PrimitiveType::Bool: v.data.b = val != 0; break;
            case memglass::PrimitiveType::Int8: v.data.i8 = static_cast<int8_t>(val); break;
            case memglass::PrimitiveType::UInt8: v.data.u8 = static_cast<uint8_t>(val); break;
            case memglass::PrimitiveType::Int16: v.data.i16 = static_cast<int16_t>(val); break;
            case memglass::PrimitiveType::UInt16: v.data.u16 = static_cast<uint16_t>(val); break;
            case memglass::PrimitiveType::Int32: v.data.i32 = static_cast<int32_t>(val); break;
            case memglass::PrimitiveType::UInt32: v.data.u32 = static_cast<uint32_t>(val); break;
            case memglass::PrimitiveType::Int64: v.data.i64 = val; break;
            case memglass::PrimitiveType::UInt64: v.data.u64 = static_cast<uint64_t>(val); break;
            case memglass::PrimitiveType::Char: v.data.c = static_cast<char>(val); break;
            default: break;
        }
    }

    void read_raw_value(FieldValue& v) {
        switch (v.type) {
            case memglass::PrimitiveType::Float32:
                in_.read(reinterpret_cast<char*>(&v.data.f32), sizeof(float));
                break;
            case memglass::PrimitiveType::Float64:
                in_.read(reinterpret_cast<char*>(&v.data.f64), sizeof(double));
                break;
            default:
                set_int_value(v, read_varint_signed());
                break;
        }
    }
};

// ============================================================================
// Command-line interface
// ============================================================================

enum class OutputFormat {
    Text,
    Json,
    JsonPretty,
    Binary
};

struct Options {
    std::string session_name;
    std::string output_file;
    OutputFormat format = OutputFormat::Text;
    uint64_t interval_ms = 1000;
    bool skip_empty = true;
    bool decode_mode = false;
    std::string decode_file;
    bool help = false;
};

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [OPTIONS] <session_name>\n"
              << "       " << prog << " --decode <binary_file>\n"
              << "\n"
              << "Snapshot diff tool for memglass sessions.\n"
              << "Takes periodic snapshots and outputs changes (diffs).\n"
              << "\n"
              << "Options:\n"
              << "  -h, --help              Show this help message\n"
              << "  -i, --interval <ms>     Snapshot interval in milliseconds (default: 1000)\n"
              << "  -o, --output <file>     Write to file instead of stdout\n"
              << "  -f, --format <fmt>      Output format: text, json, json-pretty, binary\n"
              << "  -a, --all               Include empty diffs (no changes)\n"
              << "  --decode <file>         Decode a binary diff file to text\n"
              << "\n"
              << "Output Formats:\n"
              << "  text        Compact human-readable format (default)\n"
              << "  json        One JSON object per line (JSONL)\n"
              << "  json-pretty Pretty-printed JSON\n"
              << "  binary      Compact binary with varint/delta encoding\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " trading                    # Text output to stdout\n"
              << "  " << prog << " -i 100 -f binary -o diff.mgd trading\n"
              << "  " << prog << " --decode diff.mgd          # Decode binary to text\n";
}

Options parse_args(int argc, char* argv[]) {
    Options opts;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            opts.help = true;
            return opts;
        }
        else if (arg == "-i" || arg == "--interval") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a value\n";
                opts.help = true;
                return opts;
            }
            opts.interval_ms = std::stoull(argv[++i]);
        }
        else if (arg == "-o" || arg == "--output") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a filename\n";
                opts.help = true;
                return opts;
            }
            opts.output_file = argv[++i];
        }
        else if (arg == "-f" || arg == "--format") {
            if (i + 1 >= argc) {
                std::cerr << "Error: " << arg << " requires a format\n";
                opts.help = true;
                return opts;
            }
            std::string fmt = argv[++i];
            if (fmt == "text") opts.format = OutputFormat::Text;
            else if (fmt == "json") opts.format = OutputFormat::Json;
            else if (fmt == "json-pretty") opts.format = OutputFormat::JsonPretty;
            else if (fmt == "binary") opts.format = OutputFormat::Binary;
            else {
                std::cerr << "Error: unknown format '" << fmt << "'\n";
                opts.help = true;
                return opts;
            }
        }
        else if (arg == "-a" || arg == "--all") {
            opts.skip_empty = false;
        }
        else if (arg == "--decode") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --decode requires a filename\n";
                opts.help = true;
                return opts;
            }
            opts.decode_mode = true;
            opts.decode_file = argv[++i];
        }
        else if (arg[0] == '-') {
            std::cerr << "Error: unknown option '" << arg << "'\n";
            opts.help = true;
            return opts;
        }
        else {
            opts.session_name = arg;
        }
    }

    return opts;
}

int decode_binary_file(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        std::cerr << "Error: cannot open '" << filename << "'\n";
        return 1;
    }

    BinaryReader reader(in);
    if (!reader.read_header()) {
        std::cerr << "Error: invalid binary diff file\n";
        return 1;
    }

    uint64_t last_timestamp = 0;
    while (auto diff = reader.read_diff(last_timestamp)) {
        write_diff_text(std::cout, *diff);
    }

    return 0;
}

int run_diff(const Options& opts) {
    memglass::Observer obs(opts.session_name);

    std::cerr << "Connecting to session '" << opts.session_name << "'...\n";

    if (!obs.connect()) {
        std::cerr << "Failed to connect. Is the producer running?\n";
        return 1;
    }

    std::cerr << "Connected to PID: " << obs.producer_pid() << "\n";
    std::cerr << "Taking snapshots every " << opts.interval_ms << "ms. Press Ctrl+C to stop.\n";

    // Output stream
    std::ofstream file_out;
    std::ostream* out = &std::cout;
    if (!opts.output_file.empty()) {
        auto mode = (opts.format == OutputFormat::Binary)
            ? (std::ios::out | std::ios::binary)
            : std::ios::out;
        file_out.open(opts.output_file, mode);
        if (!file_out) {
            std::cerr << "Error: cannot open output file '" << opts.output_file << "'\n";
            return 1;
        }
        out = &file_out;
    }

    // Binary writer (if needed)
    std::unique_ptr<BinaryWriter> bin_writer;
    if (opts.format == OutputFormat::Binary) {
        bin_writer = std::make_unique<BinaryWriter>(*out);
        bin_writer->write_header();
    }

    // Take initial snapshot
    Snapshot prev_snap = take_snapshot(obs);
    uint64_t last_timestamp = prev_snap.timestamp_ns;
    uint64_t diff_count = 0;
    uint64_t change_count = 0;

    auto interval = std::chrono::milliseconds(opts.interval_ms);

    while (g_running) {
        std::this_thread::sleep_for(interval);
        if (!g_running) break;

        Snapshot new_snap = take_snapshot(obs);
        SnapshotDiff diff = compute_diff(prev_snap, new_snap);

        if (!diff.empty() || !opts.skip_empty) {
            switch (opts.format) {
                case OutputFormat::Text:
                    write_diff_text(*out, diff);
                    break;
                case OutputFormat::Json:
                    write_diff_json(*out, diff, false);
                    break;
                case OutputFormat::JsonPretty:
                    write_diff_json(*out, diff, true);
                    break;
                case OutputFormat::Binary:
                    bin_writer->write_diff(diff, last_timestamp);
                    break;
            }

            out->flush();
            diff_count++;
            change_count += diff.field_changes.size();
        }

        last_timestamp = new_snap.timestamp_ns;
        prev_snap = std::move(new_snap);
    }

    if (opts.format == OutputFormat::Binary && bin_writer) {
        bin_writer->write_end();
    }

    std::cerr << "\nRecorded " << diff_count << " diffs with " << change_count << " total changes\n";

    return 0;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    Options opts = parse_args(argc, argv);

    if (opts.help) {
        print_usage(argv[0]);
        return 1;
    }

    if (opts.decode_mode) {
        return decode_binary_file(opts.decode_file);
    }

    if (opts.session_name.empty()) {
        std::cerr << "Error: session name required\n\n";
        print_usage(argv[0]);
        return 1;
    }

    return run_diff(opts);
}
