"""Run fixed black-box tests against the STM32 MCUForge demo over USB CDC."""

from __future__ import annotations

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("缺少 pyserial，请先运行：pip install -r PC_Tools/requirements.txt") from exc

from mcuforge_protocol import pack_control_frame


BAUDRATE = 115200
PROGRAM_DIR = Path(__file__).resolve().parent
CASE_DIR = PROGRAM_DIR / "mcuforge_testcases"
DEFAULT_OUTPUT_DIR = PROGRAM_DIR / "data" / "mcuforge-tests"


def load_case(case_id: str) -> dict:
    path = CASE_DIR / f"{case_id.upper()}.json"
    if not path.exists():
        available = ", ".join(sorted(item.stem for item in CASE_DIR.glob("*.json")))
        raise ValueError(f"找不到测试 {case_id}；可用测试：{available}")
    with path.open("r", encoding="utf-8") as case_file:
        case = json.load(case_file)
    if not case.get("phases") or not case.get("assertions"):
        raise ValueError(f"测试定义不完整：{path}")
    return case


def _compare(actual, operator: str, expected) -> bool:
    if operator == "eq":
        return actual == expected
    if operator == "ne":
        return actual != expected
    if operator == "in":
        return actual in expected
    if actual is None:
        return False
    if operator == "lte":
        return actual <= expected
    if operator == "gte":
        return actual >= expected
    if operator == "abs_lte":
        return abs(actual) <= expected
    raise ValueError(f"未知比较操作：{operator}")


def evaluate(case: dict, samples: list[dict]) -> list[dict]:
    results = []
    for assertion in case["assertions"]:
        phase = assertion["phase"]
        start_after_ms = assertion.get("start_after_ms", 0)
        candidates = [
            sample
            for sample in samples
            if sample["phase"] == phase and sample["phase_elapsed_ms"] >= start_after_ms
        ]
        if "end_before_ms" in assertion:
            candidates = [
                sample for sample in candidates if sample["phase_elapsed_ms"] <= assertion["end_before_ms"]
            ]

        selected = candidates if assertion.get("sample_scope") == "all" else candidates[-1:]
        failures = []
        if not selected:
            failures.append("断言窗口内没有收到遥测")
        for sample in selected:
            payload = sample["payload"]
            for check in assertion["checks"]:
                field = check["field"]
                actual = payload.get(field)
                if not _compare(actual, check.get("op", "eq"), check["value"]):
                    failures.append(
                        f"{field}={actual!r}，期望 {check.get('op', 'eq')} {check['value']!r}"
                    )

        results.append(
            {
                "name": assertion["name"],
                "passed": not failures,
                "sample_count": len(selected),
                "failures": failures,
            }
        )
    return results


def run_case(port: str, case: dict, output_dir: Path) -> tuple[dict, Path]:
    timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    session_dir = output_dir / f"{timestamp}_{case['id']}"
    session_index = 1
    while session_dir.exists():
        session_dir = output_dir / f"{timestamp}_{case['id']}_{session_index:02d}"
        session_index += 1
    session_dir.mkdir(parents=True, exist_ok=False)
    raw_path = session_dir / "telemetry.jsonl"
    result_path = session_dir / "result.json"

    samples = []
    sent_frames = 0
    sequence = 0
    started_at = datetime.now().isoformat(timespec="seconds")

    try:
        with serial.Serial(port, BAUDRATE, timeout=0.005, write_timeout=0.1) as serial_port, raw_path.open(
            "w", encoding="utf-8", newline=""
        ) as raw_file:
            serial_port.reset_input_buffer()
            for phase in case["phases"]:
                phase_start = time.monotonic()
                phase_duration_s = phase["duration_ms"] / 1000.0
                send = phase.get("send")
                send_period_s = phase.get("send_period_ms", 20) / 1000.0
                max_frames = phase.get("max_frames")
                phase_sent = 0
                next_send = phase_start

                while True:
                    now = time.monotonic()
                    if now - phase_start >= phase_duration_s:
                        break

                    if send and now >= next_send and (max_frames is None or phase_sent < max_frames):
                        frame = pack_control_frame(
                            sequence,
                            send.get("throttle", 0),
                            send.get("steering", 0),
                            enabled=send.get("enabled", True),
                            emergency_stop=send.get("emergency_stop", False),
                        )
                        serial_port.write(frame)
                        sequence = (sequence + 1) & 0xFFFF
                        phase_sent += 1
                        sent_frames += 1
                        next_send += send_period_s

                    raw = serial_port.readline()
                    if not raw:
                        continue
                    line = raw.decode("utf-8", errors="ignore").strip()
                    try:
                        payload = json.loads(line)
                    except json.JSONDecodeError:
                        continue
                    sample = {
                        "pc_time": datetime.now().isoformat(timespec="milliseconds"),
                        "phase": phase["name"],
                        "phase_elapsed_ms": round((time.monotonic() - phase_start) * 1000, 3),
                        "payload": payload,
                    }
                    samples.append(sample)
                    raw_file.write(json.dumps(sample, ensure_ascii=False) + "\n")
                    raw_file.flush()
    except serial.SerialException as exc:
        raise RuntimeError(f"无法在 {port} 执行测试：{exc}") from exc

    assertions = evaluate(case, samples)
    result = {
        "case_id": case["id"],
        "title": case["title"],
        "started_at": started_at,
        "port": port,
        "baudrate": BAUDRATE,
        "passed": bool(assertions) and all(item["passed"] for item in assertions),
        "sent_frames": sent_frames,
        "telemetry_samples": len(samples),
        "assertions": assertions,
        "raw_telemetry": raw_path.name,
    }
    with result_path.open("w", encoding="utf-8") as result_file:
        json.dump(result, result_file, ensure_ascii=False, indent=2)
    return result, session_dir


def main() -> int:
    parser = argparse.ArgumentParser(description="MCUForge STM32 固定黑盒测试")
    parser.add_argument("--port", help="STM32 USB CDC 串口，例如 COM3")
    parser.add_argument("--case", default="FS-001", help="测试编号，默认 FS-001")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--list", action="store_true", help="列出固定测试后退出")
    args = parser.parse_args()

    if args.list:
        for case_path in sorted(CASE_DIR.glob("*.json")):
            case = load_case(case_path.stem)
            print(f"{case['id']}: {case['title']}")
        return 0
    if not args.port:
        parser.error("执行硬件测试必须指定 --port")

    try:
        case = load_case(args.case)
        result, session_dir = run_case(args.port, case, args.output_dir.resolve())
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    print(json.dumps(result, ensure_ascii=False, indent=2))
    print(f"证据目录：{session_dir}")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
