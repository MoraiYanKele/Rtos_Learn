"""Protocol regression tests; generated logs are synthetic, never board evidence."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from rtos_test_runner import Suite, expected_cases


class ProtocolTests(unittest.TestCase):
    def make_log(self, failing=None):
        ids = expected_cases()
        records = [f"RTOS_TEST START version=1 total={len(ids)}"]
        for case in ids:
            records += [f"RTOS_TEST BEGIN id={case}",
                        f"RTOS_TEST CASE id={case} status={'FAIL' if case == failing else 'PASS'} actual=0 expected=0"]
        count = 1 if failing else 0
        records += [f"RTOS_TEST SUMMARY status={'FAIL' if count else 'PASS'} passed={len(ids)-count} failed={count} total={len(ids)}"]
        return records

    def parse(self, records):
        suite = Suite(expected_cases())
        for record in records:
            suite.feed(record)
        return suite.finish()

    def test_complete_pass(self):
        self.assertEqual(self.parse(self.make_log())["status"], "PASS")

    def test_real_case_failure(self):
        case = expected_cases()[-1]
        report = self.parse(self.make_log(case))
        self.assertEqual(report["status"], "FAIL")
        self.assertEqual(report["failed_cases"], [case])

    def test_missing_result_is_not_pass(self):
        log = self.make_log()
        del log[-2]
        self.assertEqual(self.parse(log)["status"], "FAIL")

    def test_duplicate_result_is_not_pass(self):
        log = self.make_log()
        log.insert(3, log[2])
        self.assertEqual(self.parse(log)["status"], "FAIL")

    def test_forged_summary_counts(self):
        log = self.make_log()
        log[-1] = "RTOS_TEST SUMMARY status=PASS passed=0 failed=0 total=25"
        self.assertEqual(self.parse(log)["status"], "FAIL")

    def test_timeout_identifies_hung_case(self):
        log = self.make_log()[:2]
        report = self.parse(log)
        self.assertEqual(report["status"], "FAIL")
        self.assertEqual(report["last_started_case"], expected_cases()[0])

    def test_reset_mid_run(self):
        log = self.make_log()
        log.insert(3, "RTOS_TEST READY version=1 total=25")
        self.assertEqual(self.parse(log)["status"], "FAIL")

    def test_wrong_firmware_case_count(self):
        log = self.make_log()
        log[0] = "RTOS_TEST START version=1 total=24"
        self.assertEqual(self.parse(log)["status"], "FAIL")

    def test_unknown_status(self):
        log = self.make_log()
        log[2] = log[2].replace("status=PASS", "status=UNKNOWN")
        self.assertEqual(self.parse(log)["status"], "FAIL")

    def test_two_runs_cannot_be_combined(self):
        self.assertEqual(self.parse(self.make_log() + self.make_log())["status"], "FAIL")

    def test_unrelated_uart_noise_allowed(self):
        log = self.make_log()
        log.insert(1, "hello from UART")
        self.assertEqual(self.parse(log)["status"], "PASS")


if __name__ == "__main__":
    unittest.main()
