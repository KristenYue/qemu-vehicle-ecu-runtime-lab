#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import shlex
import subprocess
from pathlib import Path


EVENT_RE = re.compile(r"ts_ms=(?P<ts>\d+) event=(?P<event>\S+)(?P<fields>.*)")
FIELD_RE = re.compile(r"(?P<key>[a-z_]+)=(?P<value>\S+)")


def parse_events(output: str) -> list[dict[str, object]]:
    events: list[dict[str, object]] = []
    for line in output.splitlines():
        match = EVENT_RE.search(line)
        if match is None:
            continue
        event: dict[str, object] = {"ts_ms": int(match.group("ts")), "event": match.group("event")}
        for field in FIELD_RE.finditer(match.group("fields")):
            event[field.group("key")] = field.group("value")
        events.append(event)
    return events


def require_event(events: list[dict[str, object]], name: str, occurrence: int = 1) -> dict[str, object]:
    matches = [event for event in events if event.get("event") == name]
    if len(matches) < occurrence:
        raise AssertionError(f"missing event {name} occurrence {occurrence}: {events}")
    return matches[occurrence - 1]


def build_qemu_command(qemu: str, kernel: Path, rootfs: Path) -> list[str]:
    return [
        qemu,
        "-M", "virt",
        "-cpu", "cortex-a53",
        "-smp", "2",
        "-m", "512",
        "-nographic",
        # The evidence guest is self-contained. Disable QEMU's implicit NIC
        # instead of depending on the host's optional efi-virtio.rom package.
        "-nic", "none",
        "-no-reboot",
        "-kernel", str(kernel),
        "-append", "rootwait root=/dev/vda console=ttyAMA0",
        "-drive", f"file={rootfs},if=none,format=raw,id=hd0",
        "-device", "virtio-blk-device,drive=hd0",
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--images", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--qemu", default="qemu-system-aarch64")
    args = parser.parse_args()

    kernel = args.images / "Image"
    rootfs_candidates = [args.images / "rootfs.ext2", args.images / "rootfs.ext4"]
    rootfs = next((candidate for candidate in rootfs_candidates if candidate.exists()), None)
    if not kernel.exists() or rootfs is None:
        raise FileNotFoundError(f"missing Buildroot images in {args.images}")

    command = build_qemu_command(args.qemu, kernel, rootfs)
    completed = subprocess.run(
        command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
        timeout=120, check=False,
    )
    serial = completed.stdout

    def fail(message: str) -> None:
        failure = {
            "schema_version": 1,
            "passed": False,
            "failure": message,
            "qemu_returncode": completed.returncode,
            "qemu_command": command,
            "serial_output": serial,
        }
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(failure, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"QEMU_COMMAND={shlex.join(command)}")
        print(f"QEMU_RETURNCODE={completed.returncode}")
        print("QEMU_SERIAL_BEGIN")
        print(serial)
        print("QEMU_SERIAL_END")
        raise AssertionError(message)

    if "VECU_ARCH=aarch64" not in serial:
        fail("guest did not report aarch64 architecture")
    if "VECU_EVIDENCE_COMPLETE" not in serial or "VECU_MANAGER_EXIT=0" not in serial:
        fail("guest evidence did not complete")

    events = parse_events(serial)
    first_ready = require_event(events, "ready", 1)
    unhealthy = require_event(events, "unhealthy", 1)
    restarted = require_event(events, "started", 2)
    second_ready = require_event(events, "ready", 2)
    exited = require_event(events, "exited", 2)
    if unhealthy.get("reason") != "heartbeat_timeout":
        raise AssertionError(f"unexpected unhealthy reason: {unhealthy}")
    if exited.get("exit_code") != "0":
        raise AssertionError(f"recovered application did not exit cleanly: {exited}")

    report = {
        "schema_version": 1,
        "environment": "qemu-system-aarch64-tcg",
        "machine": "virt",
        "cpu": "cortex-a53",
        "buildroot_version": "2026.08.2",
        "passed": True,
        "metrics": {
            "first_ready_ts_ms": int(first_ready["ts_ms"]),
            "unhealthy_ts_ms": int(unhealthy["ts_ms"]),
            "restarted_ts_ms": int(restarted["ts_ms"]),
            "recovered_ready_ts_ms": int(second_ready["ts_ms"]),
            "unhealthy_to_recovered_ready_ms": int(second_ready["ts_ms"]) - int(unhealthy["ts_ms"]),
        },
        "events": events,
        "qemu_returncode": completed.returncode,
        "serial_markers": [
            line for line in serial.splitlines()
            if "VECU_" in line or " event=" in line
        ],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": True, "unhealthy_to_ready_ms": report["metrics"]["unhealthy_to_recovered_ready_ms"]}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

