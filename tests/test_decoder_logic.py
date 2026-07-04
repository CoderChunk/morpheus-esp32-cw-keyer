import unittest

DIT = "."
DAH = "-"
DEFAULT_WPM = 15
MAX_PATTERN_LEN = 8
MAX_WORD_LEN = 64
CHAR_GAP_MULT = 3.0
WORD_GAP_MULT = 7.0

MORSE = {
    ".": "E",
    "-": "T",
    "...": "S",
    "---": "O",
}


class DecoderModel:
    def __init__(self, wpm=DEFAULT_WPM):
        self.dit_length_ms = 1200 // wpm
        self.char_pattern = ""
        self.char_pending = False
        self.last_element_end_ms = 0
        self.word_buffer = ""
        self.completed_chars = []
        self.completed_words = []

    def add_element(self, element, now):
        if len(self.char_pattern) < MAX_PATTERN_LEN - 1:
            self.char_pattern += element
        self.last_element_end_ms = now
        self.char_pending = True

    def service(self, now, tx_active=False):
        if tx_active:
            return

        silence = now - self.last_element_end_ms
        if self.char_pending and silence >= int(self.dit_length_ms * CHAR_GAP_MULT):
            self._finalize_character()
        if (
            not self.char_pending
            and self.word_buffer
            and silence >= int(self.dit_length_ms * WORD_GAP_MULT)
        ):
            self._finalize_word()

    def _finalize_character(self):
        decoded = MORSE.get(self.char_pattern, "?")
        self.completed_chars.append((decoded, self.char_pattern))
        if len(self.word_buffer) < MAX_WORD_LEN - 1:
            self.word_buffer += decoded
        self.char_pattern = ""
        self.char_pending = False

    def _finalize_word(self):
        self.completed_words.append((self.word_buffer, self.last_element_end_ms))
        self.word_buffer = ""


class DecoderLogicTests(unittest.TestCase):
    def test_decodes_single_dit_after_character_gap(self):
        decoder = DecoderModel()
        decoder.add_element(DIT, now=100)
        decoder.service(100 + int(decoder.dit_length_ms * CHAR_GAP_MULT))

        self.assertEqual(decoder.completed_chars, [("E", ".")])
        self.assertEqual(decoder.word_buffer, "E")
        self.assertFalse(decoder.char_pending)

    def test_decodes_sos_and_finalizes_word_after_word_gap(self):
        decoder = DecoderModel()
        now = 100
        for pattern in ("...", "---", "..."):
            for element in pattern:
                decoder.add_element(element, now)
                now += decoder.dit_length_ms
            decoder.service(now + int(decoder.dit_length_ms * CHAR_GAP_MULT))
            now += int(decoder.dit_length_ms * CHAR_GAP_MULT)

        decoder.service(decoder.last_element_end_ms + int(decoder.dit_length_ms * WORD_GAP_MULT))

        self.assertEqual([char for char, _pattern in decoder.completed_chars], ["S", "O", "S"])
        self.assertEqual(decoder.completed_words, [("SOS", decoder.last_element_end_ms)])
        self.assertEqual(decoder.word_buffer, "")

    def test_does_not_finalize_character_while_transmitting(self):
        decoder = DecoderModel()
        decoder.add_element(DAH, now=50)
        decoder.service(50 + int(decoder.dit_length_ms * CHAR_GAP_MULT), tx_active=True)

        self.assertEqual(decoder.completed_chars, [])
        self.assertEqual(decoder.char_pattern, "-")
        self.assertTrue(decoder.char_pending)

    def test_pattern_buffer_is_bounded_to_leave_null_terminator_space(self):
        decoder = DecoderModel()
        for i in range(MAX_PATTERN_LEN + 3):
            decoder.add_element(DIT, now=i)

        self.assertEqual(len(decoder.char_pattern), MAX_PATTERN_LEN - 1)


if __name__ == "__main__":
    unittest.main()
