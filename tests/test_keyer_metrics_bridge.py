"""Real backend queue, parser and WS dispatch; no hardware is accessed."""
import asyncio
import json
import sys
import time
import unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'ble_client'))
from backend import MorpheusBackend
from ws_server import MorpheusWebSocketServer, ClientSession

class RadioFixture:
    is_connected = True
    def __init__(self, backend): self.backend = backend
    async def write_gatt_char(self, _uuid, data, response):
        command = json.loads(data)
        if command['cmd'] in ('probe_keyer', 'reset_keyer_metrics'):
            self.backend._on_control_notify(None, bytearray(json.dumps(dict(
                evt='keyer_metrics', id=command['id'], ts=400, seq=3,
                ditMs=73, dahMs=213, gapMs=218, virtual=False)).encode()))

class MetricsBridgeTests(unittest.IsolatedAsyncioTestCase):
    async def test_queue_to_real_parser_measures_only_matching_device_reply(self):
        backend = MorpheusBackend()
        backend._loop = asyncio.get_running_loop()
        backend._client = RadioFixture(backend)
        backend._cmd_queue = asyncio.PriorityQueue()
        received = []
        backend.on_keyer_metrics(received.append)
        with patch('backend.time.perf_counter', side_effect=[100.0, 100.043]):
            writer = asyncio.create_task(backend._command_writer())
            await backend._enqueue_command(2, {'cmd': 'probe_keyer', 'id': 'test1'}, 'probeKeyerMetrics')
            await asyncio.sleep(.01)
            writer.cancel()
            await asyncio.gather(writer, return_exceptions=True)
        self.assertEqual(received[0]['ditMs'], 73)
        self.assertEqual(received[0]['gapMs'], 218)
        self.assertAlmostEqual(received[0]['bleRoundTripMs'], 43.0)
        # Cached GATT state with the same id cannot produce a second or fabricated RTT.
        backend._handle_control_payload(received[0])
        backend._handle_control_payload({'evt': 'keyer_metrics', 'id': []})
        self.assertEqual(len(received), 1)

    async def test_timeouts_disconnects_and_invalid_ids_never_invent_measurements(self):
        backend = MorpheusBackend()
        events = []; backend.on_keyer_metrics(events.append)
        backend.request_keyer_metrics('offline1')
        self.assertEqual(events, [{'id': 'offline1', 'unavailable': True}])
        timer = asyncio.get_running_loop().call_later(10, lambda: None)
        backend._metric_probes['expired1'] = (time.perf_counter(), timer)
        backend._expire_metrics('expired1')
        self.assertEqual(events[-1], {'id': 'expired1', 'unavailable': True})
        self.assertTrue(timer.cancelled())
        for probe_id in ['', 'x' * 25, 'bad"id', 'é', None]:
            with self.assertRaises(ValueError): backend.request_keyer_metrics(probe_id)

    async def test_ws_probe_dispatch_does_not_block_key_up(self):
        server = MorpheusWebSocketServer()
        calls = []
        server.backend.request_keyer_metrics = lambda token, reset: calls.append((token, reset))
        server.backend.key_up = lambda: calls.append('up')
        session = ClientSession(None)
        await server._dispatch(session, 'probeKeyerMetrics', {'id': 'test123', 'reset': True})
        await server._dispatch(session, 'keyUp', {})
        self.assertEqual(calls, [('test123', True), 'up'])

if __name__ == '__main__': unittest.main()
