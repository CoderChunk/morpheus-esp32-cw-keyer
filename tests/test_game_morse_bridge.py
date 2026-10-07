"""Exercise the real backend parser; no radio or notification mocks in the parser."""
import json
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'ble_client'))
from backend import MorpheusBackend

class GameMorseBridgeTests(unittest.TestCase):
    def test_scoped_event_preserves_decoder_fields_and_legacy_word_events(self):
        backend = MorpheusBackend()
        games, words = [], []
        backend.on_game_morse(games.append)
        backend.on_keyer_word(words.append)
        evt = dict(evt='game_morse', game='MEMORY', run=2, seq=3, char='A', pattern='.-', timestamp=900)
        backend._on_game_morse_notify(None, bytearray(json.dumps(evt).encode()))
        backend._on_word_notify(None, bytearray(json.dumps(dict(word='CQ', wpm=18, mode='STRAIGHT', timestamp=1000)).encode()))
        self.assertEqual(games, [evt])
        self.assertEqual(words[0].word, 'CQ')
    def test_malformed_or_unknown_scope_is_not_forwarded(self):
        backend = MorpheusBackend()
        events = []
        backend.on_game_morse(events.append)
        for data in [b'{bad', b'{}', b'{"evt":"game_morse","game":"FAKE","char":"A","run":1,"seq":1}']:
            backend._on_game_morse_notify(None, bytearray(data))
        self.assertEqual(events, [])
