#include "generator.hpp"

#include <fmt/format.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>

void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [options] <input.hpp> [input2.hpp ...]\n";
    std::cerr << "\nOptions:\n";
    std::cerr << "  -o <file>      Output file (default: stdout)\n";
    std::cerr << "  -I <path>      Add include path\n";
    std::cerr << "  -v, --verbose  Verbose output\n";
    std::cerr << "  --dry-run      Parse only, don't generate output\n";
    std::cerr << "  -h, --help     Show this help\n";
}

int main(int argc, char* argv[]) {
    std::string output_file;
    std::vector<std::string> input_files;
    std::vector<std::string> clang_args;
    bool verbose = false;
    bool dry_run = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "-I" && i + 1 < argc) {
            clang_args.push_back("-I" + std::string(argv[++i]));
        } else if (arg.substr(0, 2) == "-I") {
            clang_args.push_back(arg);
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "--dry-run") {
            dry_run = true;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg[0] == '-') {
            // Pass through to clang
            clang_args.push_back(arg);
        } else {
            input_files.push_back(arg);
        }
    }

    if (input_files.empty()) {
        std::cerr << "Error: No input files specified\n";
        print_usage(argv[0]);
        return 1;
    }

    memglass::gen::Generator gen;
    gen.set_verbose(verbose);

    // Parse all input files
    for (const auto& file : input_files) {
        if (verbose) {
            std::cout << "Parsing " << file << "...\n";
        }

        if (!gen.parse(file, clang_args)) {
            std::cerr << "Error parsing " << file << "\n";
            return 1;
        }
    }

    if (verbose) {
        std::cout << "Found " << gen.types().size() << " observable types\n";
        for (const auto& type : gen.types()) {
            std::cout << "  " << type.name << " (" << type.size << " bytes, "
                      << type.fields.size() << " fields)\n";
        }
    }

    if (dry_run) {
        return 0;
    }

    // Generate output
    std::string output = gen.generate_header();

    if (output_file.empty()) {
        std::cout << output;
    } else {
        std::ofstream out(output_file);
        if (!out) {
            std::cerr << "Error: Cannot open output file " << output_file << "\n";
            return 1;
        }
        out << output;

        if (verbose) {
            std::cout << "Generated " << output_file << "\n";
        }
    }

    return 0;
}
