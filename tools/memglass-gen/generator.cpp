#include "generator.hpp"

#include <fmt/format.h>
#include <regex>
#include <iostream>
#include <sstream>
#include <cstring>

namespace memglass::gen {

Generator::Generator() {
    index_ = clang_createIndex(0, 0);
}

Generator::~Generator() {
    if (index_) {
        clang_disposeIndex(index_);
    }
}

bool Generator::parse(const std::string& filename, const std::vector<std::string>& args) {
    // Convert args to char**
    std::vector<const char*> c_args;
    c_args.push_back("-std=c++20");
    c_args.push_back("-fparse-all-comments");  // Important: parse all comments
    for (const auto& arg : args) {
        c_args.push_back(arg.c_str());
    }

    CXTranslationUnit tu = clang_parseTranslationUnit(
        index_,
        filename.c_str(),
        c_args.data(),
        static_cast<int>(c_args.size()),
        nullptr,
        0,
        CXTranslationUnit_DetailedPreprocessingRecord |
        CXTranslationUnit_SkipFunctionBodies
    );

    if (!tu) {
        std::cerr << "Failed to parse " << filename << "\n";
        return false;
    }

    // Check for errors
    unsigned num_diags = clang_getNumDiagnostics(tu);
    bool has_errors = false;
    for (unsigned i = 0; i < num_diags; ++i) {
        CXDiagnostic diag = clang_getDiagnostic(tu, i);
        CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diag);
        if (severity >= CXDiagnostic_Error) {
            has_errors = true;
            CXString msg = clang_formatDiagnostic(diag, CXDiagnostic_DisplaySourceLocation);
            std::cerr << clang_getCString(msg) << "\n";
            clang_disposeString(msg);
        }
        clang_disposeDiagnostic(diag);
    }

    if (has_errors) {
        clang_disposeTranslationUnit(tu);
        return false;
    }

    // Visit AST
    CXCursor cursor = clang_getTranslationUnitCursor(tu);
    clang_visitChildren(cursor, visit_cursor, this);

    clang_disposeTranslationUnit(tu);
    return true;
}

CXChildVisitResult Generator::visit_cursor(CXCursor cursor, CXCursor parent, CXClientData data) {
    auto* gen = static_cast<Generator*>(data);

    CXCursorKind kind = clang_getCursorKind(cursor);

    // Look for struct/class declarations
    if (kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl) {
        // Check if it has the memglass::observe attribute
        if (gen->has_observe_attribute(cursor)) {
            TypeInfo type_info = gen->extract_type_info(cursor);
            if (!type_info.name.empty()) {
                if (gen->verbose_) {
                    std::cout << "Found observable type: " << type_info.name << "\n";
                }
                gen->types_.push_back(std::move(type_info));
            }
        }
    }

    // Recurse into namespaces
    if (kind == CXCursor_Namespace) {
        return CXChildVisit_Recurse;
    }

    return CXChildVisit_Continue;
}

bool Generator::has_observe_attribute(CXCursor cursor) {
    // Check for [[memglass::observe]] attribute
    // libclang doesn't directly expose C++11 attributes well,
    // so we check the raw source for the pattern

    CXSourceRange range = clang_getCursorExtent(cursor);
    CXSourceLocation start = clang_getRangeStart(range);

    CXFile file;
    unsigned line, column, offset;
    clang_getSpellingLocation(start, &file, &line, &column, &offset);

    if (!file) return false;

    CXString filename = clang_getFileName(file);
    const char* fname = clang_getCString(filename);

    // Read the source file to check for attribute
    // This is a simplified approach - in production you'd want to cache this
    FILE* f = fopen(fname, "r");
    clang_disposeString(filename);

    if (!f) return false;

    // Read a bit before the struct declaration to find attributes
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string content(file_size, '\0');
    fread(&content[0], 1, file_size, f);
    fclose(f);

    // Find the struct declaration and look for attribute before it
    size_t search_start = (offset > 200) ? offset - 200 : 0;
    size_t search_end = offset + 50;
    if (search_end > content.size()) search_end = content.size();

    std::string search_region = content.substr(search_start, search_end - search_start);

    // Look for [[memglass::observe]] pattern
    return search_region.find("[[memglass::observe]]") != std::string::npos ||
           search_region.find("[[ memglass::observe ]]") != std::string::npos ||
           search_region.find("[[memglass :: observe]]") != std::string::npos;
}

TypeInfo Generator::extract_type_info(CXCursor cursor) {
    TypeInfo info;

    CXString name = clang_getCursorSpelling(cursor);
    info.name = clang_getCString(name);
    clang_disposeString(name);

    // Get qualified name (with namespaces)
    CXString qualified = clang_getCursorDisplayName(cursor);
    info.qualified_name = clang_getCString(qualified);
    clang_disposeString(qualified);

    // Get size and alignment
    CXType type = clang_getCursorType(cursor);
    info.size = static_cast<uint32_t>(clang_Type_getSizeOf(type));
    info.alignment = static_cast<uint32_t>(clang_Type_getAlignOf(type));

    // Visit fields
    struct FieldVisitorData {
        Generator* gen;
        TypeInfo* info;
        CXCursor parent;
    };

    FieldVisitorData field_data{this, &info, cursor};

    clang_visitChildren(cursor, [](CXCursor c, CXCursor p, CXClientData d) {
        auto* data = static_cast<FieldVisitorData*>(d);
        CXCursorKind kind = clang_getCursorKind(c);

        if (kind == CXCursor_FieldDecl) {
            FieldInfo field = data->gen->extract_field_info(c, data->parent);
            if (!field.name.empty()) {
                data->info->fields.push_back(std::move(field));
            }
        }

        return CXChildVisit_Continue;
    }, &field_data);

    return info;
}

FieldInfo Generator::extract_field_info(CXCursor cursor, CXCursor parent) {
    FieldInfo info;

    CXString name = clang_getCursorSpelling(cursor);
    info.name = clang_getCString(name);
    clang_disposeString(name);

    CXType type = clang_getCursorType(cursor);
    CXType parent_type = clang_getCursorType(parent);

    // Get offset
    long long offset = clang_Type_getOffsetOf(parent_type, info.name.c_str());
    if (offset >= 0) {
        info.offset = static_cast<uint32_t>(offset / 8);  // bits to bytes
    }

    // Get size
    info.size = static_cast<uint32_t>(clang_Type_getSizeOf(type));

    // Get type name
    CXString type_spelling = clang_getTypeSpelling(type);
    info.type_name = clang_getCString(type_spelling);
    clang_disposeString(type_spelling);

    // Check if array
    if (type.kind == CXType_ConstantArray) {
        info.is_array = true;
        info.array_size = static_cast<uint32_t>(clang_getArraySize(type));
        CXType elem_type = clang_getArrayElementType(type);
        CXString elem_spelling = clang_getTypeSpelling(elem_type);
        info.type_name = clang_getCString(elem_spelling);
        clang_disposeString(elem_spelling);
    }

    // Check if nested struct
    CXType canonical = clang_getCanonicalType(type);
    if (canonical.kind == CXType_Record) {
        CXCursor type_decl = clang_getTypeDeclaration(canonical);
        if (clang_getCursorKind(type_decl) == CXCursor_StructDecl) {
            info.is_nested = true;
            CXString nested_name = clang_getCursorSpelling(type_decl);
            info.nested_type_name = clang_getCString(nested_name);
            clang_disposeString(nested_name);
        }
    }

    // Parse comment metadata
    info.meta = parse_comment(cursor);

    return info;
}

FieldMeta Generator::parse_comment(CXCursor cursor) {
    FieldMeta meta;

    CXString raw_comment = clang_Cursor_getRawCommentText(cursor);
    const char* comment = clang_getCString(raw_comment);

    if (!comment) {
        clang_disposeString(raw_comment);
        return meta;
    }

    std::string text(comment);
    clang_disposeString(raw_comment);

    // Parse @readonly
    if (text.find("@readonly") != std::string::npos) {
        meta.readonly = true;
    }

    // Parse @atomic
    if (text.find("@atomic") != std::string::npos) {
        meta.atomicity = FieldMeta::Atomicity::Atomic;
    }

    // Parse @seqlock
    if (text.find("@seqlock") != std::string::npos) {
        meta.atomicity = FieldMeta::Atomicity::Seqlock;
    }

    // Parse @locked
    if (text.find("@locked") != std::string::npos) {
        meta.atomicity = FieldMeta::Atomicity::Locked;
    }

    // Parse @range(min, max)
    std::regex range_re(R"(@range\s*\(\s*([^,]+)\s*,\s*([^)]+)\s*\))");
    std::smatch match;
    if (std::regex_search(text, match, range_re)) {
        meta.has_range = true;
        meta.range_min = std::stod(match[1]);
        meta.range_max = std::stod(match[2]);
    }

    // Parse @min(val)
    std::regex min_re(R"(@min\s*\(\s*([^)]+)\s*\))");
    if (std::regex_search(text, match, min_re)) {
        meta.has_range = true;
        meta.range_min = std::stod(match[1]);
    }

    // Parse @max(val)
    std::regex max_re(R"(@max\s*\(\s*([^)]+)\s*\))");
    if (std::regex_search(text, match, max_re)) {
        meta.has_range = true;
        meta.range_max = std::stod(match[1]);
    }

    // Parse @step(val)
    std::regex step_re(R"(@step\s*\(\s*([^)]+)\s*\))");
    if (std::regex_search(text, match, step_re)) {
        meta.step = std::stod(match[1]);
    }

    // Parse @regex("pattern")
    std::regex regex_re(R"--(@regex\s*\(\s*"([^"]+)"\s*\))--");
    if (std::regex_search(text, match, regex_re)) {
        meta.regex_pattern = match[1];
    }

    // Parse @format("fmt")
    std::regex format_re(R"--(@format\s*\(\s*"([^"]+)"\s*\))--");
    if (std::regex_search(text, match, format_re)) {
        meta.format = match[1];
    }

    // Parse @unit("str")
    std::regex unit_re(R"--(@unit\s*\(\s*"([^"]+)"\s*\))--");
    if (std::regex_search(text, match, unit_re)) {
        meta.unit = match[1];
    }

    // Parse @enum(NAME=val, ...)
    std::regex enum_re(R"(@enum\s*\(([^)]+)\))");
    if (std::regex_search(text, match, enum_re)) {
        std::string enum_list = match[1];
        std::regex item_re(R"((\w+)\s*=\s*(-?\d+))");
        std::sregex_iterator it(enum_list.begin(), enum_list.end(), item_re);
        std::sregex_iterator end;
        while (it != end) {
            meta.enum_values.emplace_back((*it)[1], std::stoll((*it)[2]));
            ++it;
        }
    }

    // Parse @flags(NAME=bit, ...)
    std::regex flags_re(R"(@flags\s*\(([^)]+)\))");
    if (std::regex_search(text, match, flags_re)) {
        std::string flags_list = match[1];
        std::regex item_re(R"((\w+)\s*=\s*(\d+))");
        std::sregex_iterator it(flags_list.begin(), flags_list.end(), item_re);
        std::sregex_iterator end;
        while (it != end) {
            meta.flags.emplace_back((*it)[1], std::stoull((*it)[2]));
            ++it;
        }
    }

    return meta;
}

std::string Generator::generate_header() const {
    std::ostringstream out;

    out << "// Generated by memglass-gen - DO NOT EDIT\n";
    out << "#pragma once\n\n";
    out << "#include <memglass/memglass.hpp>\n";
    out << "#include <memglass/registry.hpp>\n";
    out << "#include <array>\n";
    out << "#include <cstddef>\n\n";
    out << "namespace memglass::generated {\n\n";

    // Generate TypeDescriptor specializations
    for (const auto& type : types_) {
        out << fmt::format("// Type: {}\n", type.name);
        out << fmt::format("inline uint32_t register_{}() {{\n", type.name);
        out << "    memglass::TypeDescriptor desc;\n";
        out << fmt::format("    desc.name = \"{}\";\n", type.name);
        out << fmt::format("    desc.size = {};\n", type.size);
        out << fmt::format("    desc.alignment = {};\n", type.alignment);
        out << "    desc.fields = {\n";

        for (const auto& field : type.fields) {
            out << "        {";
            out << fmt::format("\"{}\", ", field.name);
            out << fmt::format("{}, ", field.offset);
            out << fmt::format("{}, ", field.size);

            // Primitive type
            if (field.type_name == "bool") out << "memglass::PrimitiveType::Bool, ";
            else if (field.type_name == "int8_t" || field.type_name == "signed char") out << "memglass::PrimitiveType::Int8, ";
            else if (field.type_name == "uint8_t" || field.type_name == "unsigned char") out << "memglass::PrimitiveType::UInt8, ";
            else if (field.type_name == "int16_t" || field.type_name == "short") out << "memglass::PrimitiveType::Int16, ";
            else if (field.type_name == "uint16_t" || field.type_name == "unsigned short") out << "memglass::PrimitiveType::UInt16, ";
            else if (field.type_name == "int32_t" || field.type_name == "int") out << "memglass::PrimitiveType::Int32, ";
            else if (field.type_name == "uint32_t" || field.type_name == "unsigned int") out << "memglass::PrimitiveType::UInt32, ";
            else if (field.type_name == "int64_t" || field.type_name == "long" || field.type_name == "long long") out << "memglass::PrimitiveType::Int64, ";
            else if (field.type_name == "uint64_t" || field.type_name == "unsigned long" || field.type_name == "unsigned long long") out << "memglass::PrimitiveType::UInt64, ";
            else if (field.type_name == "float") out << "memglass::PrimitiveType::Float32, ";
            else if (field.type_name == "double") out << "memglass::PrimitiveType::Float64, ";
            else if (field.type_name == "char") out << "memglass::PrimitiveType::Char, ";
            else out << "memglass::PrimitiveType::Unknown, ";

            out << "0, ";  // user_type_id (TODO: resolve nested types)
            out << fmt::format("{}, ", field.array_size);

            // Atomicity
            switch (field.meta.atomicity) {
                case FieldMeta::Atomicity::Atomic: out << "memglass::Atomicity::Atomic, "; break;
                case FieldMeta::Atomicity::Seqlock: out << "memglass::Atomicity::Seqlock, "; break;
                case FieldMeta::Atomicity::Locked: out << "memglass::Atomicity::Locked, "; break;
                default: out << "memglass::Atomicity::None, "; break;
            }

            out << (field.meta.readonly ? "true" : "false");
            out << "},\n";
        }

        out << "    };\n";
        out << fmt::format("    return memglass::registry::register_type_for<{}>(desc);\n", type.name);
        out << "}\n\n";
    }

    // Generate register_all_types function
    out << "inline void register_all_types() {\n";
    for (const auto& type : types_) {
        out << fmt::format("    register_{}();\n", type.name);
    }
    out << "}\n\n";

    out << "} // namespace memglass::generated\n";

    return out.str();
}

} // namespace memglass::gen
