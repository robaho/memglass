# Quick Start Guide

This guide will get you up and running with memglass in 5 minutes.

## Prerequisites

- C++20 compiler (GCC 10+, Clang 12+)
- CMake 3.16+
- libclang-dev (for the code generator)

```bash
# Ubuntu/Debian
sudo apt install libclang-dev

# Fedora
sudo dnf install clang-devel

# macOS
brew install llvm
```

## Building

```bash
git clone https://github.com/your-repo/memglass
cd memglass
mkdir build && cd build
cmake ..
make
```

## Step 1: Define Your Types

Create a header with POD structs marked for observation:

```cpp
// my_types.hpp
#pragma once
#include <cstdint>

struct [[memglass::observe]] Counter {
    uint64_t value;      // @atomic
    uint64_t timestamp;
};

struct [[memglass::observe]] Stats {
    int64_t min_value;
    int64_t max_value;
    double average;
    uint32_t count;
};
```

The `[[memglass::observe]]` attribute marks types for automatic reflection. Comments like `// @atomic` specify synchronization behavior.

## Step 2: Generate Registration Code

Run the code generator on your header:

```bash
./build/memglass-gen my_types.hpp -o my_types_generated.hpp
```

This creates type registration functions that memglass uses at runtime.

## Step 3: Write a Producer

```cpp
// producer.cpp
#include <memglass/memglass.hpp>
#include "my_types.hpp"
#include "my_types_generated.hpp"

#include <thread>
#include <chrono>

int main() {
    // Initialize with a session name
    memglass::init("my_app");

    // Register all generated types
    memglass::generated::register_all_types();

    // Create objects in shared memory
    auto* counter = memglass::create<Counter>("main_counter");
    auto* stats = memglass::create<Stats>("global_stats");

    // Initialize
    counter->value = 0;
    counter->timestamp = 0;
    stats->min_value = INT64_MAX;
    stats->max_value = INT64_MIN;
    stats->average = 0.0;
    stats->count = 0;

    // Update loop
    while (true) {
        counter->value++;
        counter->timestamp = std::chrono::steady_clock::now()
            .time_since_epoch().count();

        // Update stats
        int64_t v = counter->value % 1000;
        if (v < stats->min_value) stats->min_value = v;
        if (v > stats->max_value) stats->max_value = v;
        stats->count++;
        stats->average = (stats->average * (stats->count - 1) + v) / stats->count;

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    memglass::shutdown();
    return 0;
}
```

## Step 4: Observe with the TUI

Start your producer in one terminal:

```bash
./producer
```

In another terminal, use the memglass viewer:

```bash
./build/memglass my_app
```

You'll see an interactive tree view:

```
=== Memglass Browser ===
PID: 12345  Objects: 2  Seq: 2
--------------------------------------------------------------------------------
[-] main_counter (Counter)
      value            =         123456 [atomic]
      timestamp        = 1704825432123456789
[+] global_stats (Stats)
--------------------------------------------------------------------------------
h/? for help | q to quit
```

**Controls:**
- `↑/↓` or `j/k` - Navigate
- `Enter` or `Space` - Expand/collapse
- `q` - Quit
- `h` or `?` - Toggle help

## Step 5: Or Use the Web UI

For a browser-based view, start the web server:

```bash
./build/memglass --web my_app
```

Then open http://localhost:8080 in your browser. The web UI provides:
- Auto-refresh with value change highlighting
- Expandable/collapsible tree view
- Atomicity badges (atomic, seqlock, locked)
- Dark theme optimized for monitoring

You can also specify a custom port:

```bash
./build/memglass --web 9000 my_app
```

### Remote Access via SSH Tunnel

To access the web UI from a remote server (e.g., colocation), use SSH port forwarding:

```bash
# Forward remote port 8080 to local port 8080
ssh -L 8080:localhost:8080 user@remote-server

# Then open http://localhost:8080 in your local browser
```

For background tunneling (no interactive shell):

```bash
ssh -fNL 8080:localhost:8080 user@remote-server
```

Or add to your `~/.ssh/config`:

```
Host myserver-memglass
    HostName remote-server
    User your-user
    LocalForward 8080 localhost:8080
```

Then just: `ssh myserver-memglass`

## Step 6: Write a Custom Observer (Optional)

For programmatic access:

```cpp
// observer.cpp
#include <memglass/observer.hpp>
#include <iostream>
#include <thread>

int main() {
    memglass::Observer obs("my_app");

    if (!obs.connect()) {
        std::cerr << "Failed to connect\n";
        return 1;
    }

    while (true) {
        auto counter = obs.find("main_counter");
        if (counter) {
            uint64_t value = counter["value"];
            std::cout << "Counter: " << value << "\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
```

## Step 7: Use the Python Client (Optional)

For quick scripts or integration with Python tools:

```python
from memglass import MemglassClient

client = MemglassClient("http://localhost:8080")
snapshot = client.fetch()

counter = snapshot.get_object("main_counter")
if counter:
    print(f"Counter: {counter['value']}")

# Continuous monitoring
for snapshot in client.stream(interval=0.5):
    counter = snapshot.get_object("main_counter")
    print(f"Value: {counter['value']}")
```

See [clients/python/README.md](../clients/python/README.md) for full documentation.

## Step 8: Record and Replay State Changes

Use `memglass-diff` to capture and analyze state changes over time:

```bash
# Stream live changes to terminal (1 second interval)
./build/memglass-diff my_app

# Record at 100ms intervals to a binary file
./build/memglass-diff -i 100 -f binary -o session.mgd my_app
```

Let it run while your producer is active, then press Ctrl+C to stop recording.

**Replay the recording:**

```bash
# Decode binary file to readable text
./build/memglass-diff --decode session.mgd
```

Example output:

```
@1704825432123456789 seq:41->42
  main_counter.value: 123 -> 124
  main_counter.timestamp: 1704825432100000000 -> 1704825432200000000
@1704825432223456789 seq:42->43
  main_counter.value: 124 -> 125
  global_stats.count: 123 -> 124
```

For JSON output (useful with `jq`):

```bash
./build/memglass-diff -f json my_app | jq '.changes[]'
```

See [memglass-diff documentation](memglass-diff.md) for all options and the binary format specification.

## Next Steps

- Read the [Advanced Guide](advanced.md) for nested structs, synchronization options, and more
- See the [API Reference](api-reference.md) for complete documentation
- See the [Python Client](../clients/python/README.md) for scripting and automation
- Check the [Architecture](architecture.md) for understanding the internals
