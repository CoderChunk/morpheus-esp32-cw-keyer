"""Exercise real desktop command serialization/validation, with radio disabled."""
import json
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'/'ble_client'))
from backend import MorpheusBackend
class Commands(unittest.TestCase):
    def setUp(self):
        self.backend=MorpheusBackend()
        self.sent=[]
        self.backend._send_command=lambda command,operation=None:self.sent.append(command)
    def test_settings_range_and_budget(self):
        for field,value in [('wpm',40),('tone',2000),('volume',100),('mode',1),('iambic',1),('reversed',1),('sidetone',0),('weight',70)]:
            self.backend.set_keyer_setting(field,value)
            self.assertLess(len(json.dumps(self.sent[-1],separators=(',',':')).encode()),96)
        before=len(self.sent)
        for field,value in [('wpm',41),('tone',199),('volume',101),('mode',True),('weight',30.5),('other',1)]:
            with self.assertRaises(ValueError):self.backend.set_keyer_setting(field,value)
        self.assertEqual(len(self.sent),before)
    def test_koch_pool_is_optional_and_validated(self):
        self.backend.start_training('LISTENING')
        self.assertEqual(self.sent[-1],{'cmd':'train_start','mode':'LISTENING'})
        self.backend.start_training('LISTENING',2)
        self.assertEqual(self.sent[-1]['kochLevel'],2)
        for level in [1,41,True,2.5,'2']:
            with self.assertRaises(ValueError):self.backend.start_training('LISTENING',level)
    def test_scoped_counter_malformed_values_are_not_forwarded(self):
        events=[];self.backend.on_game_morse(events.append)
        for raw in [[],dict(evt='game_morse',game='COPY',char='A',run=True,seq=1),dict(evt='game_morse',game='COPY',char='A',run=1,seq=0)]:
            self.backend._on_game_morse_notify(None,bytearray(json.dumps(raw).encode()))
        self.assertEqual(events,[])
if __name__=='__main__':unittest.main()
