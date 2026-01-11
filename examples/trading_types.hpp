#pragma once

#include <cstdint>

// Trading types for memglass example
// These would normally be processed by memglass-gen
//
// Annotation guide:
//   @atomic   - For primitive fields (int64_t, etc.) that need atomic access.
//               Observer uses std::atomic load/store semantics.
//   @seqlock  - For compound struct fields where producer uses Guarded<T>.
//               Observer spins until consistent read obtained.
//   @locked   - For fields where producer uses Locked<T> spinlock.
//   @readonly - Field is never modified after initialization.
//   @enum()   - Maps integer values to named constants for display.

struct [[memglass::observe]] Quote {
    int64_t bid_price;      // @atomic - Price in ticks
    int64_t ask_price;      // @atomic
    uint32_t bid_size;      // @atomic
    uint32_t ask_size;      // @atomic
    uint64_t timestamp_ns;  // @atomic
};

struct [[memglass::observe]] Position {
    uint32_t symbol_id;
    int64_t quantity;       // @atomic
    int64_t avg_price;
    int64_t realized_pnl;
    int64_t unrealized_pnl;
};

struct [[memglass::observe]] Order {
    uint64_t order_id;      // @readonly
    uint32_t symbol_id;
    int64_t price;
    uint32_t quantity;
    uint32_t filled_qty;
    int8_t side;            // @enum(BUY=1, SELL=-1)
    int8_t status;          // @enum(PENDING=0, OPEN=1, FILLED=2, CANCELLED=3)
    int8_t padding[2];
};

// Security combines Quote and Position for a single symbol
struct [[memglass::observe]] Security {
    Quote quote;
    Position position;
};
