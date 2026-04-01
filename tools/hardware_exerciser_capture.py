#!/usr/bin/env python3
"""Minimal host-side probe for the Hardware Exerciser CDC0 escape protocol."""

import argparse
import struct
import sys

try:
    import serial
except ImportError:
    serial = None


SYNC = 0x03
CTRL = 0x06
PC_TO_RDR_ESCAPE = 0x6B
HX_VERSION = 1

OPCODE_GET_CAPS = 0x02
OPCODE_GPIO_LIST_PINS = 0x40
OPCODE_GPIO_CONFIGURE = 0x41
OPCODE_GPIO_WRITE = 0x42
OPCODE_GPIO_READ = 0x43
OPCODE_GPIO_READ_ANALOG = 0x44
OPCODE_GPIO_VECTOR = 0x45
OPCODE_GPIO_RESET = 0x46
OPCODE_QUIT = 0x7E

GPIO_MODE = {
    "in": 0,
    "out": 1,
    "open_drain": 2,
    "analog": 3,
}

GPIO_PULL = {
    "none": 0,
    "up": 1,
    "down": 2,
}

GPIO_SAMPLE_KIND = {
    "d": 0,
    "a": 1,
}


def lrc(data: bytes) -> int:
    """Return the XOR LRC used by Hardware Exerciser request and response frames."""
    value = 0
    for b in data:
        value ^= b
    return value


def build_escape_frame(seq: int, opcode: int, body: bytes = b"", slot: int = 0) -> bytes:
    """Build one PC_TO_RDR_ESCAPE frame carrying an HX request payload."""
    payload = b"HX" + bytes([HX_VERSION, opcode]) + body
    # The frame layout mirrors the firmware helper: preamble, CCID header, HX payload, trailing LRC.
    header = bytearray(
        [
            SYNC,
            CTRL,
            PC_TO_RDR_ESCAPE,
            len(payload) & 0xFF,
            (len(payload) >> 8) & 0xFF,
            (len(payload) >> 16) & 0xFF,
            (len(payload) >> 24) & 0xFF,
            slot & 0xFF,
            seq & 0xFF,
            0,
            0,
            0,
        ]
    )
    frame = bytes(header) + payload
    return frame + bytes([lrc(frame)])


def read_frame(port: serial.Serial) -> bytes:
    """Read a complete response frame using the CCID payload length from the 12-byte wire header."""
    header = port.read(12)
    if len(header) < 12:
        raise TimeoutError("timed out waiting for response header")
    payload_len = struct.unpack("<I", header[3:7])[0]
    # One extra byte is read for the trailing LRC appended after the payload.
    payload = port.read(payload_len + 1)
    if len(payload) < payload_len + 1:
        raise TimeoutError("timed out waiting for response payload")
    return header + payload


def parse_write_item(text: str) -> bytes:
    """Parse PIN=LEVEL vector writes into a two-byte HX body fragment."""
    pin_text, level_text = text.split("=", 1)
    pin = int(pin_text, 0)
    level = int(level_text, 0)
    if pin < 0 or pin > 255 or level not in (0, 1):
        raise ValueError("vector writes must be PIN=0 or PIN=1")
    return bytes([pin, level])


def parse_sample_item(text: str) -> bytes:
    """Parse PIN:d or PIN:a vector sample selectors into a two-byte HX body fragment."""
    pin_text, kind_text = text.split(":", 1)
    pin = int(pin_text, 0)
    kind = GPIO_SAMPLE_KIND[kind_text.lower()]
    if pin < 0 or pin > 255:
        raise ValueError("vector sample pin must fit in one byte")
    return bytes([pin, kind])


def build_body(args: argparse.Namespace) -> tuple[int, bytes]:
    """Build an HX opcode/body pair for the selected subcommand."""
    command = args.command
    if command in (None, "raw"):
        return args.opcode, bytes.fromhex(args.body_hex) if args.body_hex else b""
    if command == "list-pins":
        return OPCODE_GPIO_LIST_PINS, b""
    if command == "configure":
        return (
            OPCODE_GPIO_CONFIGURE,
            bytes(
                [
                    args.pin & 0xFF,
                    GPIO_MODE[args.mode],
                    GPIO_PULL[args.pull],
                    args.initial_level & 0xFF,
                ]
            ),
        )
    if command == "write":
        return OPCODE_GPIO_WRITE, bytes([args.pin & 0xFF, args.level & 0xFF])
    if command == "read":
        return OPCODE_GPIO_READ, bytes([args.pin & 0xFF])
    if command == "read-analog":
        return OPCODE_GPIO_READ_ANALOG, bytes([args.pin & 0xFF])
    if command == "reset":
        return OPCODE_GPIO_RESET, b""
    if command == "quit":
        return OPCODE_QUIT, b""
    if command == "vector":
        write_items = [parse_write_item(item) for item in args.write]
        sample_items = [parse_sample_item(item) for item in args.sample]
        body = bytearray()
        body.append(len(write_items) & 0xFF)
        for item in write_items:
            body.extend(item)
        body.extend(struct.pack("<I", args.settle_us))
        body.append(len(sample_items) & 0xFF)
        for item in sample_items:
            body.extend(item)
        return OPCODE_GPIO_VECTOR, bytes(body)

    raise ValueError(f"unsupported command: {command}")


def create_parser() -> argparse.ArgumentParser:
    """Create the CLI parser for both legacy raw mode and explicit subcommands."""
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="CDC0 serial port for HardwareExerciser")
    parser.add_argument("--seq", type=int, default=1)
    # Preserve the original helper workflow where raw requests were the implicit default mode.
    parser.add_argument("--opcode", type=lambda x: int(x, 0), default=OPCODE_GET_CAPS)
    parser.add_argument("--body-hex", default="", help="Raw HX request body bytes.")
    subparsers = parser.add_subparsers(dest="command")

    raw = subparsers.add_parser("raw")
    raw.add_argument("--opcode", type=lambda x: int(x, 0), default=OPCODE_GET_CAPS)
    raw.add_argument("--body-hex", default="", help="Raw HX request body bytes.")

    subparsers.add_parser("list-pins")

    configure = subparsers.add_parser("configure")
    configure.add_argument("pin", type=lambda x: int(x, 0))
    configure.add_argument("mode", choices=sorted(GPIO_MODE))
    configure.add_argument("--pull", choices=sorted(GPIO_PULL), default="none")
    configure.add_argument("--initial-level", type=lambda x: int(x, 0), default=0)

    write = subparsers.add_parser("write")
    write.add_argument("pin", type=lambda x: int(x, 0))
    write.add_argument("level", type=lambda x: int(x, 0))

    read = subparsers.add_parser("read")
    read.add_argument("pin", type=lambda x: int(x, 0))

    read_analog = subparsers.add_parser("read-analog")
    read_analog.add_argument("pin", type=lambda x: int(x, 0))

    vector = subparsers.add_parser("vector")
    vector.add_argument("--write", action="append", default=[], help="PIN=LEVEL, may be repeated")
    vector.add_argument("--sample", action="append", default=[], help="PIN:d or PIN:a, may be repeated")
    vector.add_argument("--settle-us", type=lambda x: int(x, 0), default=0)

    subparsers.add_parser("reset")
    subparsers.add_parser("quit")

    return parser


def main() -> int:
    parser = create_parser()
    args = parser.parse_args()

    if serial is None:
        print("pyserial is required", file=sys.stderr)
        return 2

    opcode, body = build_body(args)
    frame = build_escape_frame(args.seq, opcode, body)

    with serial.Serial(args.port, 115200, timeout=1.0) as port:
        # The helper is intentionally single-shot so it can be used in shell loops or manual probing.
        port.write(frame)
        response = read_frame(port)

    print(
        "# Raw response frame as hex. Decode the HX payload after the 12-byte header for opcode-specific bodies.",
        file=sys.stderr,
    )
    print(response.hex())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
