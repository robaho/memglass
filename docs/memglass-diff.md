# memglass-diff: Snapshot Diff Tool

`memglass-diff` takes periodic snapshots of a memglass session and outputs the differences (changes) between consecutive snapshots. This is useful for:

- Recording state changes over time for debugging
- Analyzing update patterns and frequencies
- Creating compact logs of field modifications
- Post-mortem analysis of trading/system behavior

## Quick Start

```bash
# Stream text diffs to stdout (1 second interval)
memglass-diff trading_engine

# Record at 100ms intervals to binary file
memglass-diff -i 100 -f binary -o changes.mgd trading_engine

# Decode binary file to text
memglass-diff --decode changes.mgd
```

## Command-Line Options

```
memglass-diff [OPTIONS] <session_name>
memglass-diff --decode <binary_file>

Options:
  -h, --help              Show help message
  -i, --interval <ms>     Snapshot interval in milliseconds (default: 1000)
  -o, --output <file>     Write to file instead of stdout
  -f, --format <fmt>      Output format: text, json, json-pretty, binary
  -a, --all               Include empty diffs (no changes)
  --decode <file>         Decode a binary diff file to text
```

## Output Formats

### Text Format (default)

Compact human-readable format, one diff per block:

```
@1704825432123456789 seq:41->42
  AAPL.quote.bid_price: 15023 -> 15025
  AAPL.quote.timestamp_ns: 1704825432100000000 -> 1704825432200000000
@1704825432223456789 seq:42->43
  AAPL.quote.bid_price: 15025 -> 15024
  MSFT.quote.ask_price: 38010 -> 38012
```

Format breakdown:
- `@<timestamp_ns>` - Nanosecond timestamp of the snapshot
- `seq:<old>-><new>` - Sequence number change
- `+objs:[...]` - Added objects (if any)
- `-objs:[...]` - Removed objects (if any)
- Indented lines show field changes: `object.field: old -> new`

### JSON Format

One JSON object per line (JSONL), suitable for processing with `jq`:

```json
{"timestamp_ns":1704825432123456789,"old_sequence":41,"new_sequence":42,"added":[],"removed":[],"changes":[{"obj":"AAPL","field":"quote.bid_price","old":15023,"new":15025}]}
```

Use with `jq` for filtering:

```bash
memglass-diff -f json trading | jq 'select(.changes | length > 0)'
```

### JSON Pretty Format

Formatted JSON for readability:

```json
{
  "timestamp_ns":1704825432123456789,
  "old_sequence":41,
  "new_sequence":42,
  "added":[],
  "removed":[],
  "changes":[
    {"obj":"AAPL","field":"quote.bid_price","old":15023,"new":15025}
  ]
}
```

### Binary Format

Compact binary format with varint and delta encoding. Ideal for high-frequency recording with minimal disk usage.

**File structure:**
```
Header (8 bytes):
  Magic:    "MGDF" (4 bytes)
  Version:  uint8 (currently 1)
  Flags:    uint8 (reserved)
  Reserved: 2 bytes

Per-record:
  Record type:     uint8 (1=diff, 0=end)
  Timestamp delta: signed varint (ns since last record)
  Sequence:        varint
  Num added:       varint
  Num removed:     varint
  Num changes:     varint
  [Added object names as length-prefixed strings]
  [Removed object names as length-prefixed strings]
  [Field changes: obj_name, field_name, type, value_delta]
```

**Encoding details:**
- Integers use unsigned LEB128 varint encoding
- Signed integers use ZigZag encoding before varint
- Timestamps are delta-encoded (difference from previous)
- Integer field changes store delta from old value (very compact for counters)
- Float/double values are stored as raw bytes

**Decode to text:**
```bash
memglass-diff --decode changes.mgd > changes.txt
```

## Use Cases

### High-Frequency Recording

For trading systems, record at high frequency to capture every update:

```bash
memglass-diff -i 10 -f binary -o session.mgd trading_engine
```

This captures snapshots every 10ms. Binary format keeps file sizes manageable.

### Debugging Specific Objects

Pipe to grep to focus on specific objects:

```bash
memglass-diff trading | grep "AAPL"
```

### JSON Processing Pipeline

```bash
# Count changes per field
memglass-diff -f json trading | jq -r '.changes[].field' | sort | uniq -c | sort -rn

# Find largest price movements
memglass-diff -f json trading | jq '.changes[] | select(.field | endswith("price"))'
```

### Recording with Compression

For long sessions, pipe through compression:

```bash
memglass-diff -f binary trading | gzip > session.mgd.gz
```

### Comparing Different Intervals

Lower interval captures more granular changes but uses more space:

```bash
# 1 second (default) - overview
memglass-diff trading > overview.txt

# 100ms - detailed
memglass-diff -i 100 trading > detailed.txt

# 10ms - high-resolution (use binary)
memglass-diff -i 10 -f binary -o hires.mgd trading
```

## Size Comparison

Approximate sizes for 1 hour of recording with 100 field changes/second:

| Format | Interval | Approx Size |
|--------|----------|-------------|
| text | 1000ms | ~2 MB |
| json | 1000ms | ~4 MB |
| binary | 1000ms | ~0.5 MB |
| binary | 100ms | ~5 MB |
| binary | 10ms | ~50 MB |

Binary format is typically 4-8x smaller than text due to:
- Varint encoding (small numbers use 1-2 bytes)
- Delta encoding (consecutive values often have small differences)
- No field name repetition overhead

## Limitations

- Snapshots are point-in-time; changes between snapshots are not captured
- Very high frequency (< 1ms) may impact system performance
- Binary format requires decoding tool to read
- Object additions/removals are detected but not field additions within types

## See Also

- [memglass CLI](architecture.md#memglass-cli-tool) - Interactive TUI/Web browser
- [Python Client](../clients/python/README.md) - Programmatic access via Web API
- [API Reference](api-reference.md) - C++ Observer API
