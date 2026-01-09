"""
memglass - Python client for the memglass Web API

A simple client for observing memglass sessions via the HTTP/JSON API.

Example usage:
    from memglass import MemglassClient

    client = MemglassClient("http://localhost:8080")

    # Get full session snapshot
    data = client.fetch()
    print(f"Producer PID: {data.pid}")
    print(f"Objects: {len(data.objects)}")

    # Access specific object
    counter = client.get_object("main_counter")
    if counter:
        print(f"Counter value: {counter['value']}")

    # Continuous monitoring
    for snapshot in client.stream(interval=0.5):
        counter = snapshot.get_object("main_counter")
        if counter:
            print(f"Counter: {counter['value']}")
"""

import json
import time
from dataclasses import dataclass, field
from typing import Any, Dict, Iterator, List, Optional, Union
from urllib.request import urlopen, Request
from urllib.error import URLError, HTTPError


@dataclass
class FieldValue:
    """A field value with metadata."""
    name: str
    value: Any
    atomicity: str  # "none", "atomic", "seqlock", "locked"

    @property
    def is_atomic(self) -> bool:
        return self.atomicity == "atomic"

    @property
    def is_seqlock(self) -> bool:
        return self.atomicity == "seqlock"

    @property
    def is_locked(self) -> bool:
        return self.atomicity == "locked"


@dataclass
class TypeInfo:
    """Type metadata."""
    name: str
    type_id: int
    size: int
    field_count: int


@dataclass
class ObjectInfo:
    """An observed object with its current field values."""
    label: str
    type_name: str
    type_id: int
    fields: List[FieldValue] = field(default_factory=list)

    def __getitem__(self, field_name: str) -> Any:
        """Get field value by name. Supports dot notation for nested fields."""
        for f in self.fields:
            if f.name == field_name:
                return f.value
        raise KeyError(f"Field '{field_name}' not found in object '{self.label}'")

    def get(self, field_name: str, default: Any = None) -> Any:
        """Get field value with default."""
        try:
            return self[field_name]
        except KeyError:
            return default

    def get_field(self, field_name: str) -> Optional[FieldValue]:
        """Get full field info including atomicity."""
        for f in self.fields:
            if f.name == field_name:
                return f
        return None

    @property
    def field_names(self) -> List[str]:
        """Get all field names."""
        return [f.name for f in self.fields]

    def to_dict(self) -> Dict[str, Any]:
        """Convert to a plain dictionary of field values."""
        return {f.name: f.value for f in self.fields}


@dataclass
class Snapshot:
    """A point-in-time snapshot of a memglass session."""
    pid: int
    sequence: int
    types: List[TypeInfo] = field(default_factory=list)
    objects: List[ObjectInfo] = field(default_factory=list)

    def get_object(self, label: str) -> Optional[ObjectInfo]:
        """Find an object by label."""
        for obj in self.objects:
            if obj.label == label:
                return obj
        return None

    def get_objects_by_type(self, type_name: str) -> List[ObjectInfo]:
        """Get all objects of a given type."""
        return [obj for obj in self.objects if obj.type_name == type_name]

    def get_type(self, name: str) -> Optional[TypeInfo]:
        """Find type info by name."""
        for t in self.types:
            if t.name == name:
                return t
        return None

    @property
    def object_labels(self) -> List[str]:
        """Get all object labels."""
        return [obj.label for obj in self.objects]

    @property
    def type_names(self) -> List[str]:
        """Get all type names."""
        return [t.name for t in self.types]


class MemglassError(Exception):
    """Base exception for memglass client errors."""
    pass


class ConnectionError(MemglassError):
    """Failed to connect to the memglass server."""
    pass


class MemglassClient:
    """
    Client for the memglass Web API.

    Args:
        url: Base URL of the memglass web server (e.g., "http://localhost:8080")
        timeout: Request timeout in seconds (default: 5.0)

    Example:
        client = MemglassClient("http://localhost:8080")
        data = client.fetch()
        print(f"PID: {data.pid}, Objects: {len(data.objects)}")
    """

    def __init__(self, url: str = "http://localhost:8080", timeout: float = 5.0):
        self.url = url.rstrip("/")
        self.timeout = timeout
        self._last_sequence: Optional[int] = None

    def fetch(self) -> Snapshot:
        """
        Fetch current session state.

        Returns:
            Snapshot containing all types and objects with their current values.

        Raises:
            ConnectionError: If the server is unreachable.
            MemglassError: If the response is invalid.
        """
        try:
            req = Request(f"{self.url}/api/data")
            with urlopen(req, timeout=self.timeout) as response:
                data = json.loads(response.read().decode("utf-8"))
        except HTTPError as e:
            raise MemglassError(f"HTTP error {e.code}: {e.reason}")
        except URLError as e:
            raise ConnectionError(f"Failed to connect to {self.url}: {e.reason}")
        except json.JSONDecodeError as e:
            raise MemglassError(f"Invalid JSON response: {e}")

        return self._parse_snapshot(data)

    def _parse_snapshot(self, data: Dict) -> Snapshot:
        """Parse JSON response into Snapshot."""
        types = [
            TypeInfo(
                name=t["name"],
                type_id=t["type_id"],
                size=t["size"],
                field_count=t["field_count"]
            )
            for t in data.get("types", [])
        ]

        objects = []
        for obj in data.get("objects", []):
            fields = [
                FieldValue(
                    name=f["name"],
                    value=f["value"],
                    atomicity=f.get("atomicity", "none")
                )
                for f in obj.get("fields", [])
            ]
            objects.append(ObjectInfo(
                label=obj["label"],
                type_name=obj["type_name"],
                type_id=obj["type_id"],
                fields=fields
            ))

        snapshot = Snapshot(
            pid=data.get("pid", 0),
            sequence=data.get("sequence", 0),
            types=types,
            objects=objects
        )

        self._last_sequence = snapshot.sequence
        return snapshot

    def get_object(self, label: str) -> Optional[ObjectInfo]:
        """
        Fetch and return a specific object by label.

        Convenience method that fetches the full snapshot and returns
        the requested object.
        """
        return self.fetch().get_object(label)

    def stream(self, interval: float = 0.5) -> Iterator[Snapshot]:
        """
        Continuously stream snapshots.

        Args:
            interval: Time between fetches in seconds (default: 0.5)

        Yields:
            Snapshot for each fetch cycle.

        Example:
            for snapshot in client.stream(interval=0.1):
                counter = snapshot.get_object("counter")
                if counter:
                    print(counter["value"])
        """
        while True:
            try:
                yield self.fetch()
            except ConnectionError:
                # Server disconnected, wait and retry
                pass
            time.sleep(interval)

    def stream_changes(self, interval: float = 0.5) -> Iterator[Snapshot]:
        """
        Stream only when sequence number changes.

        More efficient than stream() when you only care about structural
        changes (new objects, removed objects, type changes).

        Args:
            interval: Polling interval in seconds

        Yields:
            Snapshot only when sequence changes.
        """
        last_seq = None
        while True:
            try:
                snapshot = self.fetch()
                if snapshot.sequence != last_seq:
                    last_seq = snapshot.sequence
                    yield snapshot
            except ConnectionError:
                pass
            time.sleep(interval)

    def wait_for_producer(self, timeout: float = 30.0, poll_interval: float = 0.5) -> bool:
        """
        Wait for the producer to become available.

        Args:
            timeout: Maximum time to wait in seconds
            poll_interval: Time between connection attempts

        Returns:
            True if connected, False if timeout reached.
        """
        start = time.time()
        while time.time() - start < timeout:
            try:
                self.fetch()
                return True
            except ConnectionError:
                time.sleep(poll_interval)
        return False

    @property
    def last_sequence(self) -> Optional[int]:
        """Last seen sequence number, or None if never fetched."""
        return self._last_sequence

    def __repr__(self) -> str:
        return f"MemglassClient({self.url!r})"


# Convenience function for one-off fetches
def fetch(url: str = "http://localhost:8080", timeout: float = 5.0) -> Snapshot:
    """
    One-shot fetch of memglass session state.

    Args:
        url: memglass web server URL
        timeout: Request timeout in seconds

    Returns:
        Snapshot of current session state.
    """
    return MemglassClient(url, timeout).fetch()


if __name__ == "__main__":
    import argparse
    import sys

    parser = argparse.ArgumentParser(description="memglass Python client")
    parser.add_argument("url", nargs="?", default="http://localhost:8080",
                        help="memglass web server URL (default: http://localhost:8080)")
    parser.add_argument("-w", "--watch", action="store_true",
                        help="Continuously watch for changes")
    parser.add_argument("-i", "--interval", type=float, default=0.5,
                        help="Watch interval in seconds (default: 0.5)")
    parser.add_argument("-j", "--json", action="store_true",
                        help="Output raw JSON")
    parser.add_argument("-o", "--object", dest="object_label",
                        help="Show only this object")

    args = parser.parse_args()

    client = MemglassClient(args.url)

    def print_snapshot(snap: Snapshot):
        if args.json:
            output = {
                "pid": snap.pid,
                "sequence": snap.sequence,
                "types": [{"name": t.name, "type_id": t.type_id, "size": t.size, "field_count": t.field_count} for t in snap.types],
                "objects": [{"label": o.label, "type_name": o.type_name, "fields": [{"name": f.name, "value": f.value, "atomicity": f.atomicity} for f in o.fields]} for o in snap.objects]
            }
            print(json.dumps(output, indent=2))
        else:
            print(f"PID: {snap.pid}  Sequence: {snap.sequence}  Objects: {len(snap.objects)}")
            print("-" * 60)

            for obj in snap.objects:
                if args.object_label and obj.label != args.object_label:
                    continue

                print(f"{obj.label} ({obj.type_name})")
                for f in obj.fields:
                    atomicity = f" [{f.atomicity}]" if f.atomicity != "none" else ""
                    print(f"  {f.name:30} = {f.value}{atomicity}")
                print()

    try:
        if args.watch:
            print(f"Watching {args.url} (Ctrl+C to stop)...\n")
            for snap in client.stream(interval=args.interval):
                print("\033[2J\033[H", end="")  # Clear screen
                print_snapshot(snap)
        else:
            snap = client.fetch()
            print_snapshot(snap)
    except ConnectionError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nStopped.")
        sys.exit(0)
