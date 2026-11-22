#!/usr/bin/env python3
"""Tests for the Phase 5 host tools. Standard library only:

    python3 tools/test_tools.py

Covers the serial record parser (tools/serial_logger.py); the plotting and
replay tools are exercised end-to-end from docs/testing.md.
"""
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

import serial_logger  # noqa: E402


class ParseRecordTests(unittest.TestCase):
    def test_sample_record(self):
        line = "S,1000,0.1,0.2,9.8,0.01,0.02,0.03,0,0,1,1.5,-2.5,0.02,0"
        kind, payload = serial_logger.parse_record(line)
        self.assertEqual(kind, "S")
        self.assertEqual(len(payload), len(serial_logger.SAMPLE_FIELDS))
        self.assertAlmostEqual(payload[0], 1000.0)
        self.assertAlmostEqual(payload[6], 0.03)  # gz

    def test_sample_with_crlf(self):
        line = "S,1,0,0,9.8,0,0,0,0,0,1,0,0,0.02,1\r"
        kind, _ = serial_logger.parse_record(line)
        self.assertEqual(kind, "S")

    def test_swing_record_keeps_text_fields(self):
        line = "W,5000,260,7.90,1.00,2.00,1.28,0.00,0.00,3.0,forehand,"
        kind, payload = serial_logger.parse_record(line)
        self.assertEqual(kind, "W")
        self.assertEqual(payload[-2], "forehand")
        self.assertEqual(payload[-1], "")
        # 0=t 1=duration 2=peak 3=peak_lin 4=peak_up 5=int_yaw ...
        self.assertAlmostEqual(payload[5], 1.28)  # int_yaw

    def test_swing_reject_reason(self):
        line = "W,5000,260,7.90,1.00,2.00,0.0,0.0,1.3,0.0,none,roll_dominant"
        kind, payload = serial_logger.parse_record(line)
        self.assertEqual(kind, "W")
        self.assertEqual(payload[-2], "none")
        self.assertEqual(payload[-1], "roll_dominant")

    def test_tunable_record(self):
        kind, payload = serial_logger.parse_record("T,onset=2.5")
        self.assertEqual(kind, "T")
        self.assertEqual(payload, "onset=2.5")

    def test_human_log_lines_are_ignored(self):
        for line in ("[stats] datagrams=0 samples=0",
                     "[angles] roll=+0.1 pitch=-0.2 deg",
                     "[cal] done: gyro bias=(+0.020 -0.010 +0.015) rad/s",
                     "random text", ""):
            self.assertEqual(serial_logger.parse_record(line), (None, None))

    def test_wrong_field_counts_are_rejected(self):
        self.assertEqual(serial_logger.parse_record("S,1,2,3")[0], None)
        self.assertEqual(serial_logger.parse_record("W,1,2,3")[0], None)

    def test_non_numeric_sample_is_rejected(self):
        self.assertEqual(serial_logger.parse_record("S,a,b,c,d,e,f,g,h,i,j,k,l,m")[0], None)

    def test_unknown_record_type(self):
        self.assertEqual(serial_logger.parse_record("X,1,2,3")[0], None)


class FieldContractTests(unittest.TestCase):
    """The firmware's CSV field order must match what the tools expect."""

    def test_sample_fields_match_firmware(self):
        # main.cpp emits: S,rx,ax,ay,az,gx,gy,gz,up.x,up.y,up.z,roll,pitch,dt
        self.assertEqual(serial_logger.SAMPLE_FIELDS,
                         ["rx_ms", "ax", "ay", "az", "gx", "gy", "gz",
                          "up_x", "up_y", "up_z", "roll_deg", "pitch_deg", "dt_s",
                          "calibrating"])

    def test_swing_fields_match_firmware(self):
        # main.cpp emits: W,t,duration,peak,peakLin,peakUp,intYaw,intPitch,intRoll,
        #                 onsetPitch,gesture,reason
        self.assertEqual(serial_logger.SWING_FIELDS,
                         ["t_ms", "duration_ms", "peak_gyro", "peak_lin_acc", "peak_up_acc",
                          "int_yaw", "int_pitch", "int_roll", "onset_pitch_deg",
                          "gesture", "reason"])


if __name__ == "__main__":
    unittest.main(verbosity=2)