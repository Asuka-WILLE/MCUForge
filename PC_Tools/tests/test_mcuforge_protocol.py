import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from mcuforge_protocol import CONTROL_FRAME_SIZE, crc16_modbus, pack_control_frame, unpack_control_frame


class MCUForgeProtocolTests(unittest.TestCase):
    def test_known_modbus_crc_vector(self):
        self.assertEqual(crc16_modbus(b"123456789"), 0x4B37)

    def test_round_trip(self):
        frame = pack_control_frame(65537, -321, 456, enabled=True, emergency_stop=True)
        self.assertEqual(len(frame), CONTROL_FRAME_SIZE)
        self.assertEqual(frame.hex(" ").upper(), "AA 55 01 01 01 00 BF FE C8 01 03 00 2E C4")
        self.assertEqual(
            unpack_control_frame(frame),
            {
                "sequence": 1,
                "throttle": -321,
                "steering": 456,
                "enabled": True,
                "emergency_stop": True,
            },
        )

    def test_rejects_out_of_range_command(self):
        with self.assertRaises(ValueError):
            pack_control_frame(0, 1001, 0)

    def test_rejects_corrupt_crc(self):
        frame = bytearray(pack_control_frame(0, 0, 0))
        frame[6] ^= 0x01
        with self.assertRaisesRegex(ValueError, "CRC16"):
            unpack_control_frame(bytes(frame))


if __name__ == "__main__":
    unittest.main()
