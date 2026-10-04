#!/usr/bin/env python3
"""Run or replay the on-target suite. Exit 0 only for a complete, valid PASS."""
from __future__ import annotations
import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
DEFINITION = ROOT / "tests/firmware/rtos_test_cases.def"


def expected_cases() -> list[str]:
    return re.findall(r"^RTOS_CASE\((\w+),", DEFINITION.read_text(encoding="utf-8"), re.M)


class Suite:
    def __init__(self, expected: list[str]):
        self.expected = expected
        self.started = False
        self.summary = None
        self.pending = None
        self.results: list[dict] = []
        self.errors: list[str] = []

    def feed(self, line: str) -> None:
        if not line.startswith("RTOS_TEST "):
            return
        parts = line.split()
        kind = parts[1] if len(parts) > 1 else ""
        try:
            pairs = [item.split("=", 1) for item in parts[2:]]
            if any(len(item) != 2 for item in pairs):
                raise ValueError("malformed fields")
            fields = dict(pairs)
            if len(fields) != len(pairs):
                raise ValueError("duplicate fields")
            if self.summary is not None:
                raise ValueError("records after SUMMARY or a second run in the log")
            if kind == "READY":
                if self.started:
                    raise ValueError("board restarted during the suite")
                self._header(fields)
            elif kind == "START":
                if self.started:
                    raise ValueError("duplicate START or board reset")
                self._header(fields)
                self.started = True
            elif kind == "BEGIN":
                self._require_started()
                case_id = fields["id"]
                index = len(self.results)
                if self.pending or index >= len(self.expected) or case_id != self.expected[index]:
                    raise ValueError(f"unexpected BEGIN {case_id}")
                self.pending = case_id
            elif kind == "CASE":
                self._require_started()
                case_id = fields["id"]
                if case_id != self.pending or fields["status"] not in ("PASS", "FAIL"):
                    raise ValueError(f"unexpected CASE {case_id}")
                actual, expected = int(fields["actual"]), int(fields["expected"])
                if not (0 <= actual <= 0xFFFFFFFF and 0 <= expected <= 0xFFFFFFFF):
                    raise ValueError("invalid measurement")
                self.results.append(dict(id=case_id, status=fields["status"], actual=actual, expected=expected))
                self.pending = None
            elif kind == "ABORT":
                self.errors.append("board aborted: " + fields.get("reason", "unknown"))
            elif kind == "SUMMARY":
                self._require_started()
                self.summary = fields
                passed = sum(r["status"] == "PASS" for r in self.results)
                failed = len(self.results) - passed
                if self.pending or [r["id"] for r in self.results] != self.expected:
                    raise ValueError("missing cases or unfinished BEGIN")
                if (int(fields["passed"]), int(fields["failed"]), int(fields["total"])) != (passed, failed, len(self.expected)):
                    raise ValueError("summary counts do not match the case records")
                if fields["status"] != ("PASS" if failed == 0 else "FAIL"):
                    raise ValueError("summary status does not match the cases")
            else:
                raise ValueError(f"unknown record {kind}")
        except (KeyError, ValueError) as exc:
            self.errors.append(f"{exc}: {line}")

    def _header(self, fields: dict) -> None:
        if fields["version"] != "1" or int(fields["total"]) != len(self.expected):
            raise ValueError("firmware protocol or case count mismatch")

    def _require_started(self) -> None:
        if not self.started:
            raise ValueError("record before START")

    def finish(self) -> dict:
        errors = list(self.errors)
        if self.summary is None:
            errors.append("no SUMMARY: timeout, reset, fault, or incomplete capture")
        missing = [case for case in self.expected if case not in {r["id"] for r in self.results}]
        failed = [r["id"] for r in self.results if r["status"] == "FAIL"]
        return dict(status="PASS" if not errors and not missing and not failed else "FAIL",
                    cases=self.results, failed_cases=failed, missing_cases=missing,
                    last_started_case=self.pending, errors=errors, summary=self.summary)


def capture(args, suite: Suite, output: Path) -> None:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("install pyserial: python -m pip install -r tests/requirements.txt") from exc
    deadline = time.monotonic() + args.timeout
    # Do not toggle DTR/RTS deliberately; reset is done by the flash step or the user.
    port = serial.Serial()
    port.port, port.baudrate, port.timeout = args.port, args.baud, 0.1
    port.dtr = port.rts = False
    port.open()
    try:
        with output.open("w", encoding="utf-8", newline="\n") as raw:
            pending = bytearray()
            sent = False
            while time.monotonic() < deadline and suite.summary is None and not suite.errors:
                pending.extend(port.read(port.in_waiting or 1))
                if len(pending) > 4096:
                    raise RuntimeError("overlong UART line; check baud rate")
                while b"\n" in pending:
                    record, _, rest = pending.partition(b"\n")
                    pending = bytearray(rest)
                    line = record.decode("ascii", errors="replace").rstrip("\r")
                    raw.write(line + "\n")
                    raw.flush()
                    print(line, flush=True)
                    suite.feed(line)
                    if line.startswith("RTOS_TEST READY ") and not sent and not suite.errors:
                        # Firmware polls RX. Pace characters to avoid single-byte UART overrun.
                        for ch in b"RUN\n":
                            port.write(bytes([ch]))
                            port.flush()
                            time.sleep(0.005)
                        sent = True
                if suite.summary is not None:
                    break
            if pending:
                raw.write(pending.decode("ascii", errors="replace"))
    finally:
        port.close()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="e.g. COM3")
    source.add_argument("--log", type=Path, help="replay an existing raw UART log")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=30.0, help="whole run deadline, seconds")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "build/test-results")
    parser.add_argument("--firmware", type=Path, help="host ELF for provenance; defaults to TestDebug for live capture")
    args = parser.parse_args(argv)
    if args.timeout <= 0 or args.baud <= 0:
        parser.error("timeout and baud must be positive")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    raw_path = args.output_dir / f"rtos-{stamp}.log"
    report_path = args.output_dir / f"rtos-{stamp}.json"
    suite = Suite(expected_cases())
    try:
        if args.log:
            for line in args.log.read_text(encoding="utf-8", errors="replace").splitlines():
                suite.feed(line)
            raw_path = args.log.resolve()
        else:
            capture(args, suite, raw_path)
    except (OSError, RuntimeError) as exc:
        suite.errors.append(str(exc))
    report = suite.finish()
    report.update(port=args.port, baud=args.baud, log=str(raw_path), replay=bool(args.log),
                  captured_at=datetime.now().astimezone().isoformat())
    firmware = args.firmware or (None if args.log else ROOT / "build/TestDebug/Rtos_Learn.elf")
    if firmware is not None and firmware.exists():
        report["host_firmware"] = str(firmware.resolve())
        report["host_firmware_sha256"] = hashlib.sha256(firmware.read_bytes()).hexdigest()
    report_path.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{report['status']}: {len(report['cases'])}/{len(suite.expected)} cases, "
          f"{len(report['failed_cases'])} failed")
    for error in report["errors"]:
        print("ERROR: " + error)
    print("Report: " + str(report_path))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
