import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from mcuforge_test_runner import evaluate, load_case


def sample(phase, elapsed_ms, **payload):
    return {"phase": phase, "phase_elapsed_ms": elapsed_ms, "payload": payload}


class MCUForgeTestRunnerTests(unittest.TestCase):
    def test_ctrl_case_accepts_expected_mixer_output(self):
        case = load_case("CTRL-001")
        results = evaluate(
            case,
            [sample("drive", 150, left_cmd=500, right_cmd=300, cmd_valid=1)],
        )
        self.assertTrue(all(item["passed"] for item in results))

    def test_fs_case_rejects_deliberate_hold_last_baseline(self):
        case = load_case("FS-001")
        results = evaluate(
            case,
            [sample("link_loss", 200, left_cmd=600, right_cmd=600, cmd_valid=1, state="DEMO_BASELINE")],
        )
        self.assertFalse(all(item["passed"] for item in results))

    def test_fs_case_accepts_zeroed_failsafe_samples(self):
        case = load_case("FS-001")
        results = evaluate(
            case,
            [
                sample("link_loss", 200, left_cmd=0, right_cmd=0, cmd_valid=0, state="FAILSAFE"),
                sample("link_loss", 250, left_cmd=0, right_cmd=0, cmd_valid=0, state="FAILSAFE"),
            ],
        )
        self.assertTrue(all(item["passed"] for item in results))

    def test_recovery_case_accepts_three_neutral_frame_contract(self):
        case = load_case("REC-001")
        results = evaluate(
            case,
            [
                sample(
                    "observe_neutral_2",
                    50,
                    left_cmd=0,
                    right_cmd=0,
                    state="FAILSAFE",
                    pc_recovery_neutral_count=2,
                ),
                sample("observe_neutral_3", 50, left_cmd=0, right_cmd=0, state="RUN"),
                sample("drive_after_recovery", 150, left_cmd=300, right_cmd=300, state="RUN"),
            ],
        )
        self.assertTrue(all(item["passed"] for item in results))


if __name__ == "__main__":
    unittest.main()
