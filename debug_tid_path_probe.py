#!/usr/bin/env python3
from __future__ import annotations

import time
import serial


PORT = "/dev/tty.usbmodemflip_Oshawotb1"
BAUD = 115200
RX_WAIT_S = 0.35
INTER_CMD_S = 0.04

PUBLIC_EPC = "0000723800007AD491520AD1"
PRIVATE_TID = "E280110520007AD491520AD1"
ACCESS_PW = 0x9619E8D3


def build_frame(command: int, payload: bytes, frame_type: int = 0x00) -> bytes:
    body = bytes([frame_type, command & 0xFF, (len(payload) >> 8) & 0xFF, len(payload) & 0xFF]) + payload
    checksum = sum(body) & 0xFF
    return b"\xBB" + body + bytes([checksum, 0x7E])


def build_select(mask_hex: str, mem_bank: int, ptr_bits: int) -> bytes:
    mask = bytes.fromhex(mask_hex)
    payload = bytes([mem_bank & 0x03]) + ptr_bits.to_bytes(4, "big") + bytes([len(mask) * 8, 0x00]) + mask
    return build_frame(0x0C, payload)


def build_qt(access_password: int, control_word: int) -> bytes:
    payload = access_password.to_bytes(4, "big") + bytes([0x01, 0x01]) + control_word.to_bytes(2, "big")
    return build_frame(0xE5, payload)


def build_read_mem(access_password: int, mem_bank: int, word_ptr: int, word_count: int) -> bytes:
    payload = (
        access_password.to_bytes(4, "big")
        + bytes([mem_bank & 0x03])
        + word_ptr.to_bytes(2, "big")
        + word_count.to_bytes(2, "big")
    )
    return build_frame(0x39, payload)


def read_until_idle(ser: serial.Serial, wait_s: float) -> bytes:
    end = time.time() + wait_s
    out = bytearray()
    while time.time() < end:
        chunk = ser.read(256)
        if chunk:
            out.extend(chunk)
            end = time.time() + 0.15
        else:
            time.sleep(0.005)
    return bytes(out)


def send_and_read(ser: serial.Serial, tx: bytes) -> bytes:
    ser.write(tx)
    ser.flush()
    time.sleep(INTER_CMD_S)
    rx = read_until_idle(ser, RX_WAIT_S)
    if rx and rx[-1] != 0x7E:
        rx += read_until_idle(ser, 0.15)
    return rx


def log(label: str, tx: bytes | None, rx: bytes) -> None:
    if tx is not None:
        print(f"{label}_TX={tx.hex().upper()}")
    print(f"{label}_RX={rx.hex().upper()}")


def main() -> int:
    cmds = {
        "rf_off": bytes.fromhex("BB00B0000100B17E"),
        "rf_on": bytes.fromhex("BB00B00001FFB07E"),
        "set_region_us": build_frame(0x07, b"\x02"),
        "set_channel_0": build_frame(0xAB, b"\x00"),
        "set_query_default": build_frame(0x0E, bytes.fromhex("1020")),
        "set_select_mode_inventory": build_frame(0x12, b"\x01"),
        "single_poll": bytes.fromhex("BB00220000227E"),
        "select_public_epc": build_select(PUBLIC_EPC, 0x01, 0x20),
        "qt_private": build_qt(ACCESS_PW, 0x0000),
        "select_private_tid": build_select(PRIVATE_TID, 0x02, 0x00000000),
        "read_user_pw0": build_read_mem(0, 0x03, 0x0000, 0x0020),
        "read_user_pwa": build_read_mem(ACCESS_PW, 0x03, 0x0000, 0x0020),
        "qt_public": build_qt(ACCESS_PW, 0x4000),
    }

    with serial.Serial(PORT, BAUD, timeout=0.05) as ser:
        for name in ("rf_off", "rf_on", "set_region_us", "set_channel_0", "set_query_default", "set_select_mode_inventory"):
            rx = send_and_read(ser, cmds[name])
            log(name, cmds[name], rx)
            time.sleep(0.08)

        rx = send_and_read(ser, cmds["single_poll"])
        log("single_poll_public", cmds["single_poll"], rx)
        time.sleep(0.08)

        rx = send_and_read(ser, cmds["select_public_epc"])
        log("select_public_epc", cmds["select_public_epc"], rx)
        time.sleep(0.04)

        rx = send_and_read(ser, cmds["qt_private"])
        log("qt_private", cmds["qt_private"], rx)
        time.sleep(0.12)

        ser.close()

    time.sleep(0.05)

    with serial.Serial(PORT, BAUD, timeout=0.05) as ser:
        for name in ("rf_off", "rf_on", "set_region_us", "set_channel_0", "set_query_default", "set_select_mode_inventory"):
            rx = send_and_read(ser, cmds[name])
            log(f"refresh_{name}", cmds[name], rx)
            time.sleep(0.08)

        rx = send_and_read(ser, cmds["select_private_tid"])
        log("select_private_tid", cmds["select_private_tid"], rx)
        time.sleep(0.04)

        started = time.time()
        for i in range(1, 7):
            rx = send_and_read(ser, cmds["single_poll"])
            log(f"single_poll_private_{i}", cmds["single_poll"], rx)
            if time.time() - started >= 3.0:
                break
            time.sleep(0.2)

        rx = send_and_read(ser, cmds["read_user_pw0"])
        log("read_user_pw0", cmds["read_user_pw0"], rx)
        time.sleep(0.25)
        ser.reset_input_buffer()

        rx = send_and_read(ser, cmds["qt_public"])
        log("qt_public", cmds["qt_public"], rx)
        time.sleep(0.08)

        rx = send_and_read(ser, cmds["single_poll"])
        log("single_poll_after_relock", cmds["single_poll"], rx)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
