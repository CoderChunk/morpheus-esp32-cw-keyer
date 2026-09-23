"""
MORPHEUS BLE protocol constants and enums.

Must match firmware/MORPHEUS/config.h and ble_control.cpp exactly.
"""

DEVICE_NAME = "MORPHEUS-CW"
SERVICE_UUID = "7a48a2b0-0001-4ad4-9f1a-1c2d3e4f5a6b"
WORD_CHAR_UUID = "7a48a2b0-0002-4ad4-9f1a-1c2d3e4f5a6b"
CONTROL_CMD_UUID = "7a48a2b0-0003-4ad4-9f1a-1c2d3e4f5a6b"
CONTROL_EVT_UUID = "7a48a2b0-0004-4ad4-9f1a-1c2d3e4f5a6b"

SCAN_TIMEOUT_S = 10.0

TRAIN_MODES = ["KOCH", "CHARACTERS", "WORDS", "CALLSIGNS", "ADAPTIVE", "EXAM"]
GAMES = ["COPY", "MEMORY", "SPEED"]
