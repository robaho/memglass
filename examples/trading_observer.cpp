// Example observer - trading engine monitor
#include <memglass/observer.hpp>

#include <fmt/format.h>
#include <chrono>
#include <thread>
#include <csignal>
#include <iostream>
#include <iomanip>

static volatile bool g_running = true;

void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::string session_name = "trading_engine";
    if (argc > 1) {
        session_name = argv[1];
    }

    memglass::Observer obs(session_name);

    std::cout << "Connecting to session '" << session_name << "'...\n";

    if (!obs.connect()) {
        std::cerr << "Failed to connect. Is the producer running?\n";
        return 1;
    }

    std::cout << "Connected to PID: " << obs.producer_pid() << "\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    // Print types
    std::cout << "Registered types:\n";
    for (const auto& type : obs.types()) {
        std::cout << "  " << type.name << " (" << type.size << " bytes)\n";
        for (const auto& field : type.fields) {
            std::cout << "    " << field.name << " @ offset " << field.offset << "\n";
        }
    }
    std::cout << "\n";

    uint64_t last_seq = 0;

    while (g_running) {
        // Check for structural changes
        uint64_t current_seq = obs.sequence();
        if (current_seq != last_seq) {
            obs.refresh();
            last_seq = current_seq;
        }

        // Clear screen and print header
        std::cout << "\033[2J\033[H";  // Clear screen
        std::cout << "=== Trading Engine Monitor ===\n";
        std::cout << "Session: " << session_name << "  PID: " << obs.producer_pid();
        std::cout << "  Seq: " << current_seq << "\n\n";

        // Get all objects
        auto objects = obs.objects();

        // Print quotes
        std::cout << "QUOTES:\n";
        std::cout << std::setw(10) << "Symbol"
                  << std::setw(12) << "Bid"
                  << std::setw(8) << "BidSz"
                  << std::setw(12) << "Ask"
                  << std::setw(8) << "AskSz" << "\n";
        std::cout << std::string(50, '-') << "\n";

        for (const auto& obj : objects) {
            if (obj.type_name == "Quote") {
                auto view = obs.get(obj);
                if (!view) continue;

                // Extract symbol from label (e.g., "AAPL_quote" -> "AAPL")
                std::string symbol = obj.label;
                size_t pos = symbol.find("_quote");
                if (pos != std::string::npos) {
                    symbol = symbol.substr(0, pos);
                }

                int64_t bid = view["bid_price"].as<int64_t>();
                uint32_t bid_size = view["bid_size"].as<uint32_t>();
                int64_t ask = view["ask_price"].as<int64_t>();
                uint32_t ask_size = view["ask_size"].as<uint32_t>();

                std::cout << std::setw(10) << symbol
                          << std::setw(12) << bid
                          << std::setw(8) << bid_size
                          << std::setw(12) << ask
                          << std::setw(8) << ask_size << "\n";
            }
        }

        std::cout << "\n";

        // Print positions
        std::cout << "POSITIONS:\n";
        std::cout << std::setw(10) << "Symbol"
                  << std::setw(12) << "Qty"
                  << std::setw(12) << "AvgPx"
                  << std::setw(15) << "Unrealized" << "\n";
        std::cout << std::string(50, '-') << "\n";

        for (const auto& obj : objects) {
            if (obj.type_name == "Position") {
                auto view = obs.get(obj);
                if (!view) continue;

                // Extract symbol from label
                std::string symbol = obj.label;
                size_t pos = symbol.find("_position");
                if (pos != std::string::npos) {
                    symbol = symbol.substr(0, pos);
                }

                int64_t qty = view["quantity"].as<int64_t>();
                int64_t avg_price = view["avg_price"].as<int64_t>();
                int64_t unrealized = view["unrealized_pnl"].as<int64_t>();

                std::cout << std::setw(10) << symbol
                          << std::setw(12) << qty
                          << std::setw(12) << avg_price
                          << std::setw(15) << unrealized << "\n";
            }
        }

        std::cout << "\nTotal objects: " << objects.size() << "\n";
        std::cout << "(Refreshing every 1s, Ctrl+C to quit)\n";

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "\nDisconnecting...\n";
    obs.disconnect();

    return 0;
}
