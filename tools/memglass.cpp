// Generic interactive observer - works with any memglass session
// Tree-based browser with expandable/collapsible hierarchy
// Supports nested structs via field name prefixes (e.g., "quote.bid_price")
#include <memglass/observer.hpp>

#include <fmt/format.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <csignal>
#include <chrono>
#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>

static volatile bool g_running = true;

void signal_handler(int) {
    g_running = false;
}

// Format a field value based on its primitive type
std::string format_value(const memglass::FieldProxy& field) {
    auto* info = field.info();
    if (!info) return "<invalid>";

    switch (static_cast<memglass::PrimitiveType>(info->type_id)) {
        case memglass::PrimitiveType::Bool:
            return field.as<bool>() ? "true" : "false";
        case memglass::PrimitiveType::Int8:
            return std::to_string(static_cast<int>(field.as<int8_t>()));
        case memglass::PrimitiveType::UInt8:
            return std::to_string(static_cast<unsigned>(field.as<uint8_t>()));
        case memglass::PrimitiveType::Int16:
            return std::to_string(field.as<int16_t>());
        case memglass::PrimitiveType::UInt16:
            return std::to_string(field.as<uint16_t>());
        case memglass::PrimitiveType::Int32:
            return std::to_string(field.as<int32_t>());
        case memglass::PrimitiveType::UInt32:
            return std::to_string(field.as<uint32_t>());
        case memglass::PrimitiveType::Int64:
            return std::to_string(field.as<int64_t>());
        case memglass::PrimitiveType::UInt64:
            return std::to_string(field.as<uint64_t>());
        case memglass::PrimitiveType::Float32:
            return fmt::format("{:.6g}", field.as<float>());
        case memglass::PrimitiveType::Float64:
            return fmt::format("{:.6g}", field.as<double>());
        case memglass::PrimitiveType::Char:
            return fmt::format("'{}'", field.as<char>());
        default:
            return "<unknown>";
    }
}

std::string atomicity_str(memglass::Atomicity a) {
    switch (a) {
        case memglass::Atomicity::Atomic: return " [atomic]";
        case memglass::Atomicity::Seqlock: return " [seqlock]";
        case memglass::Atomicity::Locked: return " [locked]";
        default: return "";
    }
}

// Tree browser class
class TreeBrowser {
public:
    TreeBrowser(memglass::Observer& obs) : obs_(obs) {}

    void run() {
        // Set terminal to raw mode for single keypress detection
        struct termios old_term, new_term;
        tcgetattr(STDIN_FILENO, &old_term);
        new_term = old_term;
        new_term.c_lflag &= ~(ICANON | ECHO);
        new_term.c_cc[VMIN] = 0;
        new_term.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

        // Hide cursor
        std::cout << "\033[?25l";

        refresh_objects();
        render();

        while (g_running) {
            // Use select() to wait for input with 500ms timeout
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);

            struct timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 500000;  // 500ms

            int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);

            if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
                // Read available input using raw read()
                char buf[8];
                ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
                if (n > 0) {
                    char ch = buf[0];
                    if (ch == 'q' || ch == 'Q') {
                        break;
                    } else if (ch == '\033' && n >= 3 && buf[1] == '[') {
                        // Escape sequence for arrow keys
                        if (buf[2] == 'A') {  // Up arrow
                            move_up();
                        } else if (buf[2] == 'B') {  // Down arrow
                            move_down();
                        }
                    } else if (ch == 'k' || ch == 'K') {  // vim up
                        move_up();
                    } else if (ch == 'j' || ch == 'J') {  // vim down
                        move_down();
                    } else if (ch == '\n' || ch == '\r' || ch == ' ') {  // Enter or space
                        toggle_expand();
                    } else if (ch == 'r' || ch == 'R') {  // Refresh objects list
                        refresh_objects();
                    } else if (ch == 'h' || ch == 'H' || ch == '?') {  // Help
                        show_help_ = !show_help_;
                    }
                }
            }

            // Always render (auto-update values every 500ms)
            render();
        }

        // Show cursor
        std::cout << "\033[?25h";

        // Restore terminal
        tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    }

private:
    // Line types in the display
    enum class LineType { Object, FieldGroup, Field };

    struct DisplayLine {
        LineType type;
        size_t object_index;
        std::string field_group;     // For FieldGroup lines (e.g., "quote", "position")
        size_t field_index;          // For Field lines
        int indent;
        std::string display_name;
    };

    // Parsed field info for grouping
    struct FieldGroupInfo {
        std::string group_name;      // e.g., "quote" or "" for ungrouped
        std::string field_name;      // e.g., "bid_price"
        size_t original_index;       // Index in type's fields vector
    };

    void refresh_objects() {
        objects_ = obs_.objects();
    }

    // Parse field groups from a type (fields like "quote.bid_price" -> group "quote")
    std::map<std::string, std::vector<FieldGroupInfo>> get_field_groups(const memglass::ObservedType* type) {
        std::map<std::string, std::vector<FieldGroupInfo>> groups;

        if (!type) return groups;

        for (size_t i = 0; i < type->fields.size(); ++i) {
            const auto& field = type->fields[i];
            std::string full_name = field.name;

            FieldGroupInfo info;
            info.original_index = i;

            // Check for dot notation (nested struct)
            size_t dot_pos = full_name.find('.');
            if (dot_pos != std::string::npos) {
                info.group_name = full_name.substr(0, dot_pos);
                info.field_name = full_name.substr(dot_pos + 1);
            } else {
                info.group_name = "";  // Ungrouped
                info.field_name = full_name;
            }

            groups[info.group_name].push_back(info);
        }

        return groups;
    }

    void build_display_lines() {
        lines_.clear();

        for (size_t obj_idx = 0; obj_idx < objects_.size(); ++obj_idx) {
            const auto& obj = objects_[obj_idx];

            // Add object line
            DisplayLine obj_line;
            obj_line.type = LineType::Object;
            obj_line.object_index = obj_idx;
            obj_line.field_index = 0;
            obj_line.indent = 0;
            obj_line.display_name = obj.label;
            lines_.push_back(obj_line);

            // If object is expanded, add field groups and fields
            if (expanded_objects_.count(obj_idx)) {
                const memglass::ObservedType* type_info = nullptr;
                for (const auto& t : obs_.types()) {
                    if (t.name == obj.type_name) {
                        type_info = &t;
                        break;
                    }
                }

                if (type_info) {
                    auto field_groups = get_field_groups(type_info);

                    // Sort group names (empty string first for ungrouped fields)
                    std::vector<std::string> sorted_groups;
                    for (const auto& [name, _] : field_groups) {
                        sorted_groups.push_back(name);
                    }
                    std::sort(sorted_groups.begin(), sorted_groups.end());

                    for (const auto& group_name : sorted_groups) {
                        const auto& fields_in_group = field_groups[group_name];

                        if (group_name.empty()) {
                            // Ungrouped fields - add directly
                            for (const auto& fi : fields_in_group) {
                                DisplayLine field_line;
                                field_line.type = LineType::Field;
                                field_line.object_index = obj_idx;
                                field_line.field_group = "";
                                field_line.field_index = fi.original_index;
                                field_line.indent = 1;
                                field_line.display_name = fi.field_name;
                                lines_.push_back(field_line);
                            }
                        } else {
                            // Field group header
                            DisplayLine group_line;
                            group_line.type = LineType::FieldGroup;
                            group_line.object_index = obj_idx;
                            group_line.field_group = group_name;
                            group_line.field_index = 0;
                            group_line.indent = 1;
                            group_line.display_name = group_name;
                            lines_.push_back(group_line);

                            // If field group is expanded, add its fields
                            std::string expand_key = fmt::format("{}:{}", obj_idx, group_name);
                            if (expanded_field_groups_.count(expand_key)) {
                                for (const auto& fi : fields_in_group) {
                                    DisplayLine field_line;
                                    field_line.type = LineType::Field;
                                    field_line.object_index = obj_idx;
                                    field_line.field_group = group_name;
                                    field_line.field_index = fi.original_index;
                                    field_line.indent = 2;
                                    field_line.display_name = fi.field_name;
                                    lines_.push_back(field_line);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Clamp cursor
        if (!lines_.empty() && cursor_ >= lines_.size()) {
            cursor_ = lines_.size() - 1;
        }
    }

    void render() {
        build_display_lines();

        // Get terminal size
        struct winsize ws;
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
        int term_height = ws.ws_row;
        int term_width = ws.ws_col;

        // Calculate visible range
        int header_lines = 3;
        int footer_lines = show_help_ ? 6 : 2;
        int visible_lines = term_height - header_lines - footer_lines;
        if (visible_lines < 1) visible_lines = 1;

        // Scroll to keep cursor visible
        if (cursor_ < scroll_offset_) {
            scroll_offset_ = cursor_;
        } else if (cursor_ >= scroll_offset_ + visible_lines) {
            scroll_offset_ = cursor_ - visible_lines + 1;
        }

        // Clear screen and move to top
        std::cout << "\033[2J\033[H";

        // Header
        auto now = std::chrono::steady_clock::now().time_since_epoch();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count() % 100000;
        std::cout << "\033[1;36m=== Memglass Browser ===\033[0m\n";
        std::cout << "PID: " << obs_.producer_pid() << "  Objects: " << objects_.size();
        std::cout << "  Seq: " << obs_.sequence() << "  t:" << ms << "\n";
        std::cout << std::string(std::min(term_width, 80), '-') << "\n";

        // Content
        for (int i = 0; i < visible_lines && (scroll_offset_ + i) < lines_.size(); ++i) {
            size_t line_idx = scroll_offset_ + i;
            const auto& line = lines_[line_idx];
            bool is_selected = (line_idx == cursor_);

            // Selection highlight
            if (is_selected) {
                std::cout << "\033[7m";  // Reverse video
            }

            // Indent
            std::cout << std::string(line.indent * 2, ' ');

            if (line.type == LineType::Object) {
                const auto& obj = objects_[line.object_index];
                bool is_expanded = expanded_objects_.count(line.object_index);

                std::cout << (is_expanded ? "[-] " : "[+] ");
                std::cout << fmt::format("\033[1;33m{}\033[0m", obj.label);
                if (is_selected) std::cout << "\033[7m";
                std::cout << fmt::format(" \033[0;36m({})\033[0m", obj.type_name);
                if (is_selected) std::cout << "\033[7m";

            } else if (line.type == LineType::FieldGroup) {
                std::string expand_key = fmt::format("{}:{}", line.object_index, line.field_group);
                bool is_expanded = expanded_field_groups_.count(expand_key);

                std::cout << (is_expanded ? "[-] " : "[+] ");
                std::cout << fmt::format("\033[0;32m{}\033[0m", line.display_name);
                if (is_selected) std::cout << "\033[7m";

            } else {  // Field
                const auto& obj = objects_[line.object_index];
                const memglass::ObservedType* type_info = nullptr;
                for (const auto& t : obs_.types()) {
                    if (t.name == obj.type_name) {
                        type_info = &t;
                        break;
                    }
                }

                if (type_info && line.field_index < type_info->fields.size()) {
                    const auto& field = type_info->fields[line.field_index];

                    // Get field value
                    auto view = obs_.get(obj);
                    std::string value = "<unavailable>";
                    if (view) {
                        auto fv = view[field.name];
                        if (fv) {
                            value = format_value(fv);
                        }
                    }

                    std::cout << fmt::format("    \033[0;37m{:<16}\033[0m", line.display_name);
                    if (is_selected) std::cout << "\033[7m";
                    std::cout << " = ";
                    std::cout << fmt::format("\033[1;37m{:>14}\033[0m", value);
                    if (is_selected) std::cout << "\033[7m";

                    // Atomicity indicator
                    std::string atom = atomicity_str(field.atomicity);
                    if (!atom.empty()) {
                        std::cout << fmt::format("\033[0;35m{}\033[0m", atom);
                        if (is_selected) std::cout << "\033[7m";
                    }
                }
            }

            // Clear to end of line and reset
            std::cout << "\033[K\033[0m\n";
        }

        // Fill remaining lines
        for (size_t i = lines_.size() > scroll_offset_ ? lines_.size() - scroll_offset_ : 0;
             i < static_cast<size_t>(visible_lines); ++i) {
            std::cout << "\033[K\n";
        }

        // Footer
        std::cout << std::string(std::min(term_width, 80), '-') << "\n";

        if (show_help_) {
            std::cout << "\033[0;33mNavigation:\033[0m Up/Down or j/k  "
                      << "\033[0;33mExpand/Collapse:\033[0m Enter/Space\n";
            std::cout << "\033[0;33mRefresh:\033[0m r  "
                      << "\033[0;33mHelp:\033[0m h/?  "
                      << "\033[0;33mQuit:\033[0m q\n";
            std::cout << "\n";
            std::cout << "[+] = collapsed, [-] = expanded\n";
        } else {
            std::cout << "h/? for help | q to quit\n";
        }

        std::cout.flush();
    }

    void move_up() {
        if (cursor_ > 0) {
            cursor_--;
        }
    }

    void move_down() {
        build_display_lines();
        if (cursor_ + 1 < lines_.size()) {
            cursor_++;
        }
    }

    void toggle_expand() {
        build_display_lines();
        if (cursor_ >= lines_.size()) return;

        const auto& line = lines_[cursor_];

        if (line.type == LineType::Object) {
            // Toggle object expansion
            if (expanded_objects_.count(line.object_index)) {
                expanded_objects_.erase(line.object_index);
            } else {
                expanded_objects_.insert(line.object_index);
            }
        } else if (line.type == LineType::FieldGroup) {
            // Toggle field group expansion
            std::string expand_key = fmt::format("{}:{}", line.object_index, line.field_group);
            if (expanded_field_groups_.count(expand_key)) {
                expanded_field_groups_.erase(expand_key);
            } else {
                expanded_field_groups_.insert(expand_key);
            }
        }
        // Fields don't expand further
    }

    memglass::Observer& obs_;
    std::vector<memglass::ObservedObject> objects_;

    // Expansion state
    std::set<size_t> expanded_objects_;
    std::set<std::string> expanded_field_groups_;  // "obj_idx:group_name"

    // Display
    std::vector<DisplayLine> lines_;
    size_t cursor_ = 0;
    size_t scroll_offset_ = 0;
    bool show_help_ = false;
};

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <session_name>\n";
        return 1;
    }

    std::string session_name = argv[1];

    memglass::Observer obs(session_name);

    std::cerr << "Connecting to session '" << session_name << "'...\n";

    if (!obs.connect()) {
        std::cerr << "Failed to connect. Is the producer running?\n";
        return 1;
    }

    std::cerr << "Connected to PID: " << obs.producer_pid() << "\n";
    std::cerr << "Starting browser...\n";

    TreeBrowser browser(obs);
    browser.run();

    std::cout << "\nDisconnecting...\n";
    obs.disconnect();

    return 0;
}
