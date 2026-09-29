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

TRAIN_MODE_LABELS = {
    "KOCH": "Koch Method",
    "CHARACTERS": "Characters",
    "WORDS": "Words",
    "CALLSIGNS": "Callsigns",
    "ADAPTIVE": "Adaptive",
    "EXAM": "Exam",
}

TRAIN_MODE_DESCRIPTIONS = {
    "KOCH": "Learn characters progressively - each new character is added "
            "once you copy the current set accurately enough.",
    "CHARACTERS": "Drill a fixed, device-configured set of characters.",
    "WORDS": "Copy real words at your current speed.",
    "CALLSIGNS": "Copy amateur radio callsigns.",
    "ADAPTIVE": "Speed automatically adjusts to your accuracy.",
    "EXAM": "A timed, graded copy test with a pass/fail result.",
}

# Exact copy of firmware/MORPHEUS/core_trainer.cpp's KOCH_ORDER - the
# fixed character-unlock sequence for Koch-method training. kochLevel
# (reported live in train_state) is how many of these are unlocked.
KOCH_ORDER = "KMURESNAPTLWI.JZ=FOY,VGQ5/H38B?47C1D6X92"

# Exact copy of firmware/MORPHEUS/core_decoder.cpp's morseTable - used
# to render the real Morse pattern for the live training target
# character, not an approximation from an external reference.
MORSE_TABLE = {
    "A": ".-", "B": "-...", "C": "-.-.", "D": "-..", "E": ".",
    "F": "..-.", "G": "--.", "H": "....", "I": "..", "J": ".---",
    "K": "-.-", "L": ".-..", "M": "--", "N": "-.", "O": "---",
    "P": ".--.", "Q": "--.-", "R": ".-.", "S": "...", "T": "-",
    "U": "..-", "V": "...-", "W": ".--", "X": "-..-", "Y": "-.--", "Z": "--..",
    "0": "-----", "1": ".----", "2": "..---", "3": "...--", "4": "....-",
    "5": ".....", "6": "-....", "7": "--...", "8": "---..", "9": "----.",
    ".": ".-.-.-", ",": "--..--", "?": "..--..",
    "/": "-..-.", "=": "-...-", "+": ".-.-.", "-": "-....-",
}
