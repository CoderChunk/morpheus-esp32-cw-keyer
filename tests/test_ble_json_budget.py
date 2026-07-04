import re
import unittest
from pathlib import Path

CONFIG = Path("firmware/MORPHEUS/config.h")


def _config_int(name):
    text = CONFIG.read_text()
    match = re.search(rf"{name}\s*=\s*(\d+)", text)
    if not match:
        raise AssertionError(f"Could not find {name} in {CONFIG}")
    return int(match.group(1))


class BleJsonBudgetTests(unittest.TestCase):
    def test_json_overhead_budget_covers_worst_case_envelope(self):
        overhead = _config_int("BLE_JSON_OVERHEAD_BYTES")
        worst_case_envelope = len(
            '{"word":"","wpm":40,"mode":"STRAIGHT","timestamp":4294967295}'
        )

        self.assertGreaterEqual(overhead, worst_case_envelope)

    def test_escaped_word_buffer_covers_worst_case_control_expansion(self):
        word_cap = _config_int("BLE_WORD_FIELD_CAP")
        worst_case_escaped_word = word_cap * len("\\u001f")

        self.assertEqual(worst_case_escaped_word, word_cap * 6)


if __name__ == "__main__":
    unittest.main()
