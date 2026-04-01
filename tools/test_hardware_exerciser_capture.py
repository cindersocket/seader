import unittest

from tools import hardware_exerciser_capture as capture


class HardwareExerciserCaptureCliTests(unittest.TestCase):
    def setUp(self) -> None:
        self.parser = capture.create_parser()

    def test_legacy_raw_invocation_is_preserved(self) -> None:
        args = self.parser.parse_args(
            ["/dev/ttyACM0", "--opcode", "0x44", "--body-hex", "07"]
        )

        opcode, body = capture.build_body(args)
        self.assertIsNone(args.command)
        self.assertEqual(opcode, capture.OPCODE_GPIO_READ_ANALOG)
        self.assertEqual(body, bytes([0x07]))

    def test_explicit_raw_subcommand_still_works(self) -> None:
        args = self.parser.parse_args(
            ["/dev/ttyACM0", "raw", "--opcode", "0x40", "--body-hex", ""]
        )

        opcode, body = capture.build_body(args)
        self.assertEqual(args.command, "raw")
        self.assertEqual(opcode, capture.OPCODE_GPIO_LIST_PINS)
        self.assertEqual(body, b"")

    def test_gpio_vector_body_is_encoded_in_request_order(self) -> None:
        args = self.parser.parse_args(
            [
                "/dev/ttyACM0",
                "vector",
                "--write",
                "7=1",
                "--write",
                "21=0",
                "--settle-us",
                "300",
                "--sample",
                "7:d",
                "--sample",
                "21:a",
            ]
        )

        opcode, body = capture.build_body(args)
        self.assertEqual(opcode, capture.OPCODE_GPIO_VECTOR)
        self.assertEqual(
            body,
            bytes(
                [
                    0x02,
                    0x07,
                    0x01,
                    0x15,
                    0x00,
                    0x2C,
                    0x01,
                    0x00,
                    0x00,
                    0x02,
                    0x07,
                    capture.GPIO_SAMPLE_KIND["d"],
                    0x15,
                    capture.GPIO_SAMPLE_KIND["a"],
                ]
            ),
        )


if __name__ == "__main__":
    unittest.main()
