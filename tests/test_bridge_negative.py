"""NEG-B01 .. NEG-B10 (docs/TEST_SUITE.md). Negative tests of the real bridge parser/queue (no hardware): hostile or unexpected
input must be contained, reported once, and never wedge the bridge."""
import asyncio
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "ble_client"))
from backend import MorpheusBackend  # noqa: E402


class FakeClient:
    is_connected = True

    def __init__(self, evt): self.evt = evt
    async def read_gatt_char(self, _uuid): return bytearray(json.dumps(self.evt).encode())


def backend_with(evt=None):
    b = MorpheusBackend()
    b._client = FakeClient(evt or {})
    errors, training = [], []
    b.on_error(errors.append)
    b.on_training_state(training.append)
    return b, errors, training


class ControlPayloadNegativeTests(unittest.TestCase):
    def test_neg_b01_malformed_json_is_reported_not_raised(self):
        b, errors, _ = backend_with()
        b._on_control_notify(None, bytearray(b"{not json"))
        self.assertEqual(len(errors), 1)

    def test_neg_b02_non_utf8_payload_is_reported_not_raised(self):
        b, errors, _ = backend_with()
        b._on_control_notify(None, bytearray(b"\xff\xfe\x00"))
        self.assertEqual(len(errors), 1)

    def test_neg_b03_unknown_event_is_ignored(self):
        b, errors, training = backend_with()
        b._on_control_notify(None, bytearray(json.dumps({"evt": "from_the_future", "x": 1}).encode()))
        self.assertEqual((errors, training), ([], []))

    def test_neg_b04_metrics_with_non_string_id_is_ignored(self):
        b, errors, _ = backend_with()
        for bad in (None, 5, ["a"], {"a": 1}):
            b._handle_control_payload({"evt": "keyer_metrics", "id": bad})
        self.assertEqual(errors, [])

    def test_neg_b05_metrics_for_an_id_nobody_asked_for_is_ignored(self):
        b, errors, _ = backend_with()
        seen = []
        b.on_keyer_metrics(seen.append)
        b._handle_control_payload({"evt": "keyer_metrics", "id": "neverasked", "ditMs": 60})
        self.assertEqual(seen, [])

    def test_neg_b06_state_event_with_missing_fields_does_not_raise(self):
        b, _, training = backend_with()
        b._handle_control_payload({"evt": "train_state"})
        self.assertEqual(len(training), 1)
        self.assertFalse(training[0].active)


class ReconcileNegativeTests(unittest.IsolatedAsyncioTestCase):
    async def test_neg_b07_error_event_is_not_re_emitted_by_the_post_command_re_read(self):
        b, errors, _ = backend_with({"evt": "error", "message": "keyer busy"})
        b._on_control_notify(None, bytearray(json.dumps({"evt": "error", "message": "keyer busy"}).encode()))
        await b._reconcileControlState()          # re-reads the same error
        self.assertEqual([e.message for e in errors], ["keyer busy"])

    async def test_neg_b08_state_is_still_corrected_by_the_re_read(self):
        b, _, training = backend_with({"evt": "train_state", "active": True, "mode": "WORDS"})
        await b._reconcileControlState()
        self.assertTrue(training and training[-1].active)

    async def test_neg_b09_re_read_failure_is_swallowed(self):
        class Broken(FakeClient):
            async def read_gatt_char(self, _u): raise OSError("link lost")
        b, errors, _ = backend_with()
        b._client = Broken({})
        await b._reconcileControlState()
        self.assertEqual(errors, [])

    async def test_neg_b10_commands_while_not_connected_report_an_error(self):
        b = MorpheusBackend()
        errors = []
        b.on_error(errors.append)
        b.key_down()
        await asyncio.sleep(0.05)
        self.assertTrue(errors)


class DuplicateRefusalTests(unittest.IsolatedAsyncioTestCase):
    async def test_a_refusal_is_reported_once_per_command_and_again_for_a_new_command(self):
        b, errors, _ = backend_with({"evt": "error", "message": "keyer busy"})
        evt = bytearray(json.dumps({"evt": "error", "message": "keyer busy"}).encode())
        b._on_control_notify(None, evt)          # the notification
        b._on_control_notify(None, evt)          # BlueZ echo of the re-read
        await b._reconcileControlState()         # the re-read itself
        self.assertEqual([e.message for e in errors], ["keyer busy"])

        class Radio:
            is_connected = True
            async def write_gatt_char(self, *_a, **_k): pass
        b._client = Radio()
        b._loop = asyncio.get_running_loop()
        b._cmd_queue = asyncio.PriorityQueue()
        writer = asyncio.create_task(b._command_writer())
        await b._enqueue_command(2, {"cmd": "set_keyer", "field": "wpm", "value": 20}, "setKeyerSetting")
        await asyncio.sleep(0.05)                # a NEW command is written
        b._on_control_notify(None, evt)          # ...and it is refused too
        writer.cancel(); await asyncio.gather(writer, return_exceptions=True)
        self.assertEqual([e.message for e in errors], ["keyer busy", "keyer busy"])


class EchoOfReadTests(unittest.IsolatedAsyncioTestCase):
    async def test_a_notification_identical_to_our_own_read_is_dropped_for_a_short_window(self):
        evt = {"evt": "error", "message": "keyer busy"}
        b, errors, _ = backend_with(evt)
        await b._reconcileControlState()                      # reads (and reports once is skipped: errors are events)
        b._on_control_notify(None, bytearray(json.dumps(evt).encode()))   # BlueZ echo of that read
        self.assertEqual(errors, [])
        b._echo_guard = (b._echo_guard[0], 0)                 # window over: a real, identical event is reported
        b._on_control_notify(None, bytearray(json.dumps(evt).encode()))
        self.assertEqual([e.message for e in errors], ["keyer busy"])

    async def test_a_different_event_inside_the_window_is_not_dropped(self):
        b, _, training = backend_with({"evt": "train_state", "active": False})
        await b._reconcileControlState()
        b._on_control_notify(None, bytearray(json.dumps({"evt": "train_state", "active": True, "mode": "WORDS"}).encode()))
        self.assertTrue(training and training[-1].active)


class WsErrorCodeTests(unittest.IsolatedAsyncioTestCase):
    async def test_invalid_setting_is_reported_as_invalid_parameter_not_internal_error(self):
        from ws_server import MorpheusWebSocketServer, ClientSession, RequestError

        class Dummy:
            subscribed = set()
        server = MorpheusWebSocketServer("127.0.0.1", 0)
        for field, value in (("wpm", 999), ("nope", 1), ("wpm", "20"), ("wpm", True), (None, None)):
            with self.assertRaises(RequestError) as ctx:
                await server._dispatch(ClientSession(Dummy()), "setKeyerSetting", {"field": field, "value": value})
            self.assertEqual(ctx.exception.code, "INVALID_PARAMETER", (field, value))


if __name__ == "__main__":
    unittest.main()
