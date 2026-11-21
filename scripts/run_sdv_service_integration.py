#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import queue
import re
import signal
import socket
import subprocess
import tempfile
import threading
import time
from pathlib import Path


EVENT_RE = re.compile(r"ts_ms=(?P<ts>\d+) event=(?P<event>\S+)(?P<fields>.*)")
FIELD_RE = re.compile(r"(?P<key>[a-z_]+)=(?P<value>\S+)")


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as candidate:
        candidate.bind(("127.0.0.1", 0))
        return int(candidate.getsockname()[1])


def parse_event(line: str) -> dict[str, object] | None:
    match = EVENT_RE.search(line)
    if not match:
        return None
    event: dict[str, object] = {"ts_ms": int(match.group("ts")), "event": match.group("event")}
    for field in FIELD_RE.finditer(match.group("fields")):
        event[field.group("key")] = field.group("value")
    return event


def read_output(process: subprocess.Popen[str], lines: queue.Queue[str]) -> None:
    assert process.stdout is not None
    for line in process.stdout:
        lines.put(line.rstrip("\n"))


def wait_for_event(
    lines: queue.Queue[str], events: list[dict[str, object]], raw: list[str],
    name: str, occurrence: int, timeout: float = 8.0,
) -> dict[str, object]:
    deadline = time.monotonic() + timeout
    found = sum(1 for event in events if event.get("event") == name)
    while time.monotonic() < deadline:
        try:
            line = lines.get(timeout=min(0.1, deadline - time.monotonic()))
        except queue.Empty:
            continue
        raw.append(line)
        parsed = parse_event(line)
        if parsed is not None:
            events.append(parsed)
            if parsed.get("event") == name:
                found += 1
                if found >= occurrence:
                    return parsed
    raise TimeoutError(f"timed out waiting for {name} occurrence {occurrence}; output={raw}")


def call_client(client: Path, port: int) -> dict[str, object]:
    completed = subprocess.run(
        [str(client), str(port)], text=True, capture_output=True, timeout=5, check=False
    )
    if completed.returncode != 0:
        raise AssertionError(f"vehicle client failed: {completed.stderr}")
    response = json.loads(completed.stdout)
    if response.get("speed_kph") != 123.45 or response.get("battery_percent") != 75:
        raise AssertionError(f"unexpected vehicle snapshot: {response}")
    return response


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manager", type=Path, required=True)
    parser.add_argument("--adapter", type=Path, required=True)
    parser.add_argument("--sdv-server", type=Path, required=True)
    parser.add_argument("--sdv-client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    port = free_udp_port()
    with tempfile.TemporaryDirectory(prefix="vecu-sdv-") as temporary:
        temp = Path(temporary)
        manifest = temp / "sdv.manifest"
        journal = temp / "runtime.journal"
        command = " ".join(
            [str(args.adapter), "READY", "--", str(args.sdv_server), str(port),
             "snapshot", str(journal), "2"]
        )
        manifest.write_text(
            "[vehicle-state-service]\n"
            f"command={command}\n"
            "depends=\nrestart=on-failure\nmax_restarts=2\n"
            "restart_backoff_ms=100\nstart_timeout_ms=2000\nheartbeat_timeout_ms=500\n",
            encoding="utf-8",
        )

        process = subprocess.Popen(
            [str(args.manager), str(manifest)], cwd=args.manager.parent,
            text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, bufsize=1,
        )
        lines: queue.Queue[str] = queue.Queue()
        reader = threading.Thread(target=read_output, args=(process, lines), daemon=True)
        reader.start()
        events: list[dict[str, object]] = []
        raw: list[str] = []
        try:
            first_ready = wait_for_event(lines, events, raw, "ready", 1)
            first_started = next(event for event in events if event.get("event") == "started")
            first_response = call_client(args.sdv_client, port)
            kill_at_ms = time.monotonic_ns() // 1_000_000
            os.kill(int(str(first_started["pid"])), signal.SIGKILL)
            second_ready = wait_for_event(lines, events, raw, "ready", 2)
            second_response = call_client(args.sdv_client, port)
            final_response = call_client(args.sdv_client, port)
            returncode = process.wait(timeout=8)
            reader.join(timeout=1)
            while not lines.empty():
                line = lines.get_nowait()
                raw.append(line)
                parsed = parse_event(line)
                if parsed is not None:
                    events.append(parsed)
            if returncode != 0:
                raise AssertionError(f"Execution Manager returned {returncode}: {raw}")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=2)

    report = {
        "schema_version": 1,
        "environment": "linux-x86_64",
        "managed_service": "sdv-communication-integration-lab/sdv_someip_vehicle_server",
        "port": port,
        "passed": True,
        "metrics": {
            "first_ready_ts_ms": int(first_ready["ts_ms"]),
            "adapter_kill_ts_ms": kill_at_ms,
            "recovered_ready_ts_ms": int(second_ready["ts_ms"]),
            "kill_to_recovered_ready_ms": int(second_ready["ts_ms"]) - kill_at_ms,
        },
        "responses": [first_response, second_response, final_response],
        "events": events,
        "raw_output": raw,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": True, "kill_to_ready_ms": report["metrics"]["kill_to_recovered_ready_ms"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

