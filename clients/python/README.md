# memglass Python Client

A Python client for the memglass Web API.

## Requirements

- Python 3.7+
- No external dependencies (uses only stdlib)

## Installation

Copy `memglass.py` to your project, or add the `clients/python` directory to your Python path.

## Quick Start

```python
from memglass import MemglassClient

# Connect to memglass web server
client = MemglassClient("http://localhost:8080")

# Fetch current state
snapshot = client.fetch()
print(f"Producer PID: {snapshot.pid}")
print(f"Sequence: {snapshot.sequence}")
print(f"Objects: {snapshot.object_labels}")

# Access a specific object
counter = snapshot.get_object("main_counter")
if counter:
    print(f"Counter value: {counter['value']}")
    print(f"Timestamp: {counter['timestamp']}")

# Get all fields as a dict
print(counter.to_dict())
```

## Continuous Monitoring

```python
# Stream snapshots at 500ms intervals
for snapshot in client.stream(interval=0.5):
    counter = snapshot.get_object("main_counter")
    if counter:
        print(f"Counter: {counter['value']}")
```

## Command-Line Usage

The module can be run directly:

```bash
# One-shot fetch
python memglass.py http://localhost:8080

# Watch mode (continuous updates)
python memglass.py -w http://localhost:8080

# Custom interval
python memglass.py -w -i 0.1 http://localhost:8080

# JSON output
python memglass.py -j http://localhost:8080

# Show only one object
python memglass.py -o main_counter http://localhost:8080
```

## API Reference

### MemglassClient

```python
client = MemglassClient(url="http://localhost:8080", timeout=5.0)
```

**Methods:**

| Method | Description |
|--------|-------------|
| `fetch()` | Fetch current snapshot |
| `get_object(label)` | Fetch and return specific object |
| `stream(interval)` | Yield snapshots continuously |
| `stream_changes(interval)` | Yield only when sequence changes |
| `wait_for_producer(timeout)` | Wait for server to become available |

### Snapshot

```python
snapshot = client.fetch()
```

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| `pid` | int | Producer process ID |
| `sequence` | int | Sequence number (increments on structural changes) |
| `types` | List[TypeInfo] | Registered types |
| `objects` | List[ObjectInfo] | All observed objects |
| `object_labels` | List[str] | All object labels |
| `type_names` | List[str] | All type names |

**Methods:**

| Method | Description |
|--------|-------------|
| `get_object(label)` | Find object by label |
| `get_objects_by_type(type_name)` | Get all objects of a type |
| `get_type(name)` | Find type info by name |

### ObjectInfo

```python
obj = snapshot.get_object("main_counter")
```

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| `label` | str | Object label/name |
| `type_name` | str | Type name |
| `type_id` | int | Type ID |
| `fields` | List[FieldValue] | Field values |
| `field_names` | List[str] | All field names |

**Methods:**

| Method | Description |
|--------|-------------|
| `obj["field"]` | Get field value (KeyError if not found) |
| `obj.get("field", default)` | Get field value with default |
| `obj.get_field("field")` | Get FieldValue with metadata |
| `obj.to_dict()` | Convert to plain dict |

### FieldValue

```python
field = obj.get_field("value")
```

**Properties:**

| Property | Type | Description |
|----------|------|-------------|
| `name` | str | Field name |
| `value` | Any | Current value |
| `atomicity` | str | "none", "atomic", "seqlock", "locked" |
| `is_atomic` | bool | True if atomicity is "atomic" |
| `is_seqlock` | bool | True if atomicity is "seqlock" |
| `is_locked` | bool | True if atomicity is "locked" |

## Examples

### Monitor Trading Data

```python
from memglass import MemglassClient
import time

client = MemglassClient("http://trading-server:8080")

# Wait for producer
if not client.wait_for_producer(timeout=30):
    print("Producer not available")
    exit(1)

# Monitor quotes
for snapshot in client.stream(interval=0.1):
    for obj in snapshot.get_objects_by_type("Security"):
        bid = obj.get("quote.bid_price", 0)
        ask = obj.get("quote.ask_price", 0)
        print(f"{obj.label}: {bid} / {ask}")
```

### Log Value Changes

```python
from memglass import MemglassClient

client = MemglassClient()
previous = {}

for snapshot in client.stream(interval=0.5):
    counter = snapshot.get_object("main_counter")
    if counter:
        value = counter["value"]
        if previous.get("value") != value:
            print(f"Counter changed: {previous.get('value')} -> {value}")
            previous["value"] = value
```

### Remote Access via SSH Tunnel

```bash
# Set up tunnel
ssh -L 8080:localhost:8080 user@remote-server

# Then use Python client locally
python -c "from memglass import fetch; print(fetch())"
```

## Error Handling

```python
from memglass import MemglassClient, ConnectionError, MemglassError

client = MemglassClient()

try:
    snapshot = client.fetch()
except ConnectionError as e:
    print(f"Cannot connect: {e}")
except MemglassError as e:
    print(f"API error: {e}")
```
