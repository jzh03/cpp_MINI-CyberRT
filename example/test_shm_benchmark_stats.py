#!/usr/bin/env python3
"""Synthetic checks of aggregation and rejection of misleading benchmark data."""
import copy
import unittest
from run_shm_benchmark import validate, metrics, summarize


class BenchmarkStatsTest(unittest.TestCase):
    def setUp(self):
        self.sender = dict(mode="copy", size_bytes=4096, scope="e2e", slots=32,
                           slot_capacity=8388608, segment_bytes=268506112,
                           channel_id=1, serialized_size=4103, warmup_ms=1000,
                           duration_ms=1000, drain_ms=1000, window_start_ns=1_000_000_000,
                           window_end_ns=2_000_000_000, cpu_start_ns=1_000_000_000,
                           cpu_end_ns=2_000_000_000, cpu_ns=500_000_000,
                           pid=10, peer_pid=20, path_verified=True, attempts=10,
                           send_success=8, acquire_fail=0, transmit_fail=2,
                           measured_serializations=10)
        self.receiver = {**self.sender, "pid": 20, "peer_pid": 10,
                         "deserializations_all_phases": 9, "invalid": 0, "early": 0,
                         "warmup_after_start": 0, "window_unique": 4, "drain_unique": 2,
                         "warmup_received_excluded": 1, "missing_attempts_after_drain": 4,
                         "missing_success_after_drain": 2, "received_failed_send": 0,
                         "window_valid_bytes": 16384, "formal_callbacks": 7,
                         "duplicates_window": 1, "duplicates_drain": 0, "after_cutoff": 0}

    def test_valid_with_losses_and_duplicates(self):
        validate(self.sender, self.receiver)
        result = metrics(self.sender, self.receiver)
        self.assertEqual(result["throughput_mib_s"], 4 / 256)  # Excludes two drain messages.
        self.assertEqual(result["sender_cpu_pct"], 50)
        self.assertEqual(result["missing_success_pct"], 25)

    def test_reject_invalid_evidence(self):
        for key, value in (("pid", 10), ("slots", 64), ("invalid", 1), ("warmup_after_start", 1),
                           ("window_valid_bytes", 24576), ("cpu_end_ns", 2_030_000_000),
                           ("missing_success_after_drain", 4), ("path_verified", False)):
            with self.subTest(key=key):
                receiver = copy.deepcopy(self.receiver); receiver[key] = value
                with self.assertRaises(ValueError): validate(self.sender, receiver)

    def test_loan_requires_actual_shm_callbacks(self):
        self.sender.update(mode="loan", measured_serializations=0, measured_shm_loans=10)
        self.receiver.update(mode="loan", shm_callbacks=6)
        with self.assertRaises(ValueError): validate(self.sender, self.receiver)
        self.receiver["shm_callbacks"] = 7
        validate(self.sender, self.receiver)

    def test_median_and_range(self):
        rows = [{"size_bytes": 4096, "mode": mode, "throughput_mib_s": n,
                 "sender_cpu_pct": n, "receiver_cpu_pct": n, "missing_success_pct": n}
                for mode in ("copy", "loan") for n in (3, 1, 20)]
        for row in summarize(rows):
            self.assertEqual(row["repeats"], 3)
            self.assertEqual(row["throughput_mib_s"], {"median": 3, "min": 1, "max": 20})


if __name__ == "__main__":
    unittest.main()
