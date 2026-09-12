"""Require complete actual Arduino/IDF drv output after a USB middle loss."""
import unittest

import hil_runner as runner


ARDUINO = """=== Driver Health ===
  State: READY
  Online: yes
  Consecutive failures: 0
  Total success: 10
  Total failures: 0
  Success rate: 100.0%
  Last OK: 2 ms ago (at 45 ms)
  Last error: never
> """

IDF = """state=1 initialized=1 online=1 addr=0x50 capacity=32768 variant=MB85RC256V ok=10 fail=0 consecutive=0
transport_timeout_ms=10 max_tx=128 max_rx=128 write_data=126 read_data=128
hs_support=no hs_enabled=no normal_hz=1000000 hs_hz=0 sleep_support=no sleep_state=AWAKE tREC_us=0 wake_ready_ms=0
> """


def verdict(text):
    return runner.classify(runner.CommandStep('health', 'health', 'drv'), text, .01).status


class HealthSnapshotTests(unittest.TestCase):
    def test_complete_current_formats(self):
        for text in (ARDUINO, IDF, '\x1b[32m' + ARDUINO + '\x1b[0m'):
            self.assertEqual(verdict(text), 'PASS')

    def test_every_arduino_health_line_is_required(self):
        lines = ARDUINO.splitlines(keepends=True)
        for index in range(len(lines) - 1):
            with self.subTest(line=index):
                self.assertEqual(verdict(''.join(lines[:index] + lines[index + 1:])), 'FAIL')

    def test_every_idf_health_line_and_field_is_required(self):
        lines = IDF.splitlines(keepends=True)
        for index in range(len(lines) - 1):
            self.assertEqual(verdict(''.join(lines[:index] + lines[index + 1:])), 'FAIL')
            for token in lines[index].strip().split():
                with self.subTest(field=token.split('=')[0]):
                    self.assertEqual(verdict(IDF.replace(token, '', 1)), 'FAIL')

    def test_duplicate_or_malformed_fields_never_hide_behind_valid_ones(self):
        for text in (ARDUINO, IDF):
            for line in text.splitlines(keepends=True)[:-1]:
                with self.subTest(line=line.strip()):
                    self.assertEqual(verdict(text.replace(line, line + line)), 'FAIL')
                    self.assertEqual(verdict(text.replace(line, line + line.rstrip() + 'junk\n')), 'FAIL')
        for before, after in (('ok=10', 'ok=10junk'), ('max_rx=128', 'max_rx=128junk'),
                              ('wake_ready_ms=0', 'wake_ready_ms=0junk')):
            self.assertEqual(verdict(IDF.replace(before, after)), 'FAIL')

    def test_stale_healthy_snapshot_cannot_replace_later_snapshot(self):
        for text, field in ((ARDUINO, '  Total success: 10\n'), (IDF, 'ok=10 ')):
            stale = text.removesuffix('> ')
            self.assertEqual(verdict(stale + text.replace(field, '')), 'FAIL')
            self.assertEqual(verdict(text.replace(field, '') + text), 'FAIL')

    def test_zero_and_uint32_boundary_success_counts(self):
        empty = ARDUINO.replace('success: 10', 'success: 0').replace('100.0%', '0.0%').replace('2 ms ago (at 45 ms)', 'never')
        self.assertEqual(verdict(empty), 'PASS')
        self.assertEqual(verdict(ARDUINO.replace('success: 10', 'success: 4294967295')), 'PASS')
        self.assertEqual(verdict(ARDUINO.replace('100.0%', '0.0%')), 'FAIL')
        self.assertEqual(verdict(ARDUINO.replace('success: 10', 'success: 4294967296')), 'FAIL')

    def test_health_policy_still_rejects_current_failures(self):
        for text in (ARDUINO.replace('READY', 'OFFLINE'), ARDUINO.replace('failures: 0', 'failures: 1'),
                     IDF.replace('state=1', 'state=3'), IDF.replace('fail=0', 'fail=1')):
            self.assertEqual(verdict(text), 'FAIL')

    def test_retained_error_details_must_be_complete_and_unique(self):
        # Exercise structural validation directly; the present-device HIL policy
        # separately rejects errors, including retained transport failures.
        history = ARDUINO.replace('Last error: never', 'Last error: 5 ms ago (at 42 ms)')
        history = history.replace('> ', '  Error code: CANCELLED\n  Error detail: -2\n  Error msg: cancelled\n> ')
        self.assertIsNone(runner.health_failure_reason(history))
        for removed in ('  Error code: CANCELLED\n', '  Error detail: -2\n'):
            self.assertIsNotNone(runner.health_failure_reason(history.replace(removed, '')))
        self.assertIsNotNone(runner.health_failure_reason(history.replace('Error detail: -2', 'Error detail: -2junk')))
        self.assertIsNotNone(runner.health_failure_reason(history.replace('  Error msg: cancelled\n', '  Error msg: cancelled\n' * 2)))


if __name__ == '__main__':
    unittest.main()
