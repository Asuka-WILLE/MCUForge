"""MCUForge PC-to-STM32 control frame codec."""

from __future__ import annotations

import struct


HEADER = b"\xAA\x55"
PROTOCOL_VERSION = 1
FRAME_TYPE_CONTROL = 1
FLAG_ENABLE = 0x01
FLAG_ESTOP = 0x02
COMMAND_MIN = -1000
COMMAND_MAX = 1000

_PAYLOAD = struct.Struct("<2sBBHhhBB")
_CRC = struct.Struct("<H")
CONTROL_FRAME_SIZE = _PAYLOAD.size + _CRC.size


def crc16_modbus(data: bytes) -> int:
    """Return Modbus CRC-16 for *data*."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc & 0xFFFF


def _validate_command(name: str, value: int) -> int:
    value = int(value)
    if not COMMAND_MIN <= value <= COMMAND_MAX:
        raise ValueError(f"{name} 必须位于 {COMMAND_MIN}..{COMMAND_MAX}，当前为 {value}")
    return value


def pack_control_frame(
    sequence: int,
    throttle: int,
    steering: int,
    *,
    enabled: bool = True,
    emergency_stop: bool = False,
) -> bytes:
    """Pack one fixed-length control frame accepted by the demo firmware."""
    throttle = _validate_command("throttle", throttle)
    steering = _validate_command("steering", steering)
    flags = (FLAG_ENABLE if enabled else 0) | (FLAG_ESTOP if emergency_stop else 0)
    payload = _PAYLOAD.pack(
        HEADER,
        PROTOCOL_VERSION,
        FRAME_TYPE_CONTROL,
        int(sequence) & 0xFFFF,
        throttle,
        steering,
        flags,
        0,
    )
    return payload + _CRC.pack(crc16_modbus(payload))


def unpack_control_frame(frame: bytes) -> dict:
    """Decode and validate a frame; primarily used by local tests and tools."""
    if len(frame) != CONTROL_FRAME_SIZE:
        raise ValueError(f"控制帧必须为 {CONTROL_FRAME_SIZE} 字节")
    payload = frame[:-2]
    expected_crc = crc16_modbus(payload)
    actual_crc = _CRC.unpack(frame[-2:])[0]
    if actual_crc != expected_crc:
        raise ValueError("CRC16 校验失败")

    header, version, frame_type, sequence, throttle, steering, flags, reserved = _PAYLOAD.unpack(payload)
    if header != HEADER:
        raise ValueError("帧头错误")
    if version != PROTOCOL_VERSION or frame_type != FRAME_TYPE_CONTROL or reserved != 0:
        raise ValueError("版本、类型或保留字段错误")
    _validate_command("throttle", throttle)
    _validate_command("steering", steering)
    return {
        "sequence": sequence,
        "throttle": throttle,
        "steering": steering,
        "enabled": bool(flags & FLAG_ENABLE),
        "emergency_stop": bool(flags & FLAG_ESTOP),
    }
