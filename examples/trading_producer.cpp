// Example producer - trading engine simulation
// Demonstrates using generated Producer wrappers for synchronized writes
#include <memglass/memglass.hpp>
#include "trading_types.hpp"
#include "trading_types_generated.hpp"

#include <fmt/format.h>
#include <chrono>
#include <random>
#include <thread>
#include <csignal>
#include <iostream>

static volatile bool g_running = true;

void signal_handler(int) {
    g_running = false;
}

int main() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize memglass
    if (!memglass::init("trading_engine")) {
        std::cerr << "Failed to initialize memglass\n";
        return 1;
    }

    // Register types
    memglass::generated::register_all_types();

    // Write types to header (trigger refresh in observers)
    memglass::registry::write_to_header(
        memglass::detail::get_context()->header(),
        memglass::detail::get_context()->header_shm().data());

    std::cout << "Trading engine started (PID: " << getpid() << ")\n";
    std::cout << "Press Ctrl+C to stop\n\n";

    // Create securities for tracked symbols
    const char* symbols[] = {"AAPL", "MSFT", "GOOG", "AMZN", "META"};

    // Store raw pointers for cleanup, and producer wrappers for synchronized access
    std::vector<Security*> securities;
    std::vector<memglass::generated::QuoteProducer> quote_producers;
    std::vector<memglass::generated::PositionProducer> position_producers;

    for (size_t i = 0; i < 5; ++i) {
        auto* sec = memglass::create<Security>(symbols[i]);
        if (!sec) {
            std::cerr << "Failed to create security " << symbols[i] << "\n";
            continue;
        }

        // Create producer wrappers for synchronized access
        auto quote_prod = memglass::generated::make_producer(&sec->quote);
        auto pos_prod = memglass::generated::make_producer(&sec->position);

        // Initialize quote using producer wrapper (atomic writes)
        int64_t base_price = 15000 + static_cast<int64_t>(i) * 1000;
        quote_prod.set_bid_price(base_price);
        quote_prod.set_ask_price(base_price + 5);
        quote_prod.set_bid_size(100);
        quote_prod.set_ask_size(100);
        quote_prod.set_timestamp_ns(0);

        // Initialize position (symbol_id has no atomicity annotation, so direct access)
        sec->position.symbol_id = static_cast<uint32_t>(i);
        pos_prod.set_quantity(0);
        pos_prod.set_avg_price(0);
        pos_prod.set_realized_pnl(0);
        pos_prod.set_unrealized_pnl(0);

        securities.push_back(sec);
        quote_producers.push_back(quote_prod);
        position_producers.push_back(pos_prod);
        std::cout << "Created " << symbols[i] << " security\n";
    }

    // Market simulation loop
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> price_delta(-10, 10);
    std::uniform_int_distribution<> size_change(-20, 20);

    uint64_t tick = 0;
    while (g_running) {
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();

        for (size_t i = 0; i < securities.size(); ++i) {
            auto& quote_prod = quote_producers[i];
            auto& pos_prod = position_producers[i];

            // Update quotes with price movement (using synchronized setters)
            int64_t bid = quote_prod.get_bid_price() + price_delta(gen);
            if (bid < 1000) bid = 1000;
            quote_prod.set_bid_price(bid);
            quote_prod.set_ask_price(bid + 5);

            int new_bid_size = static_cast<int>(quote_prod.get_bid_size()) + size_change(gen);
            if (new_bid_size < 10) new_bid_size = 10;
            quote_prod.set_bid_size(static_cast<uint32_t>(new_bid_size));

            int new_ask_size = static_cast<int>(quote_prod.get_ask_size()) + size_change(gen);
            if (new_ask_size < 10) new_ask_size = 10;
            quote_prod.set_ask_size(static_cast<uint32_t>(new_ask_size));

            quote_prod.set_timestamp_ns(static_cast<uint64_t>(now));

            // Occasionally change position
            if (tick % 100 == i * 20) {
                int pos_change = (gen() % 3) - 1;  // -1, 0, or 1
                int64_t qty = pos_prod.get_quantity() + pos_change * 100;
                pos_prod.set_quantity(qty);

                if (qty != 0 && pos_prod.get_avg_price() == 0) {
                    pos_prod.set_avg_price(bid);
                }
            }

            // Update P&L
            int64_t qty = pos_prod.get_quantity();
            if (qty != 0) {
                int64_t mark = quote_prod.get_bid_price();
                int64_t avg = pos_prod.get_avg_price();
                pos_prod.set_unrealized_pnl((mark - avg) * qty);
            }
        }

        // Print status every second
        if (tick % 100 == 0) {
            std::cout << "\rTick " << tick << ": ";
            for (size_t i = 0; i < quote_producers.size() && i < 3; ++i) {
                std::cout << symbols[i] << "=" << quote_producers[i].get_bid_price() << " ";
            }
            std::cout << "          " << std::flush;
        }

        ++tick;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "\n\nShutting down...\n";

    // Cleanup
    for (auto* sec : securities) {
        memglass::destroy(sec);
    }

    memglass::shutdown();
    return 0;
}
