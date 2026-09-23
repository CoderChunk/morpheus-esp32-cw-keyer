"""
In-app BLE pairing via a BlueZ Agent1 D-Bus service - the same mechanism
a smartwatch companion app uses to show its own branded pairing dialog
instead of sending the user to a separate OS Settings app.

The security model is unchanged: MORPHEUS is IO-capability DisplayOnly
with MITM protection, so a human still has to read the passkey off the
MORPHEUS OLED and enter/confirm it here - that's what makes the pairing
trustworthy. What this removes is the terminal/OS-Settings detour.

Runs on its own background thread with its own asyncio loop (separate
from ble_client_core.BleWorker's loop - pairing and the post-pairing
GATT connection are deliberately independent sessions).
"""

import asyncio
import threading

from PySide6.QtCore import QObject, Signal

from dbus_next import BusType, Variant
from dbus_next.aio import MessageBus
from dbus_next.errors import DBusError
from dbus_next.service import ServiceInterface, method

AGENT_PATH = "/com/morpheus/bleclient/agent"
BLUEZ = "org.bluez"
DISCOVERY_TIMEOUT_S = 15.0


class _PairingAgentInterface(ServiceInterface):
    """BlueZ org.bluez.Agent1 implementation. Methods that need a human
    answer await an asyncio.Future the worker resolves once the GUI
    thread calls submit_passkey()/submit_confirmation().
    """

    def __init__(self, worker: "PairingWorker"):
        super().__init__("org.bluez.Agent1")
        self._worker = worker

    @method()
    def Release(self):
        pass

    @method()
    async def RequestPasskey(self, device: "o") -> "u":  # noqa: F821
        return await self._worker._await_passkey_entry(device)

    @method()
    def DisplayPasskey(self, device: "o", passkey: "u", entered: "q"):  # noqa: F821
        self._worker.passkey_display.emit(device, passkey)

    @method()
    async def RequestConfirmation(self, device: "o", passkey: "u"):  # noqa: F821
        accepted = await self._worker._await_confirmation(device, passkey)
        if not accepted:
            raise DBusError("org.bluez.Error.Rejected", "Rejected by user")

    @method()
    async def RequestAuthorization(self, device: "o"):  # noqa: F821
        return None

    @method()
    def AuthorizeService(self, device: "o", uuid: "s"):  # noqa: F821
        return None

    @method()
    def Cancel(self):
        pass


class PairingWorker(QObject):
    status_changed = Signal(str)
    passkey_entry_needed = Signal(str)          # device object path
    confirmation_needed = Signal(str, int)       # device object path, passkey
    passkey_display = Signal(str, int)           # informational only
    pairing_finished = Signal(bool, str)         # ok, message (device path on success)

    def __init__(self):
        super().__init__()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._passkey_future: asyncio.Future | None = None
        self._confirm_future: asyncio.Future | None = None

    # ------------------------------------------------------------------
    def start_pairing(self, target_name: str):
        if self._thread and self._thread.is_alive():
            return
        self._thread = threading.Thread(target=self._run, args=(target_name,), daemon=True)
        self._thread.start()

    def submit_passkey(self, value: int):
        self._resolve(self._passkey_future, value)

    def submit_confirmation(self, accepted: bool):
        self._resolve(self._confirm_future, accepted)

    def _resolve(self, future, value):
        if self._loop is None or future is None or future.done():
            return
        self._loop.call_soon_threadsafe(future.set_result, value)

    # ------------------------------------------------------------------
    async def _await_passkey_entry(self, device_path: str) -> int:
        self._passkey_future = self._loop.create_future()
        self.passkey_entry_needed.emit(device_path)
        try:
            return await self._passkey_future
        finally:
            self._passkey_future = None

    async def _await_confirmation(self, device_path: str, passkey: int) -> bool:
        self._confirm_future = self._loop.create_future()
        self.confirmation_needed.emit(device_path, passkey)
        try:
            return await self._confirm_future
        finally:
            self._confirm_future = None

    # ------------------------------------------------------------------
    def _run(self, target_name: str):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        try:
            self._loop.run_until_complete(self._async_main(target_name))
        except Exception as exc:  # noqa: BLE001
            self.pairing_finished.emit(False, str(exc))
        finally:
            self._loop.close()
            self._loop = None

    async def _async_main(self, target_name: str):
        self.status_changed.emit("Connecting to system D-Bus...")
        bus = await MessageBus(bus_type=BusType.SYSTEM).connect()

        agent = _PairingAgentInterface(self)
        bus.export(AGENT_PATH, agent)

        bluez_intr = await bus.introspect(BLUEZ, "/org/bluez")
        bluez_obj = bus.get_proxy_object(BLUEZ, "/org/bluez", bluez_intr)
        agent_mgr = bluez_obj.get_interface("org.bluez.AgentManager1")
        await agent_mgr.call_register_agent(AGENT_PATH, "KeyboardDisplay")
        await agent_mgr.call_request_default_agent(AGENT_PATH)

        root_intr = await bus.introspect(BLUEZ, "/")
        root_obj = bus.get_proxy_object(BLUEZ, "/", root_intr)
        om = root_obj.get_interface("org.freedesktop.DBus.ObjectManager")
        objects = await om.call_get_managed_objects()

        adapter_path = next((p for p, ifaces in objects.items() if "org.bluez.Adapter1" in ifaces), None)
        if adapter_path is None:
            self.pairing_finished.emit(False, "No Bluetooth adapter found")
            return

        adapter_intr = await bus.introspect(BLUEZ, adapter_path)
        adapter_obj = bus.get_proxy_object(BLUEZ, adapter_path, adapter_intr)
        adapter = adapter_obj.get_interface("org.bluez.Adapter1")

        found_path = next(
            (p for p, ifaces in objects.items()
             if "org.bluez.Device1" in ifaces and ifaces["org.bluez.Device1"].get("Name")
             and ifaces["org.bluez.Device1"]["Name"].value == target_name),
            None,
        )

        if found_path is None:
            self.status_changed.emit(f"Scanning for {target_name}...")
            found_event = asyncio.Event()
            found_holder = {}

            def on_interfaces_added(path, interfaces):
                dev = interfaces.get("org.bluez.Device1")
                if dev and dev.get("Name") and dev["Name"].value == target_name:
                    found_holder["path"] = path
                    found_event.set()

            om.on_interfaces_added(on_interfaces_added)
            await adapter.call_start_discovery()
            try:
                await asyncio.wait_for(found_event.wait(), timeout=DISCOVERY_TIMEOUT_S)
                found_path = found_holder.get("path")
            except asyncio.TimeoutError:
                pass
            finally:
                try:
                    await adapter.call_stop_discovery()
                except DBusError:
                    pass
                om.off_interfaces_added(on_interfaces_added)

        if found_path is None:
            self.pairing_finished.emit(False, f"{target_name} not found within {DISCOVERY_TIMEOUT_S:.0f}s")
            return

        self.status_changed.emit("Pairing...")
        dev_intr = await bus.introspect(BLUEZ, found_path)
        dev_obj = bus.get_proxy_object(BLUEZ, found_path, dev_intr)
        device_iface = dev_obj.get_interface("org.bluez.Device1")
        props_iface = dev_obj.get_interface("org.freedesktop.DBus.Properties")

        try:
            already_paired = await props_iface.call_get("org.bluez.Device1", "Paired")
            if not already_paired.value:
                await device_iface.call_pair()
            await props_iface.call_set("org.bluez.Device1", "Trusted", Variant("b", True))
        except DBusError as exc:
            self.pairing_finished.emit(False, f"{exc}")
            return

        self.pairing_finished.emit(True, found_path)
