"""Deterministic timing-analysis/collector tests; no game process required."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import probe_timing as probe

MS = 1_000_000


class AnalysisTests(unittest.TestCase):
    def test_fixed_40ms_updates_at_5ms_polling(self):
        samples = [(ms * MS, 1000 + ms // 40 * 40, ms // 40) for ms in range(0, 401, 5)]
        result = probe.analyze_samples(samples, 400 * MS)
        self.assertEqual(result['clock_delta_ms']['count'], 10)
        self.assertEqual(result['clock_delta_ms']['mode'], 40)
        self.assertEqual(result['clock_delta_ms']['p99'], 40)
        self.assertEqual(result['clock_delta_ms']['mode_count'], 10)
        self.assertEqual(result['frame_advance_total'], 10)
        self.assertEqual(result['frame_advance_hz'], 25)
        self.assertEqual(result['effective_observed_clock_change_hz'], 25)
        self.assertEqual(result['poll_interval_ms']['p50'], 5)
        self.assertFalse(result['per_tick_complete'])

    def test_wrap_clock_and_counter(self):
        result = probe.analyze_samples([(0, 0xFFFFFFF0, 0xFFFFFFFF), (40 * MS, 24, 0)], 40 * MS)
        self.assertEqual(result['clock_delta_ms']['mode'], 40)
        self.assertEqual(result['clock_wraps'], 1)
        self.assertEqual(result['frame_wraps'], 1)
        self.assertEqual(result['frame_advance_total'], 1)
        self.assertEqual(result['frame_advance_hz'], 25)

    def test_reset_excluded_and_total_rate_unavailable(self):
        samples = [(0, 1000, 100), (40 * MS, 1040, 101), (80 * MS, 40, 0), (120 * MS, 80, 1)]
        result = probe.analyze_samples(samples, 120 * MS)
        self.assertEqual(result['clock_discontinuities'], 1)
        self.assertEqual(result['frame_discontinuities'], 1)
        self.assertEqual(result['clock_delta_ms']['count'], 2)
        self.assertEqual(result['frame_advance_total'], 2)
        self.assertIsNone(result['frame_advance_hz'])

    def test_forward_clock_initialization_not_cadence(self):
        result = probe.analyze_samples([(0, 0, 0), (5 * MS, 100000, 0), (45 * MS, 100040, 1)], 45 * MS)
        self.assertEqual(result['clock_discontinuities'], 1)
        self.assertEqual(result['clock_delta_ms']['count'], 1)
        self.assertEqual(result['clock_delta_ms']['mode'], 40)

    def test_poll_misses_preserve_counter_rate_not_complete_tick_log(self):
        samples = [(ms * MS, 1000 + ms, ms // 40) for ms in (0, 80, 160, 240)]
        result = probe.analyze_samples(samples, 240 * MS)
        self.assertEqual(result['observed_frame_transitions'], 3)
        self.assertEqual(result['frame_advance_total'], 6)
        self.assertEqual(result['frame_advance_hz'], 25)
        self.assertEqual(result['effective_observed_clock_change_hz'], 12.5)
        self.assertEqual(result['clock_delta_ms']['mode'], 80)
        self.assertEqual(result['intermediate_frames_not_individually_observed'], 3)
        self.assertFalse(result['per_tick_complete'])

    def test_stall_excludes_unobserved_time_after_last_read(self):
        result = probe.analyze_samples([(0, 1000, 0), (100 * MS, 1000, 0)], 5000 * MS)
        self.assertEqual(result['longest_observed_stall_seconds'], .1)
        self.assertEqual(result['frame_advance_hz'], 0)

    def test_empty_and_invalid_timestamps(self):
        self.assertIsNone(probe.analyze_samples([], 0)['frame_advance_hz'])
        with self.assertRaises(ValueError):
            probe.analyze_samples([(1, 0, 0), (1, 40, 1)], 1)
        with self.assertRaises(ValueError):
            probe.analyze_samples([(2, 0, 0)], 1)

    def test_interpolated_percentiles_and_modes(self):
        result = probe.interval_statistics([40, 40, 80, 80])
        self.assertEqual(result['p50'], 60)
        self.assertEqual(result['modes'], [40, 80])
        self.assertEqual(result['mode_count'], 2)


class FakeClock:
    def __init__(self):
        self.now = 0

    def read(self):
        return self.now

    def sleep(self, seconds):
        self.now += round(seconds * 1_000_000_000)


class FakeProcess:
    def __init__(self, clock, statuses=None, fail_read_at=None):
        self.clock = clock
        self.statuses = iter(statuses or [])
        self.fail_read_at = fail_read_at
        self.read_count = 0

    def exit_code(self):
        value = next(self.statuses, None)
        if isinstance(value, Exception):
            raise value
        return value

    def read_sample(self):
        self.read_count += 1
        if self.read_count == self.fail_read_at:
            raise OSError('crafted ReadProcessMemory failure')
        frame = self.clock.now // (40 * MS)
        return 1000 + frame * 40, frame


class CollectorTests(unittest.TestCase):
    def collect(self, statuses=None, fail_read_at=None):
        clock, events = FakeClock(), []
        process = FakeProcess(clock, statuses, fail_read_at)
        result = probe.collect_samples(process, .2, 5, events.append, clock.read, clock.sleep)
        self.assertEqual(events[-1]['event'], 'summary')
        return result

    def test_completed_window(self):
        result = self.collect()
        self.assertEqual(result['status'], 'completed_observation')
        self.assertEqual(result['samples'], 40)
        self.assertEqual(result['frame_advance_total'], 4)

    def test_exit_before_first_sample_including_exit_code_259(self):
        result = self.collect([259])
        self.assertEqual(result['status'], 'process_exited')
        self.assertEqual(result['process_exit_code'], 259)
        self.assertEqual(result['samples'], 0)

    def test_exit_mid_observation(self):
        result = self.collect([None, None, 0])
        self.assertEqual(result['status'], 'process_exited')
        self.assertEqual(result['samples'], 2)
        self.assertEqual(result['elapsed_seconds'], .01)

    def test_status_query_failure_is_not_exit(self):
        result = self.collect([OSError('status denied')])
        self.assertEqual(result['status'], 'process_status_failed')
        self.assertIsNone(result['process_exit_code'])

    def test_read_failure_while_alive(self):
        result = self.collect(fail_read_at=2)
        self.assertEqual(result['status'], 'read_failed')
        self.assertEqual(result['samples'], 1)
        self.assertEqual(result['longest_observed_stall_seconds'], 0)

    def test_exit_racing_read_failure(self):
        result = self.collect([None, 7], fail_read_at=1)
        self.assertEqual(result['status'], 'process_exited')
        self.assertEqual(result['process_exit_code'], 7)
        self.assertEqual(result['error']['operation'], 'read_memory')


class ImageGateTests(unittest.TestCase):
    def setUp(self):
        self.image = bytearray(probe.IMAGE_SIZE)
        self.image[:2] = b'MZ'
        struct.pack_into('<I', self.image, 0x3C, 0x80)
        self.image[0x80:0x84] = b'PE\0\0'
        for offset, fmt, value in [(4, '<H', 0x14C), (24, '<H', 0x10B), (40, '<I', 0x1000),
                                   (52, '<I', probe.IMAGE_BASE), (80, '<I', probe.IMAGE_SIZE)]:
            struct.pack_into(fmt, self.image, 0x80 + offset, value)
        for address, signature in probe.SIGNATURES.items():
            offset = address - probe.IMAGE_BASE
            self.image[offset:offset + len(signature)] = signature

    def read(self, address, size):
        offset = address - probe.IMAGE_BASE
        return self.image[offset:offset + size]

    def test_exact_image(self):
        self.assertEqual(len(probe.validate_image(self.read, probe.IMAGE_BASE, probe.IMAGE_SIZE)), 5)

    def test_wrong_base_and_changed_instruction_refused(self):
        with self.assertRaises(ValueError):
            probe.validate_image(self.read, probe.IMAGE_BASE + 0x10000, probe.IMAGE_SIZE)
        self.image[0x678C0] ^= 1
        with self.assertRaisesRegex(ValueError, 'signature'):
            probe.validate_image(self.read, probe.IMAGE_BASE, probe.IMAGE_SIZE)


if __name__ == '__main__':
    unittest.main()
