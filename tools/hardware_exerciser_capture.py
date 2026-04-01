#!/usr/bin/env python3
import argparse
import struct
import sys

try:
    import serial
except ImportError:
    print("pyserial is required", file=sys.stderr)
    sys.exit(2)


SYNC = 0x03
CTRL = 0x06
PC_TO_RDR_ESCAPE = 0x6B
HX_VERSION = 1


def lrc(data: bytes) -> int:
    value = 0
    for b in data:
        value ^= b
    return value


def build_escape_frame(seq: int, opcode: int, body: bytes = b"", slot: int = 0) -> bytes:
    payload = b"HX" + bytes([HX_VERSION, opcode]) + body
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
    header = port.read(12)
    if len(header) < 12:
        raise TimeoutError("timed out waiting for response header")
    payload_len = struct.unpack("<I", header[3:7])[0]
    payload = port.read(payload_len + 1)
    if len(payload) < payload_len + 1:
        raise TimeoutError("timed out waiting for response payload")
    return header + payload


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("port", help="CDC0 serial port for HardwareExerciser")
    parser.add_argument("--seq", type=int, default=1)
    parser.add_argument("--opcode", type=lambda x: int(x, 0), default=0x02)
    parser.add_argument(
        "--body-hex",
        default="",
        help="Raw request body bytes. HF14A_TXRX expects raw ISO14443A frame bytes.",
    )
    args = parser.parse_args()

    body = bytes.fromhex(args.body_hex) if args.body_hex else b""
    frame = build_escape_frame(args.seq, args.opcode, body)

    with serial.Serial(args.port, 115200, timeout=1.0) as port:
        port.write(frame)
        response = read_frame(port)

    print(
        "# HF14A_SCAN response: uid_len | uid | atqa_lo | atqa_hi | sak | ats_len | ats",
        file=sys.stderr,
    )
    print(response.hex())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
