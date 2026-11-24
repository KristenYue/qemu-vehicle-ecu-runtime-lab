#!/usr/bin/env python3
from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("run_qemu_arm64_evidence.py")
SPEC = importlib.util.spec_from_file_location("run_qemu_arm64_evidence", SCRIPT)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"cannot load {SCRIPT}")
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class QemuEvidenceTest(unittest.TestCase):
    def test_command_disables_implicit_network_device(self) -> None:
        command = MODULE.build_qemu_command(
            "qemu-system-aarch64", Path("images/Image"), Path("images/rootfs.ext2")
        )

        nic_index = command.index("-nic")
        self.assertEqual(command[nic_index + 1], "none")
        self.assertEqual(command[command.index("-M") + 1], "virt")
        self.assertEqual(command[command.index("-cpu") + 1], "cortex-a53")
        expected_drive = f"file={Path('images/rootfs.ext2')},if=none,format=raw,id=hd0"
        self.assertIn(expected_drive, command)

    def test_event_parser_preserves_recovery_fields(self) -> None:
        serial = (
            "ts_ms=100 event=unhealthy app=faultable reason=heartbeat_timeout\n"
            "ts_ms=302 event=ready app=faultable pid=42\n"
        )

        events = MODULE.parse_events(serial)

        self.assertEqual(events[0]["reason"], "heartbeat_timeout")
        self.assertEqual(events[1]["event"], "ready")
        self.assertEqual(events[1]["pid"], "42")


if __name__ == "__main__":
    unittest.main()
