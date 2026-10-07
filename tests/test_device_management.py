"""Discovery, exact-device bonds and pairing cancellation; no Bluetooth hardware."""
import asyncio
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools' / 'ble_client'))
from backend import MorpheusBackend, ConnectionInfo, ConnectionState, PairingEventType
from ws_server import MorpheusWebSocketServer, ClientSession
from dbus_next import Variant
import pairing_backend

A = 'AA:00:00:00:00:01'
B = 'AA:00:00:00:00:02'

class BluezFixture:
    def __init__(self, paired=False):
        self.removed = []; self.trusted = []; self.pairs = []; self.cancelled = []
        self.disconnected = False; self.unregistered = False; self.driver = None
        self.objects = {'/org/bluez/hci0': {'org.bluez.Adapter1': {}}}
        for address in [A, B]:
            path = '/org/bluez/hci0/dev_' + address.replace(':', '_')
            self.objects[path] = {'org.bluez.Device1': {
                'Name': Variant('s', 'MORPHEUS-CW'), 'Address': Variant('s', address),
                'Adapter': Variant('o', '/org/bluez/hci0'), 'Paired': Variant('b', paired)}}
    async def connect(self): return self
    async def introspect(self, *_): return None
    def export(self, *_): pass
    def unexport(self, *_): pass
    def disconnect(self): self.disconnected = True
    def get_proxy_object(self, _name, path, _intr):
        bus = self
        class Proxy:
            def get_interface(self, interface):
                return bus.interface(path, interface)
        return Proxy()
    def interface(self, path, interface):
        bus = self
        class Interface:
            async def call_register_agent(self, *_): pass
            async def call_request_default_agent(self, *_): pass
            async def call_unregister_agent(self, *_): bus.unregistered = True
            async def call_get_managed_objects(self): return bus.objects
            async def call_get(self, _interface, key): return bus.objects[path]['org.bluez.Device1'][key]
            async def call_set(self, _interface, key, value): bus.trusted.append((path, key, value.value))
            async def call_pair(self):
                bus.pairs.append(path)
                if bus.driver:
                    await bus.driver.await_passkey_entry(path)
            async def call_cancel_pairing(self): bus.cancelled.append(path)
            async def call_remove_device(self, target): bus.removed.append(target); bus.objects.pop(target)
        return Interface()

class DeviceManagementTests(unittest.IsolatedAsyncioTestCase):
    async def test_discovery_keeps_active_connection_and_returns_all_real_rssi(self):
        backend = MorpheusBackend()
        backend._connection = ConnectionInfo(ConnectionState.CONNECTED, deviceName='MORPHEUS-CW', deviceAddress=A)
        def radio(address, name, rssi, uuids=[]):
            return (SimpleNamespace(address=address, name=name), SimpleNamespace(local_name=name, rssi=rssi, service_uuids=uuids))
        found = {A: radio(A, 'MORPHEUS-CW', -63), B: radio(B, 'MORPHEUS-CW', -74), 'foreign': radio('C', 'OTHER', -30)}
        with patch('backend.BleakScanner.discover', AsyncMock(return_value=found)):
            rows = await backend.discover_devices()
        self.assertEqual([r['identifier'] for r in rows], [A, B])
        self.assertEqual([r['rssi'] for r in rows], [-63, -74])
        self.assertEqual(backend.connection.state, ConnectionState.CONNECTED)
        self.assertEqual(backend.get_device_runtime()['rssi'], -63)
        self.assertIsNone(backend.get_device_runtime()['signalPercent'])
        backend._advertisements[A] = (-1e6, rows[0])
        self.assertIsNone(backend.get_device_runtime()['rssi'])

    async def test_two_same_named_devices_pair_only_selected_address_and_cleanup_agent(self):
        bus = BluezFixture()
        succeeded = []
        driver = pairing_backend.PairingDriver(asyncio.get_running_loop(),
            lambda path: driver.submit_passkey(34), lambda *_: None, lambda *_: None,
            succeeded.append, lambda *_: self.fail('Unexpected pairing failure'))
        bus.driver = driver
        with patch('pairing_backend.MessageBus', return_value=bus):
            await driver.run('MORPHEUS-CW', B)
        self.assertEqual(bus.pairs, ['/org/bluez/hci0/dev_' + B.replace(':', '_')])
        self.assertEqual(succeeded, bus.pairs)
        self.assertTrue(bus.unregistered); self.assertTrue(bus.disconnected)

    async def test_cancel_during_pin_request_cancels_bluez_and_unregisters_agent(self):
        bus = BluezFixture(); waiting = asyncio.Event(); success = []
        driver = pairing_backend.PairingDriver(asyncio.get_running_loop(), lambda _: waiting.set(),
            lambda *_: None, lambda *_: None, success.append, lambda *_: None)
        bus.driver = driver
        with patch('pairing_backend.MessageBus', return_value=bus):
            task = asyncio.create_task(driver.run('MORPHEUS-CW', A))
            await asyncio.wait_for(waiting.wait(), 1)
            driver.cancel()
            with self.assertRaises(asyncio.CancelledError): await task
        self.assertEqual(success, [])
        self.assertEqual(bus.cancelled, bus.pairs)
        self.assertTrue(bus.unregistered); self.assertTrue(bus.disconnected)
        self.assertIsNone(driver._passkey_future)

    async def test_actual_host_bonds_and_remove_one_exact_pairing(self):
        bus = BluezFixture(paired=True)
        with patch('pairing_backend.MessageBus', return_value=bus):
            rows = await pairing_backend.paired_devices()
            self.assertEqual({r['identifier'] for r in rows}, {A, B})
            self.assertTrue(all(r['paired'] for r in rows))
            await pairing_backend.remove_pairing(B.lower())
            remaining = await pairing_backend.paired_devices()
        self.assertEqual([r['identifier'] for r in remaining], [A])
        self.assertEqual(bus.removed, ['/org/bluez/hci0/dev_' + B.replace(':', '_')])

    async def test_pin_and_confirmation_validation_and_remove_active_device_rejection(self):
        backend = MorpheusBackend(); pins = []
        backend._pairing_attempt = 'attempt1'
        backend._pairing_driver = SimpleNamespace(submit_passkey=pins.append)
        for pin in ['', '12345', '1234567', '12x456', 123456, None]:
            with self.assertRaises(ValueError): backend.submit_passkey(pin, 'attempt1')
        with self.assertRaises(ValueError): backend.submit_passkey('000034', 'stale')
        backend.submit_passkey('000034', 'attempt1')
        self.assertEqual(pins, [34])
        backend._connection = ConnectionInfo(ConnectionState.CONNECTED, deviceAddress=A)
        with self.assertRaises(ValueError): await backend.remove_pairing(A)

    async def test_ws_api_forwards_device_identifier_and_attempt_without_false_success(self):
        server = MorpheusWebSocketServer(); session = ClientSession(None); calls = []
        server.backend.wait_pairing_idle = AsyncMock()
        server.backend.start_pairing = lambda *args: calls.append(args)
        server.backend.submit_passkey = lambda *args: calls.append(args)
        server.backend.discover_devices = AsyncMock(return_value=[dict(identifier=A, name='MORPHEUS-CW')])
        self.assertIsNone(await server._dispatch(session, 'startPairing', dict(deviceAddress=B, attemptId='p1')))
        await server._dispatch(session, 'submitPasskey', dict(passkey='000034', attemptId='p1'))
        self.assertEqual(calls[0], ('MORPHEUS-CW', B, 'p1'))
        self.assertEqual(calls[1], ('000034', 'p1'))
        self.assertEqual((await server._dispatch(session, 'discoverDevices', {}))['devices'][0]['identifier'], A)

    async def test_system_pairing_selects_exact_identifier_and_cancel_never_claims_success(self):
        backend = MorpheusBackend(); events = []
        backend.on_pairing_state(events.append)
        backend.connect = Mock(); backend.disconnect = Mock()
        with patch.object(backend, '_try_import_pairing_backend', return_value=None):
            backend.start_pairing('MORPHEUS-CW', B, 'p1')
            backend.connect.assert_called_once_with(B)
            self.assertEqual(events[-1].type, PairingEventType.PAIRING_SYSTEM_PROMPT)
            self.assertEqual(events[-1].attemptId, 'p1')
            self.assertFalse(backend.get_device_runtime()['canListPairings'])
            backend.cancel_pairing('p1')
            backend.disconnect.assert_called_once()
            self.assertEqual(events[-1].type, PairingEventType.PAIRING_CANCELLED)
            self.assertIsNone(backend._system_pairing)
        with patch('backend.sys.platform', 'darwin'):
            self.assertTrue(backend._valid_identifier('00000000-0000-0000-0000-000000000001'))
            self.assertFalse(backend._valid_identifier(B))

    async def test_connect_ready_requires_notifications_and_authenticated_read(self):
        import protocol as proto
        backend = MorpheusBackend(); states = []; events = []; order = []
        backend.on_connection_changed(lambda s: states.append(s.state))
        backend.on_pairing_state(events.append)
        backend._system_pairing = (B, 'p1')
        device = SimpleNamespace(address=B, name='MORPHEUS-CW')
        class Client:
            is_connected = False
            services = SimpleNamespace(get_characteristic=lambda _: None)
            async def __aenter__(self): return self
            async def __aexit__(self, *_): pass
            async def start_notify(self, characteristic, callback): order.append(characteristic)
            async def read_gatt_char(self, characteristic):
                self_test.assertNotIn(ConnectionState.CONNECTED, states)
                self_test.assertEqual(order, [proto.WORD_CHAR_UUID, proto.CONTROL_EVT_UUID])
                order.append('authenticated_read'); return b'{}'
        self_test = self
        with patch('backend.BleakScanner.find_device_by_address', AsyncMock(return_value=device)), patch('backend.BleakClient', return_value=Client()) as make_client:
            await backend._async_connect_main(B)
        self.assertIn(ConnectionState.CONNECTED, states)
        self.assertEqual(events[-1].type, PairingEventType.PAIRING_SUCCEEDED)
        self.assertTrue(make_client.call_args.kwargs['pair'])
        self.assertFalse(backend._notifications_ready)
        self.assertIsNone(backend._last_seen)

    async def test_failed_authenticated_read_never_reports_connected_or_successful_pairing(self):
        backend = MorpheusBackend(); states = []; events = []
        backend.on_connection_changed(lambda s: states.append(s.state))
        backend.on_pairing_state(events.append)
        backend._system_pairing = (B, 'p1')
        class Client:
            is_connected = False
            services = SimpleNamespace(get_characteristic=lambda _: None)
            async def __aenter__(self): return self
            async def __aexit__(self, *_): pass
            async def start_notify(self, *_): pass
            async def read_gatt_char(self, *_): raise RuntimeError('Authentication failed')
        with patch('backend.BleakScanner.find_device_by_address', AsyncMock(return_value=SimpleNamespace(address=B, name='MORPHEUS-CW'))), patch('backend.BleakClient', return_value=Client()):
            await backend._async_connect_main(B)
        self.assertNotIn(ConnectionState.CONNECTED, states); self.assertEqual(events, [])
        self.assertFalse(backend._notifications_ready)
