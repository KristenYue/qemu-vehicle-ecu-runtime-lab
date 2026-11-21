#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


EVENT_RE = re.compile(r"ts_ms=(?P<ts>\d+) event=(?P<event>\S+)(?P<fields>.*)")
FIELD_RE = re.compile(r"(?P<key>[a-z_]+)=(?P<value>\S+)")


def parse_events(output: str) -> list[dict[str, object]]:
    events: list[dict[str, object]] = []
    for line in output.splitlines():
        match = EVENT_RE.search(line)
        if not match:
            continue
        event: dict[str, object] = {
            "ts_ms": int(match.group("ts")),
            "event": match.group("event"),
        }
        for field in FIELD_RE.finditer(match.group("fields")):
            event[field.group("key")] = field.group("value")
        events.append(event)
    return events


def event_index(events: list[dict[str, object]], name: str, **fields: str) -> int:
    for index, event in enumerate(events):
        if event.get("event") == name and all(event.get(key) == value for key, value in fields.items()):
            return index
    raise AssertionError(f"missing event {name} with {fields}")


def run_case(manager: Path, manifest: Path, state_file: Path | None = None) -> dict[str, object]:
    if state_file is not None:
        state_file.unlink(missing_ok=True)
    completed = subprocess.run(
        [str(manager), str(manifest)],
        cwd=manager.parent,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=15,
        check=False,
    )
    combined = completed.stdout
    events = parse_events(combined)
    if completed.returncode != 0:
        raise AssertionError(f"manager returned {completed.returncode}:\n{combined}")
    return {"returncode": completed.returncode, "events": events, "raw_output": combined}


def validate_normal(result: dict[str, object]) -> None:
    events = result["events"]
    assert isinstance(events, list)
    health_ready = event_index(events, "ready", app="health-service")
    client_started = event_index(events, "started", app="vehicle-client")
    if health_ready >= client_started:
        raise AssertionError("dependent client started before health service was ready")


def recovery_metrics(result: dict[str, object], fault_event: str) -> dict[str, int]:
    events = result["events"]
    assert isinstance(events, list)
    fault_index = event_index(events, fault_event, app="recovery-service")
    restart_index = event_index(events, "restart_scheduled", app="recovery-service")
    ready_candidates = [
        event for index, event in enumerate(events)
        if index > restart_index and event.get("event") == "ready" and event.get("app") == "recovery-service"
    ]
    if not ready_candidates:
        raise AssertionError("service never returned to ready after restart")
    fault_ts = int(events[fault_index]["ts_ms"])
    restart_ts = int(events[restart_index]["ts_ms"])
    ready_ts = int(ready_candidates[0]["ts_ms"])
    return {
        "fault_to_restart_schedule_ms": restart_ts - fault_ts,
        "fault_to_recovered_ready_ms": ready_ts - fault_ts,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manager", type=Path, required=True)
    parser.add_argument("--config-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    crash_state = Path("/tmp/vecu-crash-once.state")
    hang_state = Path("/tmp/vecu-hang-once.state")
    normal = run_case(args.manager, args.config_dir / "apps.manifest")
    validate_normal(normal)
    crash = run_case(args.manager, args.config_dir / "crash-recovery.manifest", crash_state)
    hang = run_case(args.manager, args.config_dir / "hang-recovery.manifest", hang_state)

    report = {
        "schema_version": 1,
        "environment": "linux-x86_64",
        "cases": {
            "dependency_ready_order": {"passed": True, "events": normal["events"]},
            "crash_once_recovery": {
                "passed": True,
                "metrics": recovery_metrics(crash, "exited"),
                "events": crash["events"],
            },
            "hang_once_recovery": {
                "passed": True,
                "metrics": recovery_metrics(hang, "unhealthy"),
                "events": hang["events"],
            },
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": True, "output": str(args.output)}, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

